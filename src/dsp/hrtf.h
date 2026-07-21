// Copyright (c) 2026 Beas Audio Engineering. All Rights Reserved.
// Proprietary — no license granted.
// Beas Audio DSP Library — HRTF database & FFT overlap-add convolution
#pragma once
#include "internal.h"

/* Single HRTF entry (one grid cell) */
struct BaHRTFEntry {
    float    h_L[BA_HRTF_MAX_IR_LEN];
    float    h_R[BA_HRTF_MAX_IR_LEN];
    float    azimuth_deg;
    float    elevation_deg;
    int      ir_length;
    bool     valid;
};

/* ---- HRTF database — 216-cell grid + pre-computed freq-domain IRs ------- */
// Freq-domain buffers allocated dynamically to avoid ld segfault on large BSS
constexpr int BA_HRTF_FREQ_BINS = 1026;  // packed: 2*(max_fft/2+1)
struct BaHRTFDatabase {
    // Time-domain IRs — heap-allocated (avoids oversized .bss that crashes GNU ld 2.45)
    BaHRTFEntry* cells;   // [BA_GRID_TOTAL], alloc by ba_hrtf_generate_math
    // Pre-computed freq-domain IRs — heap-allocated
    float* freq_L;        // [BA_GRID_TOTAL * BA_HRTF_FREQ_BINS]
    float* freq_R;        // same
    int   ir_length;
    uint32_t sample_rate;
    bool  loaded;
    bool  freq_valid;
};

/* Functions */
void ba_hrtf_generate_math(BaHRTFDatabase* db, uint32_t sample_rate, int ir_length,
                           struct PFFFT_Setup* fft_setup, int fft_size);
const BaHRTFEntry* ba_hrtf_lookup(const BaHRTFDatabase* db,
                                   float azimuth_rad, float elevation_rad);
void ba_build_ir(BaIRSet* ir, float azimuth_deg, float elevation_deg,
                 uint32_t sample_rate, int ir_length);
void ba_conv_load_ir(BaAudioContext* ctx, int slot_idx,
                     const float* ir_L, const float* ir_R, int ir_len);
void ba_conv_process(BaAudioContext* ctx, int slot_idx,
                     const float* in, float* out_L, float* out_R);
void ba_start_crossfade(BaCluster* cluster, BaIRSet* new_ir);
void convolve_clusters(BaAudioContext* ctx, float* out_L, float* out_R, uint32_t N);
