// Copyright (c) 2026 Beas Audio Engineering. All Rights Reserved.
// Proprietary — no license granted.
// Beas Audio DSP Library — HRTF database & overlap-add convolution
#include "hrtf.h"
#include "pffft.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <new>

static constexpr float DEG2RAD = 3.14159265f / 180.0f;
static constexpr float RAD2DEG = 180.0f / 3.14159265f;

// ==========================================================================
// 1. ANGLE-TO-CELL LOOKUP
// ==========================================================================

static int angle_to_cell(float az_deg, float el_deg) {
    while (az_deg > 180.0f)  az_deg -= 360.0f;
    while (az_deg < -180.0f) az_deg += 360.0f;
    el_deg = std::max(-90.0f, std::min(90.0f, el_deg));
    int cell_az = (int)((az_deg + 180.0f) / 10.0f) % BA_GRID_AZ_CELLS;
    if (cell_az < 0) cell_az += BA_GRID_AZ_CELLS;
    int cell_el = (int)((el_deg + 90.0f) / 30.0f);
    cell_el = std::max(0, std::min(BA_GRID_EL_CELLS - 1, cell_el));
    return cell_az * BA_GRID_EL_CELLS + cell_el;
}

const BaHRTFEntry* ba_hrtf_lookup(const BaHRTFDatabase* db,
                                   float az_rad, float el_rad) {
    int cell = angle_to_cell(az_rad * RAD2DEG, el_rad * RAD2DEG);
    return &db->cells[cell];
}

// ==========================================================================
// 2. MATH-BASED HRTF GENERATOR (full 216-cell grid)
// ==========================================================================

void ba_hrtf_generate_math(BaHRTFDatabase* db, uint32_t sample_rate, int ir_length,
                           PFFFT_Setup* fft_setup, int fft_size) {
    std::memset(db, 0, sizeof(BaHRTFDatabase));
    db->ir_length = ir_length;
    db->sample_rate = sample_rate;
    db->loaded = false;

    // Allocate cells on heap (avoids ~3MB BSS that crashes GNU ld 2.45)
    db->cells = (BaHRTFEntry*)pffft_aligned_malloc(
        (size_t)BA_GRID_TOTAL * sizeof(BaHRTFEntry));
    if (!db->cells) return;
    std::memset(db->cells, 0, (size_t)BA_GRID_TOTAL * sizeof(BaHRTFEntry));

    const float head_radius = 0.0875f;   // 8.75 cm average
    const float speed_sound = 343.0f;    // m/s

    for (int ca = 0; ca < BA_GRID_AZ_CELLS; ++ca) {
        for (int ce = 0; ce < BA_GRID_EL_CELLS; ++ce) {
            int cell = ca * BA_GRID_EL_CELLS + ce;
            float az_deg = -180.0f + ca * 10.0f + 5.0f;  // cell centre
            float el_deg = -90.0f  + ce * 30.0f + 15.0f;
            float az_rad = az_deg * DEG2RAD;
            float el_rad = el_deg * DEG2RAD;

            // Woodworth ITD model
            float itd_s = (head_radius / speed_sound) * (az_rad + std::sin(az_rad));
            int itd_samps = (int)std::round(itd_s * (float)sample_rate);
            itd_samps = std::max(-ir_length / 4, std::min(ir_length / 4, itd_samps));

            // ILD — simple sine-shaped panning with elevation scaling
            float ild_L = 0.5f + 0.5f * std::sin(az_rad) * std::cos(el_rad);
            float ild_R = 0.5f - 0.5f * std::sin(az_rad) * std::cos(el_rad);
            ild_L = std::max(0.1f, std::min(1.0f, ild_L));
            ild_R = std::max(0.1f, std::min(1.0f, ild_R));

            // Pinna shadow taper for elevation
            float pinna = 1.0f - 0.3f * std::fabs(std::sin(el_rad));

            // Build left IR — impulse + pinna notch
            auto& entry = db->cells[cell];
            std::memset(entry.h_L, 0, BA_HRTF_MAX_IR_LEN * sizeof(float));
            int tap_L = std::max(0, -itd_samps);
            if (tap_L < ir_length) {
                entry.h_L[tap_L] = ild_L * pinna;
                int notch = tap_L + (int)(0.0002f * sample_rate);
                if (notch < ir_length)
                    entry.h_L[notch] = -0.3f * ild_L * pinna;
            }

            // Build right IR
            std::memset(entry.h_R, 0, BA_HRTF_MAX_IR_LEN * sizeof(float));
            int tap_R = std::max(0, itd_samps);
            if (tap_R < ir_length) {
                entry.h_R[tap_R] = ild_R * pinna;
                int notch = tap_R + (int)(0.0002f * sample_rate);
                if (notch < ir_length)
                    entry.h_R[notch] = -0.3f * ild_R * pinna;
            }

            entry.azimuth_deg   = az_deg;
            entry.elevation_deg = el_deg;
            entry.ir_length = ir_length;
            entry.valid = true;
        }
    }

    // Pre-compute frequency-domain IRs for all cells (avoids per-frame FFT)
    if (fft_setup && fft_size > 0 && fft_size <= 1024) {
        size_t freq_sz = (size_t)BA_GRID_TOTAL * BA_HRTF_FREQ_BINS * sizeof(float);
        db->freq_L = (float*)pffft_aligned_malloc(freq_sz);
        db->freq_R = (float*)pffft_aligned_malloc(freq_sz);
        if (db->freq_L && db->freq_R) {
            for (int i = 0; i < BA_GRID_TOTAL; ++i) {
                auto& entry = db->cells[i];
                if (!entry.valid) continue;
                float* tmp = (float*)pffft_aligned_malloc(fft_size * sizeof(float));
                if (!tmp) continue;
                std::memset(tmp, 0, fft_size * sizeof(float));
                int cp = std::min(entry.ir_length, fft_size);
                std::memcpy(tmp, entry.h_L, cp * sizeof(float));
                pffft_transform_ordered(fft_setup, tmp,
                                        db->freq_L + i * BA_HRTF_FREQ_BINS,
                                        nullptr, PFFFT_FORWARD);
                std::memset(tmp, 0, fft_size * sizeof(float));
                std::memcpy(tmp, entry.h_R, cp * sizeof(float));
                pffft_transform_ordered(fft_setup, tmp,
                                        db->freq_R + i * BA_HRTF_FREQ_BINS,
                                        nullptr, PFFFT_FORWARD);
                pffft_aligned_free(tmp);
            }
            db->freq_valid = true;
        }
    }
}

