# Beas Audio DSP Library — Public C API Reference

**Confidential — Proprietary**

---

## Types

### `BeasResult`
```c
typedef enum {
    BA_OK               = 0,
    BA_ERR_INVALID_ARG  = -1,
    BA_ERR_NO_MEMORY    = -2,
    BA_ERR_CPUID_FAIL   = -3,
    BA_ERR_NOT_INIT     = -4
} BeasResult;
```

### `BeasPath`, `BeasMode`, `BeasRoomMaterial`, `BeasHRTFMode`
Mirrors the enums from the engine: path (explicit-objects/legacy-stereo), mode (9 audio presets), room materials, HRTF mode selection.

### `BeasAudioObject`
```c
typedef struct {
    uint32_t id;
    float    azimuth;      // radians
    float    elevation;    // radians
    float    distance;     // meters
    float    gain;         // linear
    const float* pcm;      // [buffer_size] samples
} BeasAudioObject;
```

### `BeasConfig`
```c
typedef struct {
    uint32_t      sample_rate;
    uint32_t      buffer_size;
    BeasPath      path;
    uint32_t      max_objects;
    int           tier;          // 0-3
    void*       (*alloc)(size_t);
    void        (*free)(void*);
    const char*   hrtf_path;     // SOFA file path, NULL = built-in
    const char*   ear_fir_path;  // optional ear profile
    uint32_t      eq_band_count; // 0 = no EQ
} BeasConfig;
```

---

## Functions

### `ba_create` / `ba_destroy`
```c
BeasResult ba_create(const BeasConfig* cfg, BeasAudioContext** ctx_out);
void       ba_destroy(BeasAudioContext* ctx);
```

### `ba_process`
```c
BeasResult ba_process(
    BeasAudioContext*       ctx,
    const BeasAudioObject*  objects,   // [N_objects]
    uint32_t                N_objects,
    const float*            x_L,       // legacy stereo input L
    const float*            x_R,       // legacy stereo input R
    float*                  out_L,     // output L [N_samples]
    float*                  out_R,     // output R [N_samples]
    uint32_t                N_samples);
```

### Controls (all deferred via atomics)
```c
void       ba_flush(BeasAudioContext* ctx);
BeasResult ba_set_mode(BeasAudioContext* ctx, BeasMode mode);
BeasResult ba_set_tier(BeasAudioContext* ctx, int tier);

BeasResult ba_set_eq_band(BeasAudioContext* ctx, uint32_t band,
                          float freq_hz, float gain_db, float q);
BeasResult ba_load_hrtf(BeasAudioContext* ctx, const char* path);
BeasResult ba_load_ear_fir(BeasAudioContext* ctx, const char* path);
```

### Queries
```c
uint32_t ba_get_latency_samples(const BeasAudioContext* ctx);
float    ba_get_latency_ms(const BeasAudioContext* ctx);
int      ba_get_current_tier(const BeasAudioContext* ctx);
float    ba_get_cpu_load(const BeasAudioContext* ctx);
```

### Room & HRTF
```c
void      ba_set_room_materials(BeasAudioContext* ctx,
                                BeasRoomMaterial floor,
                                BeasRoomMaterial ceiling,
                                BeasRoomMaterial wall);
void      ba_set_hrtf_mode(BeasAudioContext* ctx, BeasHRTFMode mode);
BeasHRTFMode ba_get_hrtf_mode(const BeasAudioContext* ctx);

// Surround-to-binaural virtualization
BeasResult ba_process_surround(BeasAudioContext* ctx,
                               const float** channel_buffers,
                               int n_channels,
                               float* out_L, float* out_R,
                               uint32_t N_samples);
```

### Ear Profiling (future)
```c
// Placeholder — full ear profiling API to be documented in v1.1
```

---

## Linking

Static library: `libbeas_audio.a` (MSVC: `beas_audio.lib`)  
Single header include: `#include <beas/beas_audio.h>`

Dependencies (static-linked): pffft

No external runtime dependencies beyond libc/libc++.
