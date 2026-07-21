// Copyright (c) 2026 Beas Audio Engineering. All Rights Reserved.
// Proprietary — no license granted.
// Beas Audio DSP Library — public C API
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Constants --------------------------------------------------------- */
#define BA_MAX_BUFFER_SAMPLES  256
#define BA_MAX_OBJECTS         64
#define BA_MAX_EQ_BANDS        16

/* ---- Result codes ------------------------------------------------------ */
typedef enum {
    BA_OK               =  0,
    BA_ERR_INVALID_ARG  = -1,
    BA_ERR_NO_MEMORY    = -2,
    BA_ERR_CPUID_FAIL   = -3,
    BA_ERR_NOT_INIT     = -4
} BaResult;

/* ---- Processing path --------------------------------------------------- */
typedef enum {
    BA_PATH_EXPLICIT_OBJECTS = 0,
    BA_PATH_LEGACY_STEREO    = 1
} BaPath;

/* ---- Audio mode presets ------------------------------------------------ */
typedef enum {
    BA_MODE_GENERIC,
    BA_MODE_ACTION,
    BA_MODE_ADVENTURE,
    BA_MODE_RACING,
    BA_MODE_IMMERSIVE,
    BA_MODE_MMORPG,
    BA_MODE_STRATEGY,
    BA_MODE_SPEECH,
    BA_MODE_MUSIC,
    BA_MODE_CINEMATIC
} BaMode;

/* ---- Room materials ---------------------------------------------------- */
typedef enum {
    BA_MAT_DEFAULT       = 0,
    BA_MAT_CONCRETE      = 1,
    BA_MAT_WOOD_PANEL    = 2,
    BA_MAT_CARPET        = 3,
    BA_MAT_GLASS         = 4,
    BA_MAT_ACOUSTIC_TILE = 5,
    BA_MAT_PLASTER       = 6
} BaRoomMaterial;

/* ---- HRTF mode --------------------------------------------------------- */
typedef enum {
    BA_HRTF_MODE_GENERIC      = 0,
    BA_HRTF_MODE_CLUSTERED    = 1,
    BA_HRTF_MODE_FULL         = 2,
    BA_HRTF_MODE_PERSONALIZED = 3
} BaHRTFMode;

/* ---- Audio object ------------------------------------------------------ */
typedef struct {
    uint32_t    id;
    float       azimuth;       // radians
    float       elevation;     // radians
    float       distance;      // meters
    float       gain;          // linear
    const float* pcm;          // [buffer_size] samples
} BaAudioObject;

/* ---- Initialisation config --------------------------------------------- */
typedef struct {
    uint32_t    sample_rate;     // 44100, 48000, or 96000
    uint32_t    buffer_size;     // 32–256 samples
    BaPath      path;
    uint32_t    max_objects;     // 1–64
    int         tier;            // 0–3
    void*     (*alloc)(size_t);  // NULL = malloc
    void      (*free)(void*);    // NULL = free
    const char* hrtf_path;       // SOFA file path, NULL = built-in
    const char* ear_fir_path;    // optional ear profile, NULL = none
    uint32_t    eq_band_count;   // 0 = no EQ
} BaConfig;

/* ---- Opaque context ---------------------------------------------------- */
typedef struct BaAudioContext BaAudioContext;

/* ---- Lifecycle --------------------------------------------------------- */
BaResult ba_create(const BaConfig* cfg, BaAudioContext** ctx_out);
void     ba_destroy(BaAudioContext* ctx);

/* ---- Main processing --------------------------------------------------- */
BaResult ba_process(
    BaAudioContext*       ctx,
    const BaAudioObject*  objects,      // [N_objects]
    uint32_t              N_objects,
    const float*          x_L,          // legacy stereo input L
    const float*          x_R,          // legacy stereo input R
    float*                out_L,        // output L [N_samples]
    float*                out_R,        // output R [N_samples]
    uint32_t              N_samples);

/* ---- Control (deferred via atomics) ------------------------------------ */
void     ba_flush(BaAudioContext* ctx);
BaResult ba_set_mode(BaAudioContext* ctx, BaMode mode);
BaResult ba_set_tier(BaAudioContext* ctx, int tier);
/* ---- EQ ----------------------------------------------------------------- */
typedef enum {
    BA_EQ_PEAKING    = 0,
    BA_EQ_LOW_SHELF  = 1,
    BA_EQ_HIGH_SHELF = 2,
    BA_EQ_LOW_PASS   = 3,
    BA_EQ_HIGH_PASS  = 4,
    BA_EQ_NOTCH      = 5,
    BA_EQ_BANDPASS   = 6,
    BA_EQ_ALLPASS    = 7,
} BaEQFilter;

BaResult ba_set_eq_band(BaAudioContext* ctx, uint32_t band,
                         float freq_hz, float gain_db, float q);
BaResult ba_set_eq_band_ex(BaAudioContext* ctx, uint32_t band,
                            BaEQFilter filter, float freq_hz,
                            float gain_db, float q);

/* ---- HRTF / Ear FIR hot-reload ----------------------------------------- */
BaResult ba_load_hrtf(BaAudioContext* ctx, const char* path);
BaResult ba_load_ear_fir(BaAudioContext* ctx, const char* path);

/* ---- Query -------------------------------------------------------------- */
uint32_t ba_get_latency_samples(const BaAudioContext* ctx);
float    ba_get_latency_ms(const BaAudioContext* ctx);
int      ba_get_current_tier(const BaAudioContext* ctx);
float    ba_get_cpu_load(const BaAudioContext* ctx);

/* ---- Room --------------------------------------------------------------- */
void ba_set_room_materials(BaAudioContext* ctx,
                           BaRoomMaterial floor,
                           BaRoomMaterial ceiling,
                           BaRoomMaterial wall);

/* ---- HRTF mode ---------------------------------------------------------- */
void      ba_set_hrtf_mode(BaAudioContext* ctx, BaHRTFMode mode);
BaHRTFMode ba_get_hrtf_mode(const BaAudioContext* ctx);

/* ---- Surround-to-binaural ----------------------------------------------- */
BaResult ba_process_surround(BaAudioContext* ctx,
                             const float** channel_buffers,
                             int n_channels,
                             float* out_L, float* out_R,
                             uint32_t N_samples);

#ifdef __cplusplus
}
#endif