// ==========================================================================
// 3. SINGLE-POSITION IR BUILDER (Woodworth + ILD + pinna)
// ==========================================================================

void ba_build_ir(BaIRSet* ir, float az_deg, float el_deg,
                 uint32_t sample_rate, int ir_length) {
    if (!ir) return;
    ir->azimuth_deg   = az_deg;
    ir->elevation_deg = el_deg;
    ir->ir_length = ir_length;

    const float PI = 3.14159265f;
    const float c  = 343.0f;
    const float r  = 0.0875f;

    float az_rad = az_deg * DEG2RAD;
    float el_rad = el_deg * DEG2RAD;

    // Woodworth ITD
    float itd_s = (r / c) * (az_rad + std::sin(az_rad));
    int itd_samps = (int)(itd_s * (float)sample_rate);
    itd_samps = std::max(-(ir_length - 1), std::min(ir_length - 1, itd_samps));

    // ILD
    float ild_db = 6.0f * std::sin(az_rad);
    float gain_R = std::pow(10.0f,  ild_db / 20.0f);
    float gain_L = std::pow(10.0f, -ild_db / 20.0f);

    // Elevation gain taper
    float el_gain = 0.85f + 0.15f * std::cos(el_rad);
    gain_L *= el_gain;
    gain_R *= el_gain;

    // Pinna lowpass for rear sources
    bool is_rear = (az_deg > 100.0f && az_deg < 260.0f);
    float pinna_lp = 1.0f;
    if (is_rear)
        pinna_lp = std::exp(-2.0f * PI * 6000.0f / (float)sample_rate);

    // Build impulse response
    std::memset(ir->h_L, 0, BA_HRTF_MAX_IR_LEN * sizeof(float));
    std::memset(ir->h_R, 0, BA_HRTF_MAX_IR_LEN * sizeof(float));

    int tap_R = 0;
    int tap_L = (itd_samps > 0) ? itd_samps : 0;
    if (itd_samps < 0) { tap_R = -itd_samps; tap_L = 0; }

    if (tap_L < ir_length) ir->h_L[tap_L] = gain_L;
    if (tap_R < ir_length) ir->h_R[tap_R] = gain_R;

    // Pinna one-pole lowpass for rear
    if (is_rear) {
        float sl = 0.0f, sr = 0.0f;
        for (int n = 0; n < ir_length; ++n) {
            ir->h_L[n] = (1.0f - pinna_lp) * ir->h_L[n] + pinna_lp * sl; sl = ir->h_L[n];
            ir->h_R[n] = (1.0f - pinna_lp) * ir->h_R[n] + pinna_lp * sr; sr = ir->h_R[n];
        }
    }

    // Elevation pinna notch (front/above)
    if (!is_rear && el_deg > 15.0f) {
        int nd = (int)((float)sample_rate / 10000.0f);
        float ndepth = 0.3f * (el_deg / 90.0f);
        if (nd < ir_length) {
            for (int n = ir_length - 1; n >= nd; --n) {
                ir->h_L[n] -= ndepth * ir->h_L[n - nd];
                ir->h_R[n] -= ndepth * ir->h_R[n - nd];
            }
        }
    }

    ir->n_partitions = 0;  // set by ba_conv_load_ir
    ir->valid = true;
}

