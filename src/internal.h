// Copyright (c) 2026 Beas Audio Engineering. All Rights Reserved.
// Proprietary — no license granted.
// Beas Audio DSP Library — internal context & types
#pragma once

#include "beas/beas_audio.h"
#include <cstdint>
#include <cstring>
#include <cmath>
#include <atomic>
#include <climits>

struct PFFFT_Setup;

/* ---- Constants --------------------------------------------------------- */
constexpr int BA_LUT_STEPS         = 91;    // masking LUT: 0–180° inclusive
constexpr int BA_GRID_AZ_CELLS     = 36;    // 10° azimuth cells
constexpr int BA_GRID_EL_CELLS     = 6;     // 30° elevation cells
constexpr int BA_GRID_TOTAL        = BA_GRID_AZ_CELLS * BA_GRID_EL_CELLS;
constexpr int BA_MAX_CLUSTERS      = BA_GRID_TOTAL;
constexpr int BA_SPAWN_QUEUE_MAX   = 64;
constexpr int BA_IR_BUF_COUNT      = 2;     // double buffer
constexpr int BA_CONV_PART_SIZE    = 128;
constexpr int BA_CONV_FFT_SIZE     = 256;
constexpr int BA_CONV_P_MAX        = 15;
constexpr int BA_CONV_FFT_BINS     = 129;   // (256/2 + 1)
constexpr int BA_CONV_PACKED_BINS  = 258;   // pffft packed
constexpr int BA_MAX_CONV_SLOTS    = 32;
constexpr int BA_HRTF_MAX_IR_LEN   = 1824;  // max IR length for tier 3

constexpr int   BA_WATCHDOG_COOLDOWN = 500;
constexpr float BA_WATCHDOG_EMA      = 0.05f;
constexpr float BA_WATCHDOG_UP_THR   = 0.70f;

/* ---- SIMD capabilities ------------------------------------------------- */
struct BaSimdCaps {
    bool avx2;
    bool fma;
    bool sse41;
    bool neon;       // ponytail: ARM NEON detection
    bool scalar_only;
};

/* ---- Spatial position -------------------------------------------------- */
struct BaOSPos {
    float azimuth;    // radians
    float elevation;  // radians
    float distance;   // metres
};

/* ---- HRTF IR set ------------------------------------------------------- */
struct BaIRSet {
    float* h_L_partitions;   // [n_partitions x fft_size]
    float* h_R_partitions;
    float  h_L[BA_HRTF_MAX_IR_LEN];
    float  h_R[BA_HRTF_MAX_IR_LEN];
    int    n_partitions;
    int    ir_length;
    int    fft_size;
    float  azimuth_deg;
    float  elevation_deg;
    bool   valid;
};

/* ---- Convolution slot -------------------------------------------------- */

/* ---- Convolution slot -------------------------------------------------- */
struct BaConvSlot {
    float ir_parts_L[BA_CONV_P_MAX][BA_CONV_PACKED_BINS];
    float ir_parts_R[BA_CONV_P_MAX][BA_CONV_PACKED_BINS];
    float fdl_L[BA_CONV_P_MAX][BA_CONV_PACKED_BINS];
    float fdl_R[BA_CONV_P_MAX][BA_CONV_PACKED_BINS];
    int   fdl_head;
    float input_hist_L[BA_CONV_PART_SIZE];
    float input_hist_R[BA_CONV_PART_SIZE];
    float out_accum_L[BA_CONV_FFT_SIZE];
    float out_accum_R[BA_CONV_FFT_SIZE];
    float xfade_buf_L[BA_CONV_FFT_SIZE];
    float xfade_buf_R[BA_CONV_FFT_SIZE];
    int   xfade_frames_left;
    int   P;
    bool  active;
};

/* ---- Cluster ----------------------------------------------------------- */
struct BaCluster {
    float audio_sum[BA_MAX_BUFFER_SAMPLES];
    float total_energy;
    int   cell_id;
    int   cell_id_prev;
    int   count;
    BaIRSet* active_ir;
    BaIRSet* prev_ir;
    int   crossfade_remaining;
};

/* ---- Perceptual scene state -------------------------------------------- */
struct BaPerceptualSceneState {
    float    azimuth;
    float    elevation;
    float    diffuseness;
    float    confidence;
    float    energy[3];
    float    energy_total;
    float    centroid;
    bool     onset;
    float    azimuth_prev;
    uint32_t frame;
};

