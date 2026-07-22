// Copyright (c) 2026 Beas Audio Engineering. All Rights Reserved.
// Proprietary — no license granted.
// Beas Audio DSP Library — API entry points & pipeline orchestration
#include "internal.h"
#include "phsm/phsm.h"
#include "pci/pci.h"
#include "poe/poe.h"
#include "masking/masking.h"
#include "dsp/simd.h"
#include "dsp/fft.h"
#include "dsp/hrtf.h"
#include "dsp/ambient.h"
#include "dsp/postprocess.h"
#include "dsp/sanitize.h"
#include "dsp/eq.h"
#include "util/cpu.h"
#include "util/flush.h"
#include "pffft.h"
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <new>
#include <algorithm>

// ---- Mode weights --------------------------------------------------------

static void set_mode_weights(BaAudioContext* ctx, BaMode mode) {
    switch (mode) {
    case BA_MODE_GENERIC:   ctx->w1=0.5f; ctx->w2=0.3f; ctx->w3=0.2f; break;
    case BA_MODE_ACTION:    ctx->w1=0.7f; ctx->w2=0.2f; ctx->w3=0.1f; break;
    case BA_MODE_ADVENTURE: ctx->w1=0.5f; ctx->w2=0.3f; ctx->w3=0.2f; break;
    case BA_MODE_RACING:    ctx->w1=0.6f; ctx->w2=0.3f; ctx->w3=0.1f; break;
    case BA_MODE_IMMERSIVE: ctx->w1=0.4f; ctx->w2=0.4f; ctx->w3=0.2f; break;
    case BA_MODE_MMORPG:    ctx->w1=0.4f; ctx->w2=0.2f; ctx->w3=0.4f; break;
    case BA_MODE_STRATEGY:  ctx->w1=0.5f; ctx->w2=0.4f; ctx->w3=0.1f; break;
    case BA_MODE_SPEECH:    ctx->w1=0.3f; ctx->w2=0.2f; ctx->w3=0.5f; break;
    case BA_MODE_MUSIC:     ctx->w1=0.3f; ctx->w2=0.4f; ctx->w3=0.3f; break;
    case BA_MODE_CINEMATIC: ctx->w1=0.4f; ctx->w2=0.3f; ctx->w3=0.3f; break;
    default:                ctx->w1=0.5f; ctx->w2=0.3f; ctx->w3=0.2f; break;
    }
}

// ---- Cluster helper -----------------------------------------------------
// ponytail: hash-grid cluster accumulation — shared between poe and main process

static int hash_cell_cluster(float azimuth, float elevation, int /*res_deg*/) {
    static constexpr float DEG2RAD = 3.14159265f / 180.0f;
    float az_d = azimuth / DEG2RAD;
    float el_d = elevation / DEG2RAD;
    int ca = (int)((az_d + 180.0f) / 10.0f) % BA_GRID_AZ_CELLS;
    if (ca < 0) ca += BA_GRID_AZ_CELLS;
    int ce = (int)((el_d + 90.0f) / 30.0f);
    ce = std::max(0, std::min(BA_GRID_EL_CELLS - 1, ce));
    return ca * BA_GRID_EL_CELLS + ce;
}

void cluster_objects(BaAudioContext* ctx, const BaOSPos* positions,
                     const BaAudioObject* objects, float* render_gain,
                     uint32_t N_surviving) {
    // Reset clusters
    for (int c = 0; c < BA_MAX_CLUSTERS; ++c) {
        ctx->clusters[c].count = 0;
        ctx->clusters[c].total_energy = 0.0f;
        std::memset(ctx->clusters[c].audio_sum, 0, ctx->buffer_size * sizeof(float));
    }

    for (uint32_t i = 0; i < N_surviving; ++i) {
        if (render_gain[i] == 0.0f) continue;
        int cell = hash_cell_cluster(positions[i].azimuth, positions[i].elevation, ctx->cluster_res_deg);
        auto& cl = ctx->clusters[cell];
        float g = render_gain[i] * objects[i].gain;
        for (uint32_t n = 0; n < ctx->buffer_size; ++n)
            cl.audio_sum[n] += objects[i].pcm[n] * g;
        cl.total_energy += g * g;
        cl.count++;
        cl.cell_id = cell;
    }
}

// ---- Lifecycle -----------------------------------------------------------