// ==========================================================================
// 4. PARTITIONED CONVOLUTION — load IR into slot
// ==========================================================================

void ba_conv_load_ir(BaAudioContext* ctx, int slot_idx,
                     const float* ir_L, const float* ir_R, int ir_len) {
    if (slot_idx < 0 || slot_idx >= BA_MAX_CONV_SLOTS) return;
    auto& slot = ctx->conv_slots[slot_idx];

    int P = (ir_len + BA_CONV_PART_SIZE - 1) / BA_CONV_PART_SIZE;
    if (P > BA_CONV_P_MAX) P = BA_CONV_P_MAX;
    slot.P = P;
    slot.active = true;

    // FFT each partition of the IR
    for (int p = 0; p < P; ++p) {
        alignas(32) float temp[BA_CONV_FFT_SIZE] = {};
        int start = p * BA_CONV_PART_SIZE;
        int len = std::min(BA_CONV_PART_SIZE, ir_len - start);
        if (len > 0) {
            std::memcpy(temp, ir_L + start, len * sizeof(float));
        }
        pffft_transform_ordered(ctx->pffft_256, temp, slot.ir_parts_L[p],
                                ctx->pffft_scratch, PFFFT_FORWARD);

        std::memset(temp, 0, BA_CONV_FFT_SIZE * sizeof(float));
        if (len > 0) {
            std::memcpy(temp, ir_R + start, len * sizeof(float));
        }
        pffft_transform_ordered(ctx->pffft_256, temp, slot.ir_parts_R[p],
                                ctx->pffft_scratch, PFFFT_FORWARD);
    }

    // Clear FDL (frequency delay line) and history buffers
    std::memset(slot.fdl_L, 0, sizeof(slot.fdl_L));
    std::memset(slot.fdl_R, 0, sizeof(slot.fdl_R));
    std::memset(slot.input_hist_L, 0, sizeof(slot.input_hist_L));
    std::memset(slot.input_hist_R, 0, sizeof(slot.input_hist_R));
    std::memset(slot.out_accum_L, 0, sizeof(slot.out_accum_L));
    std::memset(slot.out_accum_R, 0, sizeof(slot.out_accum_R));
    slot.fdl_head = 0;
}

// ==========================================================================
// 5. PARTITIONED CONVOLUTION — process one frame
// ==========================================================================