/* ---- Ambient bus ------------------------------------------------------- */
struct BaAmbientBus {
    float buf_L[BA_MAX_BUFFER_SAMPLES];
    float buf_R[BA_MAX_BUFFER_SAMPLES];
    int   count;
    float diffuseness;
    // Late reverb state (Schroeder all-pass cascade)
    float rev_delay_L[4][1024]; // 4 all-pass delay lines, 1024 max
    float rev_delay_R[4][1024];
    int   rev_ptr[4];
    float rev_gain[4];
    bool  rev_init;
};

/* ---- Function pointer types for SIMD dispatch -------------------------- */
using BaFnEnergy   = float(*)(const float* x, uint32_t N);
using BaFnDot      = float(*)(const float* a, const float* b, uint32_t N);
using BaFnPciBatch = void (*)(const float* saliency, const float* spatial,
                              const float* temporal, float* pci_out,
                              float w1, float w2_eff, float w3_eff, uint32_t N);

/* ---- Atomic controls --------------------------------------------------- */
struct BaAtomicControls {
    std::atomic<bool> flush_pending;
    std::atomic<int>  pending_path;
    std::atomic<int>  pending_tier;
    std::atomic<int>  pending_mode;
    std::atomic<int>  active_ir_idx;

    BaAtomicControls()
        : flush_pending(false), pending_path(-1), pending_tier(-1),
          pending_mode(-1), active_ir_idx(0) {}
    BaAtomicControls(const BaAtomicControls&) = delete;
    BaAtomicControls& operator=(const BaAtomicControls&) = delete;
};

/* ---- HRTF database forward declaration (full def in dsp/hrtf.h) -------- */
struct BaHRTFDatabase;
struct BaHRTFState {
    int  mode_current;
    int  mode_override;
    bool personalized_loaded;
    float coarse_ir_L[4][BA_HRTF_MAX_IR_LEN];
    float coarse_ir_R[4][BA_HRTF_MAX_IR_LEN];
    BaHRTFDatabase* personalized_db;
};

/* ---- Surround state forward declaration --------------------------------- */
struct BaSurroundState;

/* ---- Main context ------------------------------------------------------ */
struct BaAudioContext {
    /* Config */
    uint32_t  sample_rate;
    uint32_t  buffer_size;
    uint32_t  max_objects;
    float     buffer_duration;
    void*   (*alloc_fn)(size_t);
    void    (*free_fn)(void*);

    /* State */
    BaPath    path;
    BaMode    mode;
    int       tier;
    int       N_soft;
    int       N_hard;
    int       itd_tau_max;

    /* SIMD */
    BaSimdCaps  simd;
    BaFnEnergy  fn_energy;
    BaFnDot     fn_dot;
    BaFnPciBatch fn_pci_batch;

    /* PHSM */
    BaPerceptualSceneState pss;
    BaPerceptualSceneState pss_prev;
    float    cumulative_delta;
    float    last_rebuild_angle;
    int      stable_frames;
    float    dc_x_prev_L, dc_y_prev_L;
    float    dc_x_prev_R, dc_y_prev_R;

    /* PCI */
    float*   pci_prev;
    float*   pci_smooth;
    float*   saliency;
    bool*    obj_rendering;
    float*   obj_lifetime;

    /* Spatial cache */
    float*   cached_dist;
    BaOSPos* last_pos;
    void*    array_block;

    /* Masking */
    float    masking_floor[3];
    float    mask_ratio[3];
    float    masking_lut[BA_LUT_STEPS];
    float    masking_spread;

    /* PCI weights */
    float    w1, w2, w3;

    /* Clusters */
    BaCluster  clusters[BA_MAX_CLUSTERS];
    int        cluster_res_deg;

    /* IR double buffer */
    BaIRSet    ir_buf[BA_IR_BUF_COUNT];

    /* Ambient */
    BaAmbientBus ambient_bus;

    /* Spawn queue */
    uint32_t  spawn_queue[BA_SPAWN_QUEUE_MAX];
    int       spawn_queue_count;
    int       spawn_offset;

    /* Atomic controls */
    BaAtomicControls ctrl;

    /* CPU load */
    float    cpu_load;

    /* HRTF */
    BaHRTFDatabase* hrtf_db;
    BaHRTFState     hrtf_state;

    /* EQ */
    struct BaEQState* eq;

    /* Ear FIR */
    float   ear_fir_L[64];
    float   ear_fir_R[64];
    float   ear_fir_state_L[64];
    float   ear_fir_state_R[64];
    bool    ear_fir_loaded;