BaResult ba_create(const BaConfig* cfg, BaAudioContext** ctx_out) {
    if (!cfg || !ctx_out) return BA_ERR_INVALID_ARG;
    if (cfg->buffer_size == 0 || cfg->buffer_size > BA_MAX_BUFFER_SAMPLES)
        return BA_ERR_INVALID_ARG;
    if (cfg->max_objects == 0 || cfg->max_objects > BA_MAX_OBJECTS)
        return BA_ERR_INVALID_ARG;
    if (cfg->sample_rate != 44100 && cfg->sample_rate != 48000 && cfg->sample_rate != 96000)
        return BA_ERR_INVALID_ARG;
    if (cfg->tier < 0 || cfg->tier > 3)
        return BA_ERR_INVALID_ARG;

    auto alloc_fn = cfg->alloc ? cfg->alloc : ::malloc;
    auto free_fn  = cfg->free ? cfg->free : ::free;

    void* mem = alloc_fn(sizeof(BaAudioContext));
    if (!mem) return BA_ERR_NO_MEMORY;

    BaAudioContext* ctx = new(mem) BaAudioContext{};
    ctx->sample_rate    = cfg->sample_rate;
    ctx->buffer_size    = cfg->buffer_size;
    ctx->max_objects    = cfg->max_objects;
    ctx->buffer_duration = (float)cfg->buffer_size / (float)cfg->sample_rate;
    ctx->alloc_fn = alloc_fn;
    ctx->free_fn  = free_fn;
    ctx->path     = cfg->path;
    ctx->mode     = BA_MODE_GENERIC;
    ctx->tier     = cfg->tier;

    // Pre-allocate per-object arrays in one block
    size_t n = cfg->max_objects;
    size_t total = n * sizeof(float)   // pci_prev
                 + n * sizeof(float)   // pci_smooth
                 + n * sizeof(float)   // saliency
                 + n * sizeof(bool)    // obj_rendering
                 + n * sizeof(float)   // obj_lifetime
                 + n * sizeof(float)   // cached_dist
                 + n * sizeof(BaOSPos);// last_pos
    void* block = alloc_fn(total);
    if (!block) { ctx->~BaAudioContext(); free_fn(mem); return BA_ERR_NO_MEMORY; }

    uint8_t* p = (uint8_t*)block;
    ctx->pci_prev     = (float*)p;   p += n * sizeof(float);
    ctx->pci_smooth   = (float*)p;   p += n * sizeof(float);
    ctx->saliency     = (float*)p;   p += n * sizeof(float);
    ctx->obj_rendering = (bool*)p;   p += n * sizeof(bool);
    ctx->obj_lifetime = (float*)p;   p += n * sizeof(float);
    ctx->cached_dist  = (float*)p;   p += n * sizeof(float);
    ctx->last_pos     = (BaOSPos*)p;
    std::memset(block, 0, total);
    ctx->array_block = block;

    // Init subsystems
    simd_bind(ctx);
    apply_tier_internal(ctx, cfg->tier);
    set_mode_weights(ctx, BA_MODE_GENERIC);
    masking_set_mode_ratios(ctx, BA_MODE_GENERIC);
    masking_lut_build(ctx);

    // FFT init
    fft_conv_init(ctx);
    if (!ctx->pffft_setup || !ctx->pffft_256) {
        ctx->free_fn(ctx->array_block);
        ctx->~BaAudioContext();
        ctx->free_fn(ctx);
        return BA_ERR_NO_MEMORY;
    }

// HRTF database — load from SOFA file or generate math model
{
    int ir_len = (cfg->tier == 0) ? 32 : (cfg->tier == 1) ? 288 :
                 (cfg->tier == 2) ? 800 : 1824;
    void* db_mem = alloc_fn(sizeof(BaHRTFDatabase));
    if (!db_mem) {
        ctx->free_fn(ctx->array_block);
        ctx->~BaAudioContext();
        ctx->free_fn(ctx);
        return BA_ERR_NO_MEMORY;
    }
    ctx->hrtf_db = new(db_mem) BaHRTFDatabase{};

    if (cfg->hrtf_path) {
        ba_hrtf_generate_math(ctx->hrtf_db, cfg->sample_rate, ir_len,
                              ctx->pffft_setup, ctx->fft_size);
        // Upgrade to SOFA data — replace math model with measured HRTF
        int load_r = ba_hrtf_load_sofa(ctx, cfg->hrtf_path);
        if (load_r != 0) {
            // SOFA load failed; keep the math-based model as fallback
        }
    } else {
        ba_hrtf_generate_math(ctx->hrtf_db, cfg->sample_rate, ir_len,
                              ctx->pffft_setup, ctx->fft_size);
    }
}
    ctx->hrtf_state.mode_current = BA_HRTF_MODE_FULL;
    ctx->hrtf_state.mode_override = (BaHRTFMode)-1;
    ctx->hrtf_state.personalized_loaded = false;

    // EQ
    if (cfg->eq_band_count > 0)
        ctx->eq = eq_create((int)cfg->eq_band_count, cfg->sample_rate, alloc_fn);

    // Ear FIR state
    std::memset(ctx->ear_fir_L, 0, sizeof(ctx->ear_fir_L));
    std::memset(ctx->ear_fir_R, 0, sizeof(ctx->ear_fir_R));
    std::memset(ctx->ear_fir_state_L, 0, sizeof(ctx->ear_fir_state_L));
    std::memset(ctx->ear_fir_state_R, 0, sizeof(ctx->ear_fir_state_R));
    ctx->ear_fir_loaded = false;

    ctx->pss.confidence = 1.0f;
    ctx->pss_prev.confidence = 1.0f;

    // Surround state zeroed by BaAudioContext{} init

    *ctx_out = ctx;
    return BA_OK;
}