void ba_conv_process(BaAudioContext* ctx, int slot_idx,
                     const float* in, float* out_L, float* out_R) {
    if (slot_idx < 0 || slot_idx >= BA_MAX_CONV_SLOTS) return;
    auto& slot = ctx->conv_slots[slot_idx];
    if (!slot.active || slot.P == 0) return;

    // Build analysis frame = previous history + current input
    alignas(32) float temp_in[BA_CONV_FFT_SIZE];
    std::memcpy(temp_in, slot.input_hist_L, BA_CONV_PART_SIZE * sizeof(float));
    std::memcpy(temp_in + BA_CONV_PART_SIZE, in, BA_CONV_PART_SIZE * sizeof(float));

    // Rotate history
    std::memcpy(slot.input_hist_L, in, BA_CONV_PART_SIZE * sizeof(float));

    // Forward FFT
    alignas(32) float cur_spec[BA_CONV_PACKED_BINS];
    pffft_transform_ordered(ctx->pffft_256, temp_in, cur_spec,
                            ctx->pffft_scratch, PFFFT_FORWARD);

    // Store into FDL (same spectrum for L and R since input is mono per cluster)
    std::memcpy(slot.fdl_L[slot.fdl_head], cur_spec, BA_CONV_PACKED_BINS * sizeof(float));
    std::memcpy(slot.fdl_R[slot.fdl_head], cur_spec, BA_CONV_PACKED_BINS * sizeof(float));

    // Accumulate convolution over all partitions
    alignas(32) float accum_L[BA_CONV_PACKED_BINS] = {};
    alignas(32) float accum_R[BA_CONV_PACKED_BINS] = {};
    for (int p = 0; p < slot.P; ++p) {
        int fdl_idx = (slot.fdl_head - p + BA_CONV_P_MAX) % BA_CONV_P_MAX;
        pffft_zconvolve_accumulate(ctx->pffft_256, slot.fdl_L[fdl_idx],
                                    slot.ir_parts_L[p], accum_L, 1.0f);
        pffft_zconvolve_accumulate(ctx->pffft_256, slot.fdl_R[fdl_idx],
                                    slot.ir_parts_R[p], accum_R, 1.0f);
    }

    // Inverse FFT
    alignas(32) float time_L[BA_CONV_FFT_SIZE], time_R[BA_CONV_FFT_SIZE];
    pffft_transform_ordered(ctx->pffft_256, accum_L, time_L,
                            ctx->pffft_scratch, PFFFT_BACKWARD);
    pffft_transform_ordered(ctx->pffft_256, accum_R, time_R,
                            ctx->pffft_scratch, PFFFT_BACKWARD);

    // Output = second half of convolution result (overlap-save)
    float scale = 1.0f / (float)BA_CONV_FFT_SIZE;
    for (int n = 0; n < BA_CONV_PART_SIZE; ++n) {
        out_L[n] = time_L[n + BA_CONV_PART_SIZE] * scale;
        out_R[n] = time_R[n + BA_CONV_PART_SIZE] * scale;
    }

    slot.fdl_head = (slot.fdl_head + 1) % BA_CONV_P_MAX;
}

// ==========================================================================
// 6. CROSSFADE
// ==========================================================================

void ba_start_crossfade(BaCluster* cluster, BaIRSet* new_ir) {
    cluster->prev_ir           = cluster->active_ir;
    cluster->active_ir         = new_ir;
    cluster->crossfade_remaining = 64;
    cluster->cell_id_prev      = cluster->cell_id;
}

static void apply_crossfade(BaConvSlot& slot, float* out_L, float* out_R) {
    if (slot.xfade_frames_left <= 0) return;
    float t = 1.0f - (float)slot.xfade_frames_left / (float)64;
    for (int n = 0; n < BA_CONV_PART_SIZE; ++n) {
        out_L[n] = (1.0f - t) * slot.xfade_buf_L[n] + t * out_L[n];
        out_R[n] = (1.0f - t) * slot.xfade_buf_R[n] + t * out_R[n];
    }
    slot.xfade_frames_left--;
}

// ==========================================================================
// 7. CLUSTER CONVOLUTION (per-frame, overlap-add FFT)
// ==========================================================================