    /* FFT workspace */
    PFFFT_Setup* pffft_setup;
    PFFFT_Setup* pffft_256;
    int          fft_size;
    float*       fft_workspace_X;
    float*       fft_workspace_Y_L;
    float*       fft_workspace_Y_R;
    float*       overlap_L;
    float*       overlap_R;
    float*       pffft_scratch;   // workspace for out-of-place pffft transforms
    BaConvSlot   conv_slots[BA_MAX_CONV_SLOTS];

    /* CPU watchdog */
    uint64_t  frame_start_ns;     // chrono ns at frame start
    float     cpu_load_ema;
    int       tier_cooldown_frames;
    int       last_auto_tier;

    /* Surround */
    BaSurroundState* surround;

    /* Ear profiling */
    // ponytail: ear profiling session state - add when profiling is implemented
};

/* ---- Internal function declarations (from sub-modules) ----------------- */

/* phsm/ */
void phsm_analysis(BaAudioContext* ctx, const float* x_L, const float* x_R, uint32_t N);
void reset_phsm_smoothing(BaAudioContext* ctx);
void queue_ir_rebuild(BaAudioContext* ctx, float azimuth, float elevation);

/* pci/ */
void pci_two_pass(BaAudioContext* ctx, const BaOSPos* positions,
                  const BaAudioObject* objects, uint32_t N_objects);
float hash_grid_distinctness(BaAudioContext* ctx, int object_idx,
                             const BaOSPos* positions, uint32_t N_objects);
float hash_grid_nearest_any(BaAudioContext* ctx, int object_idx,
                            const BaOSPos* positions, uint32_t N_objects);
void ambient_bus_add(BaAudioContext* ctx, int object_idx,
                     const BaAudioObject* objects);

/* poe/ */
void poe_masking_pass(BaAudioContext* ctx, const BaOSPos* positions,
                      const BaAudioObject* objects, float* render_gain,
                      uint32_t N_surviving);
void poe_merge_objects(BaAudioContext* ctx, int idx_a, int idx_b,
                       const BaOSPos* positions, float* render_gain);
void poe_spawn_tick(BaAudioContext* ctx);

/* masking/ */
void masking_floor_update(BaAudioContext* ctx, const BaPerceptualSceneState& pss);
void masking_saliency_compute(BaAudioContext* ctx, const float* e_low,
                              const float* e_mid, const float* e_high,
                              uint32_t N_objects);
void masking_lut_build(BaAudioContext* ctx);
void masking_set_mode_ratios(BaAudioContext* ctx, BaMode mode);

/* dsp/simd */
BaSimdCaps detect_simd();
void simd_bind(BaAudioContext* ctx);

/* dsp/eq */
struct BaEQState;
BaEQState* eq_create(int band_count, uint32_t sample_rate, void*(alloc_fn)(size_t));
void eq_destroy(BaEQState* eq, void(free_fn)(void*));
void eq_set_band(BaEQState* eq, int band, float freq_hz, float gain_db, float q);
void eq_process(BaEQState* eq, float* out_L, float* out_R, uint32_t N);

/* dsp/fft */
void fft_conv_init(BaAudioContext* ctx);

/* dsp/hrtf */
void ba_hrtf_generate_math(BaHRTFDatabase* db, uint32_t sample_rate, int ir_length);
void convolve_clusters(BaAudioContext* ctx, float* out_L, float* out_R, uint32_t N);

/* dsp/ambient */
void ambient_bus_clear(BaAudioContext* ctx);
void ambient_bus_render(BaAudioContext* ctx, float* out_L, float* out_R, uint32_t N);

/* dsp/postprocess */
void post_process(BaAudioContext* ctx, float* out_L, float* out_R, uint32_t N);

/* dsp/sanitize */
void sanitize_input(BaAudioContext* ctx, BaAudioObject* objects,
                    float* x_L, float* x_R, uint32_t N_samples);

/* util/cpu */
uint64_t timer_ns();
void cpu_watchdog(BaAudioContext* ctx);

/* util/flush */
void check_pending_flush(BaAudioContext* ctx);
void check_pending_tier(BaAudioContext* ctx);
void check_pending_path(BaAudioContext* ctx);
void check_pending_mode(BaAudioContext* ctx);
void apply_tier_internal(BaAudioContext* ctx, int tier);

/* internal helpers shared across modules */
void cluster_objects(BaAudioContext* ctx, const BaOSPos* positions,
                     const BaAudioObject* objects, float* render_gain,
                     uint32_t N_surviving);