void ba_destroy(BaAudioContext* ctx) {
    if (!ctx) return;
    auto free_fn = ctx->free_fn;

    if (ctx->pffft_setup)  pffft_destroy_setup(ctx->pffft_setup);
    if (ctx->pffft_256)    pffft_destroy_setup(ctx->pffft_256);
    if (ctx->fft_workspace_X)  pffft_aligned_free(ctx->fft_workspace_X);
    if (ctx->fft_workspace_Y_L) pffft_aligned_free(ctx->fft_workspace_Y_L);
    if (ctx->fft_workspace_Y_R) pffft_aligned_free(ctx->fft_workspace_Y_R);
    if (ctx->overlap_L)         pffft_aligned_free(ctx->overlap_L);
    if (ctx->overlap_R)         pffft_aligned_free(ctx->overlap_R);
    if (ctx->pffft_scratch)     pffft_aligned_free(ctx->pffft_scratch);

    if (ctx->hrtf_db) {
        if (ctx->hrtf_db->cells)  pffft_aligned_free(ctx->hrtf_db->cells);
        if (ctx->hrtf_db->freq_L) pffft_aligned_free(ctx->hrtf_db->freq_L);
        if (ctx->hrtf_db->freq_R) pffft_aligned_free(ctx->hrtf_db->freq_R);
        ctx->hrtf_db->~BaHRTFDatabase();
        free_fn(ctx->hrtf_db);
    }
    if (ctx->eq)      eq_destroy(ctx->eq, free_fn);
    if (ctx->array_block) free_fn(ctx->array_block);

    ctx->~BaAudioContext();
    free_fn(ctx);
}

// ---- Main processing -----------------------------------------------------