void convolve_clusters(BaAudioContext* ctx, float* out_L, float* out_R, uint32_t N) {
    std::memset(out_L, 0, N * sizeof(float));
    std::memset(out_R, 0, N * sizeof(float));

    if (!ctx->pffft_setup) return;
    int fft_size = ctx->fft_size;

    for (int c = 0; c < BA_MAX_CLUSTERS; ++c) {
        auto& cl = ctx->clusters[c];
        if (cl.total_energy < 1e-10f) continue;

        // Map cluster cell to HRTF grid cell centre
        float az_deg = ((c / BA_GRID_EL_CELLS) * 10.0f - 180.0f + 5.0f);
        float el_deg = ((c % BA_GRID_EL_CELLS) * 30.0f -  90.0f + 15.0f);

        // Look up HRTF for this grid position
        const BaHRTFEntry* entry = ba_hrtf_lookup(
            ctx->hrtf_db, az_deg * DEG2RAD, el_deg * DEG2RAD);
        if (!entry || !entry->valid) continue;

        // --- Overlap-add FFT convolution (uses pre-computed freq-domain IR) ---
        // Forward FFT of input
        std::memset(ctx->fft_workspace_X, 0, fft_size * sizeof(float));
        std::memcpy(ctx->fft_workspace_X, cl.audio_sum, N * sizeof(float));
        pffft_transform_ordered(ctx->pffft_setup, ctx->fft_workspace_X,
                                ctx->fft_workspace_X, nullptr, PFFFT_FORWARD);

        auto* hrtf_db = ctx->hrtf_db;
        std::memset(ctx->fft_workspace_Y_L, 0, fft_size * sizeof(float));
        std::memset(ctx->fft_workspace_Y_R, 0, fft_size * sizeof(float));

        // Use pre-computed freq-domain IR (avoids per-frame IR FFT at init cost)
        if (hrtf_db && hrtf_db->freq_valid) {
            pffft_zconvolve_accumulate(ctx->pffft_setup, ctx->fft_workspace_X,
                                        hrtf_db->freq_L + c * BA_HRTF_FREQ_BINS,
                                        ctx->fft_workspace_Y_L, 1.0f);
            pffft_zconvolve_accumulate(ctx->pffft_setup, ctx->fft_workspace_X,
                                        hrtf_db->freq_R + c * BA_HRTF_FREQ_BINS,
                                        ctx->fft_workspace_Y_R, 1.0f);
        } else {
            // Fallback: FFT IR on the fly (tier transition / first frame)
            int cp = std::min(entry->ir_length, fft_size);
            {
                float Ir_L[1024] = {}, Ir_R[1024] = {};
                std::memcpy(Ir_L, entry->h_L, cp * sizeof(float));
                std::memcpy(Ir_R, entry->h_R, cp * sizeof(float));
                pffft_transform_ordered(ctx->pffft_setup, Ir_L, Ir_L, nullptr, PFFFT_FORWARD);
                pffft_transform_ordered(ctx->pffft_setup, Ir_R, Ir_R, nullptr, PFFFT_FORWARD);
                pffft_zconvolve_accumulate(ctx->pffft_setup, ctx->fft_workspace_X,
                                            Ir_L, ctx->fft_workspace_Y_L, 1.0f);
                pffft_zconvolve_accumulate(ctx->pffft_setup, ctx->fft_workspace_X,
                                            Ir_R, ctx->fft_workspace_Y_R, 1.0f);
            }
        }

        // Inverse FFT
        pffft_transform_ordered(ctx->pffft_setup, ctx->fft_workspace_Y_L,
                                ctx->fft_workspace_Y_L, nullptr, PFFFT_BACKWARD);
        pffft_transform_ordered(ctx->pffft_setup, ctx->fft_workspace_Y_R,
                                ctx->fft_workspace_Y_R, nullptr, PFFFT_BACKWARD);

        // Normalise, overlap-add to output, save overlap tail
        float scale = 1.0f / (float)fft_size;
        for (uint32_t n = 0; n < N; ++n) {
            out_L[n] += ctx->fft_workspace_Y_L[n] * scale + ctx->overlap_L[n];
            out_R[n] += ctx->fft_workspace_Y_R[n] * scale + ctx->overlap_R[n];
        }
        for (int n = (int)N; n < fft_size; ++n) {
            ctx->overlap_L[n - (int)N] = ctx->fft_workspace_Y_L[n] * scale;
            ctx->overlap_R[n - (int)N] = ctx->fft_workspace_Y_R[n] * scale;
        }
    }
}