BaResult ba_process(BaAudioContext* ctx, const BaAudioObject* objects,
                    uint32_t N_objects, const float* x_L, const float* x_R,
                    float* out_L, float* out_R, uint32_t N_samples) {
    if (!ctx || !out_L || !out_R) return BA_ERR_INVALID_ARG;
    if (N_samples != ctx->buffer_size) return BA_ERR_INVALID_ARG;

    ctx->frame_start_ns = timer_ns();

    // Deferred control checks
    check_pending_flush(ctx);
    check_pending_tier(ctx);
    check_pending_path(ctx);
    check_pending_mode(ctx);

    if (N_objects == 0 && ctx->path == BA_PATH_EXPLICIT_OBJECTS) {
        std::memset(out_L, 0, N_samples * sizeof(float));
        std::memset(out_R, 0, N_samples * sizeof(float));
        return BA_OK;
    }

    // Sanitize
    sanitize_input(ctx, const_cast<BaAudioObject*>(objects),
                   const_cast<float*>(x_L), const_cast<float*>(x_R),
                   N_samples, N_objects);

    ambient_bus_clear(ctx);

    // PHSM — perceptual analysis (legacy stereo path only)
    if (ctx->path == BA_PATH_LEGACY_STEREO) {
        phsm_analysis(ctx, x_L, x_R, N_samples);
    } else {
        ctx->pss.confidence = 1.0f;
        ctx->pss.diffuseness = 0.0f;
    }

    // Build positions & energies
    BaOSPos positions[BA_MAX_OBJECTS] = {};
    float e_low[BA_MAX_OBJECTS] = {}, e_mid[BA_MAX_OBJECTS] = {}, e_high[BA_MAX_OBJECTS] = {};

    if (ctx->path == BA_PATH_EXPLICIT_OBJECTS) {
        for (uint32_t i = 0; i < N_objects; ++i) {
            positions[i].azimuth   = objects[i].azimuth;
            positions[i].elevation = objects[i].elevation;
            positions[i].distance  = objects[i].distance;
        }
    }

    for (uint32_t i = 0; i < N_objects; ++i) {
        float e = ctx->fn_energy(objects[i].pcm, N_samples);
        e_low[i]  = e * 0.3f;
        e_mid[i]  = e * 0.5f;
        e_high[i] = e * 0.2f;
    }

    // Masking + PCI
    masking_floor_update(ctx, ctx->pss);
    masking_saliency_compute(ctx, e_low, e_mid, e_high, N_objects);
    pci_two_pass(ctx, positions, objects, N_objects);

    // Count survivors
    uint32_t N_surviving = 0;
    for (uint32_t i = 0; i < N_objects; ++i)
        if (ctx->obj_rendering[i]) N_surviving++;

    // POE — masking pass + spawn + cluster + convolve
    float render_gain[BA_MAX_OBJECTS] = {};
    for (uint32_t i = 0; i < N_objects; ++i)
        render_gain[i] = ctx->obj_rendering[i] ? objects[i].gain : 0.0f;

    poe_masking_pass(ctx, positions, objects, render_gain, N_surviving);
    poe_spawn_tick(ctx);
    cluster_objects(ctx, positions, objects, render_gain, N_surviving);
    convolve_clusters(ctx, out_L, out_R, N_samples);
    ambient_bus_render(ctx, out_L, out_R, N_samples);
    post_process(ctx, out_L, out_R, N_samples);

    // CPU watchdog
    uint64_t end_ns = timer_ns();
    float frame_ns = (float)(end_ns - ctx->frame_start_ns);
    float budget_ns = (float)ctx->buffer_size / (float)ctx->sample_rate * 1e9f;
    float load = frame_ns / budget_ns;

    ctx->cpu_load_ema = (1.0f - BA_WATCHDOG_EMA) * ctx->cpu_load_ema
                      + BA_WATCHDOG_EMA * load;
    ctx->cpu_load = ctx->cpu_load_ema;
    cpu_watchdog(ctx);

    return BA_OK;
}

// ---- Control API ---------------------------------------------------------

void ba_flush(BaAudioContext* ctx) {
    if (!ctx) return;
    ctx->ctrl.flush_pending.store(true, std::memory_order_release);
}

BaResult ba_set_mode(BaAudioContext* ctx, BaMode mode) {
    if (!ctx) return BA_ERR_INVALID_ARG;
    ctx->ctrl.pending_mode.store((int)mode, std::memory_order_release);
    return BA_OK;
}

BaResult ba_set_tier(BaAudioContext* ctx, int tier) {
    if (!ctx) return BA_ERR_INVALID_ARG;
    if (tier < 0 || tier > 3) return BA_ERR_INVALID_ARG;
    ctx->ctrl.pending_tier.store(tier, std::memory_order_release);
    return BA_OK;
}

// ---- EQ ------------------------------------------------------------------

BaResult ba_set_eq_band(BaAudioContext* ctx, uint32_t band,
                         float freq_hz, float gain_db, float q) {
    return ba_set_eq_band_ex(ctx, band, BA_EQ_PEAKING, freq_hz, gain_db, q);
}

BaResult ba_set_eq_band_ex(BaAudioContext* ctx, uint32_t band,
                            BaEQFilter filter, float freq_hz,
                            float gain_db, float q) {
    if (!ctx) return BA_ERR_INVALID_ARG;
    if (!ctx->eq) return BA_ERR_NOT_INIT;
    eq_set_band(ctx->eq, (int)band, (int)filter, freq_hz, gain_db, q);
    return BA_OK;
}

// ---- HRTF / Ear FIR ------------------------------------------------------

BaResult ba_load_hrtf(BaAudioContext* ctx, const char* path) {
    if (!ctx || !path) return BA_ERR_INVALID_ARG;
    int r = ba_hrtf_load_sofa(ctx, path);
    return (r == 0) ? BA_OK : (r == -2) ? BA_ERR_NO_MEMORY : BA_ERR_FILE_IO;
}

BaResult ba_load_ear_fir(BaAudioContext* ctx, const char* path) {
    if (!ctx) return BA_ERR_INVALID_ARG;
    // ponytail: ear FIR loading — implement when ear profiling is integrated
    return BA_OK;
}

// ---- Query ---------------------------------------------------------------

uint32_t ba_get_latency_samples(const BaAudioContext* ctx) {
    return ctx ? ctx->buffer_size : 0;
}

float ba_get_latency_ms(const BaAudioContext* ctx) {
    if (!ctx) return 0.0f;
    return (float)ctx->buffer_size / (float)ctx->sample_rate * 1000.0f;
}

int ba_get_current_tier(const BaAudioContext* ctx) {
    return ctx ? ctx->tier : -1;
}

float ba_get_cpu_load(const BaAudioContext* ctx) {
    return ctx ? ctx->cpu_load : 0.0f;
}

// ---- Room -----------------------------------------------------------------

void ba_set_room_materials(BaAudioContext* ctx, BaRoomMaterial /*floor*/,
                            BaRoomMaterial /*ceiling*/, BaRoomMaterial /*wall*/) {
    if (!ctx) return;
    // ponytail: room acoustics — implement when reverb engine is integrated
}

// ---- HRTF mode -----------------------------------------------------------

void ba_set_hrtf_mode(BaAudioContext* ctx, BaHRTFMode mode) {
    if (!ctx) return;
    ctx->hrtf_state.mode_override = (int)mode;
}

BaHRTFMode ba_get_hrtf_mode(const BaAudioContext* ctx) {
    if (!ctx) return BA_HRTF_MODE_GENERIC;
    if (ctx->hrtf_state.mode_override >= 0)
        return (BaHRTFMode)ctx->hrtf_state.mode_override;
    return (BaHRTFMode)ctx->hrtf_state.mode_current;
}

// ---- Surround-to-binaural -------------------------------------------------

// Standard speaker positions (azimuth, elevation in radians)
// ITU-R BS.775 / AES standard layouts
struct SpkPos { float az; float el; };

static constexpr float SPK_D2R = 3.14159265f / 180.0f;

static const SpkPos kSpkStereo[] = {
    { 30.0f * SPK_D2R, 0.0f },   // L
    {-30.0f * SPK_D2R, 0.0f },   // R
};
static const SpkPos kSpk51[] = {
    { 30.0f * SPK_D2R, 0.0f },   // L
    {-30.0f * SPK_D2R, 0.0f },   // R
    { 0.0f,             0.0f },   // C
    { 0.0f,             0.0f },   // LFE (no direction)
    {110.0f * SPK_D2R, 0.0f },   // Ls
    {-110.0f * SPK_D2R,0.0f },   // Rs
};
static const SpkPos kSpk71[] = {
    { 30.0f * SPK_D2R, 0.0f },   // L
    {-30.0f * SPK_D2R, 0.0f },   // R
    { 0.0f,             0.0f },   // C
    { 0.0f,             0.0f },   // LFE
    { 90.0f * SPK_D2R, 0.0f },   // Ls
    {-90.0f * SPK_D2R, 0.0f },   // Rs
    {150.0f * SPK_D2R, 0.0f },   // Lrs
    {-150.0f* SPK_D2R, 0.0f },   // Rrs
};
static const SpkPos kSpk514[] = {
    { 30.0f * SPK_D2R, 0.0f },          // L
    {-30.0f * SPK_D2R, 0.0f },          // R
    { 0.0f,             0.0f },          // C
    { 0.0f,             0.0f },          // LFE
    {110.0f * SPK_D2R, 0.0f },          // Ls
    {-110.0f * SPK_D2R,0.0f },          // Rs
    { 45.0f * SPK_D2R, 45.0f * SPK_D2R },  // TFL
    {-45.0f * SPK_D2R, 45.0f * SPK_D2R },  // TFR
    {135.0f * SPK_D2R, 45.0f * SPK_D2R },  // TRL
    {-135.0f* SPK_D2R, 45.0f * SPK_D2R },  // TRR
};
static const SpkPos kSpk714[] = {
    { 30.0f * SPK_D2R, 0.0f },          // L
    {-30.0f * SPK_D2R, 0.0f },          // R
    { 0.0f,             0.0f },          // C
    { 0.0f,             0.0f },          // LFE
    { 90.0f * SPK_D2R, 0.0f },          // Ls
    {-90.0f * SPK_D2R, 0.0f },          // Rs
    {150.0f * SPK_D2R, 0.0f },          // Lrs
    {-150.0f* SPK_D2R, 0.0f },          // Rrs
    { 45.0f * SPK_D2R, 45.0f * SPK_D2R },  // TFL
    {-45.0f * SPK_D2R, 45.0f * SPK_D2R },  // TFR
    {135.0f * SPK_D2R, 45.0f * SPK_D2R },  // TRL
    {-135.0f* SPK_D2R, 45.0f * SPK_D2R },  // TRR
};

// Indexed by n_channels-2; nullptr for unsupported channel counts
static const SpkPos* const kSpeakerMap[] = {
    kSpkStereo,    // 2 ch → 2.0
    nullptr,       // 3
    nullptr,       // 4
    nullptr,       // 5
    kSpk51,        // 6 → 5.1
    nullptr,       // 7
    kSpk71,        // 8 → 7.1
    nullptr,       // 9
    kSpk514,       // 10 → 5.1.4
    nullptr,       // 11
    kSpk714,       // 12 → 7.1.4
};

static const SpkPos* lookup_speaker_positions(BaAudioContext* ctx, int nch) {
    // Custom layout takes priority
    if (ctx->surround.custom_nch > 0 && ctx->surround.custom_nch == nch)
        return (const SpkPos*)ctx->surround.custom_az; // SpkPos layout matches float[2]
    // Fall back to standard table
    int idx = nch - 2;
    if (idx < 0 || idx >= (int)(sizeof(kSpeakerMap)/sizeof(kSpeakerMap[0])))
        return nullptr;
    return kSpeakerMap[idx];
}

BaResult ba_surround_set_layout(BaAudioContext* ctx,
                                 const BaSpeakerPos* positions,
                                 int n_channels) {
    if (!ctx) return BA_ERR_INVALID_ARG;
    if (!positions) {
        ctx->surround.custom_nch = 0; // reset to table
        return BA_OK;
    }
    if (n_channels < 2 || n_channels > 12) return BA_ERR_INVALID_ARG;
    for (int i = 0; i < n_channels; ++i) {
        ctx->surround.custom_az[i] = positions[i].azimuth;
        ctx->surround.custom_el[i] = positions[i].elevation;
    }
    ctx->surround.custom_nch = n_channels;
    return BA_OK;
}

static void surround_render(BaAudioContext* ctx, const SpkPos* pos,
                             const float** ch, int nch,
                             float* out_L, float* out_R, uint32_t N) {
    BaOSPos positions[12];
    BaAudioObject objects[12];
    float render_gain[12];
    for (int i = 0; i < nch; ++i) {
        positions[i] = {pos[i].az, pos[i].el, 1.0f};
        objects[i]   = {(uint32_t)i, pos[i].az, pos[i].el, 1.0f, 1.0f, ch[i]};
        render_gain[i] = 1.0f;
    }
    std::memset(out_L, 0, N * sizeof(float));
    std::memset(out_R, 0, N * sizeof(float));
    ambient_bus_clear(ctx);
    cluster_objects(ctx, positions, objects, render_gain, (uint32_t)nch);
    convolve_clusters(ctx, out_L, out_R, N);
    ambient_bus_render(ctx, out_L, out_R, N);
    post_process(ctx, out_L, out_R, N);
}

BaResult ba_process_surround(BaAudioContext* ctx, const float** ch,
                              int nch, float* out_L, float* out_R,
                              uint32_t N) {
    if (!ctx || !ch || !out_L || !out_R) return BA_ERR_INVALID_ARG;
    if (N != ctx->buffer_size) return BA_ERR_INVALID_ARG;
    ctx->frame_start_ns = timer_ns();

    const SpkPos* pos = lookup_speaker_positions(ctx, nch);
    if (!pos) return BA_ERR_INVALID_ARG;

    surround_render(ctx, pos, ch, nch, out_L, out_R, N);

    // CPU load tracking
    uint64_t end_ns = timer_ns();
    float frame_ns = (float)(end_ns - ctx->frame_start_ns);
    float budget_ns = (float)N / (float)ctx->sample_rate * 1e9f;
    float load = frame_ns / budget_ns;
    ctx->cpu_load_ema = (1.0f - BA_WATCHDOG_EMA) * ctx->cpu_load_ema
                      + BA_WATCHDOG_EMA * load;
    ctx->cpu_load = ctx->cpu_load_ema;
    return BA_OK;
}
