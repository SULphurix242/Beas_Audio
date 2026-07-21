# Beas Audio DSP Library — Software Design Document

**Version:** 1.0  
**Status:** Draft  
**Confidential — Proprietary to Beas Audio Engineering**

---

## 1. Architecture Overview

```
┌────────────────────────────────────────────────────────────┐
│                     beas_audio.h  (C API)                   │
│  create / destroy / process / set_mode / set_tier / ...   │
└──────────────────────┬─────────────────────────────────────┘
                       │
┌──────────────────────▼─────────────────────────────────────┐
│                  beas_audio.cpp  (Orchestration)            │
│  sanitize → PHSM → energies → masking → PCI → POE → out   │
└──┬──────────┬──────────┬──────────┬──────────┬─────────────┘
   │          │          │          │          │
   ▼          ▼          ▼          ▼          ▼
┌──────┐  ┌──────┐  ┌──────┐  ┌──────┐  ┌──────────┐
│ PHSM │  │ DSP  │  │MASKING│  │ PCI  │  │   POE    │
│Scene │  │Prims │  │Floor │  │Control│  │ Render   │
│Analy │  │SIMD  │  │Sali- │  │ 2-pass│  │ Masking  │
│sis   │  │FFT   │  │ency  │  │ Hyst  │  │ Merge    │
│      │  │EQ    │  │      │  │ Sort  │  │ Spawn    │
└──────┘  └──────┘  └──────┘  └──────┘  └──────────┘
                                          │
                                          ▼
                                    ┌──────────┐
                                    │ Convolve  │
                                    │ (HRTF)    │
                                    │ + Ambient │
                                    │ + Post    │
                                    └──────────┘
```

### 1.1 Pipeline Stages

**PHSM** reads input audio frames and extracts perceptual metadata: direction, diffuseness, confidence, onset, energy distribution. This metadata feeds PCI weight modulation and IR rebuild decisions.

**PCI** takes object metadata (position, energy) plus PHSM output and produces a sorted priority list. Objects below saliency thresholds are diverted to ambient bus. The remainder are sorted by a fused metric of saliency, spatial distinctness, and temporal weight.

**POE** takes the sorted list, applies masking-aware gain reduction, merges objects when hard limits are exceeded, then convolves each surviving object through HRTF filters. The ambient bus is rendered diffusely and summed.

### 1.2 Quality Tiers (4 levels)

| Tier | IR Length | HRTF Mode | Convolution | Cluster Res | ITD |
|------|-----------|-----------|-------------|-------------|-----|
| 0 (Economy) | 32 | Generic | None | 10° | Off |
| 1 (Balanced) | 288 | Clustered | Partitioned (P=3) | 10° | On |
| 2 (High) | 800 | Full | Partitioned (P=7) | 5° | On |
| 3 (Ultra) | 1824 | Full+Personalized | Partitioned (P=15) | 5° | On |

---

## 2. Folder Structure (Layer-Based)

```
D:\BEAS\Beas_Audio\
├── SRS.md                        # Software Requirements Specification
├── SDD.md                        # Software Design Document (this file)
├── API.md                        # Public API reference
├── PROJECT_STATUS.md             # Project structure conventions & status
├── CMakeLists.txt                # Root build file
├── LICENSE                       # Proprietary license (All Rights Reserved)
│
├── include/
│   └── beas/
│       └── beas_audio.h          # Public C API — the only exposed header
│
├── src/
│   ├── internal.h                # BeasAudioContext + internal types
│   ├── beas_audio.cpp            # API implementation + pipeline orchestration
│   │
│   ├── phsm/                     # PHSM — Perceptual Scene Analysis
│   │   ├── phsm.h
│   │   └── phsm.cpp              # ITD/ILD/IC estimation, confidence fusion
│   │
│   ├── pci/                      # PCI — Object Control & Prioritization
│   │   ├── pci.h
│   │   └── pci.cpp               # Saliency hysteresis, spatial hash, PCI sort
│   │
│   ├── poe/                      # POE — Binaural Rendering Engine
│   │   ├── poe.h
│   │   └── poe.cpp               # Masking pass, merge, spawn, convolution
│   │
│   ├── masking/                  # Masking model & saliency
│   │   ├── masking.h
│   │   └── masking.cpp           # Masking floor, LUT, saliency per object
│   │
│   ├── dsp/                      # DSP primitives (SIMD/FFT/EQ/HRTF)
│   │   ├── simd.h / simd.cpp     # SIMD dispatch + vector math
│   │   ├── eq.h / eq.cpp         # Multi-band parametric EQ
│   │   ├── hrtf.h / hrtf.cpp     # HRTF database + convolution dispatch
│   │   ├── fft.h / fft.cpp       # FFT wrapper around pffft
│   │   ├── ambient.h / ambient.cpp      # Ambient bus accumulation
│   │   ├── postprocess.h / postprocess.cpp  # Post-processing
│   │   └── sanitize.h / sanitize.cpp    # Input validation + NaN guards
│   │
│   └── util/                     # Utilities
│       ├── cpu.h / cpu.cpp       # CPU feature detection + watchdog timer
│       └── flush.h / flush.cpp   # State reset logic
│
├── third_party/
│   └── pffft/                    # PFFFT library (MIT, compiled as static lib)
│       └── pffft/
│           ├── pffft.c
│           └── pffft.h
│
└── tests/
    └── test_basic.cpp            # Single self-check test
```

### 2.1 Why Layer-Based

Each directory maps to a pipeline stage, not a feature. This matches the data flow: audio comes in → PHSM analyzes → PCI decides → POE renders. New features are new functions within existing layers, not new folders. The structure is stable as the library grows.

---

## 3. Data Flow (Per-Frame Processing)

### 3.1 Processing Sequence
```
beas_process(ctx, objects, N, xL, xR, outL, outR):
  1. check_pending_flush/tier/path/mode     -- atomic control sync
  2. sanitize_input(objects, xL, xR)         -- NaN/Inf guard
  3. ambient_bus_clear()                     -- reset accumulator
  4. if (path == LEGACY_STEREO):
       phsm_analysis(xL, xR)                 -- perceptual analysis
  5. compute_object_energies(objects)         -- per-object energy bands
  6. masking_floor_update(pss)               -- update masking floor
  7. masking_saliency_compute(energies)       -- per-object saliency
  8. pci_two_pass(positions, objects)         -- hysteresis + priority
  9. poe_masking_pass(sorted_objects)         -- gain reduction
  10. poe_spawn_tick()                        -- deferred activation
  11. cluster_objects()                       -- hard-limit merge
  12. convolve_clusters(out)                  -- HRTF convolution
  13. ambient_bus_render(out)                 -- diffuse accumulation
  14. post_process(out)                       -- EQ, gain staging
  15. cpu_watchdog()                          -- adaptive quality
```

### 3.2 Key Data Structures

**BeasAudioContext** (internal, opaque to user):
- Config: sample_rate, buffer_size, max_objects, alloc/free
- Pipeline state: PSS, PCI vectors, masking data, clusters
- IR double buffer, convolution slots, FFT workspace
- CPU watchdog state, atomic controls

**BeasAudioObject** (public):
```c
typedef struct {
    uint32_t id;          // unique object identifier
    float    azimuth;     // radians
    float    elevation;   // radians
    float    distance;    // meters
    float    gain;        // linear gain
    float*   pcm;         // pointer to sample buffer
} BeasAudioObject;
```

**OSPos** (internal):
```cpp
struct OSPos {
    float azimuth;    // radians
    float elevation;  // radians
    float distance;   // meters
};
```

**OSPerceptualSceneState** (internal):
```cpp
struct OSPerceptualSceneState {
    float azimuth, elevation, diffuseness, confidence;
    float energy[3];        // LOW / MID / HIGH bands
    float energy_total, centroid;
    bool  onset;
    float azimuth_prev;
    uint32_t frame;
};
```

---

## 4. Module Details

### 4.1 PHSM — Perceptual Hardware State Manager

**Input:** `x_L[0..N-1]`, `x_R[0..N-1]` (stereo frame)  
**Output:** Fills `ctx->pss` with direction, diffuseness, confidence, energies

Algorithm:
1. Dual-resolution window (onset detection via short/long energy ratio)
2. Per-band energy (3 bands: LOW <500Hz, MID 500Hz-4kHz, HIGH >4kHz)
3. ILD estimation per band via RMS ratio → dB
4. Spectral centroid (energy-weighted frequency estimate)
5. ITD via GCC-PHAT with azimuth-aware skip (avoid ITD at near-center sources)
6. Per-band IC with multi-source suppression via IC variance
7. Per-band confidence fusion → overall confidence + azimuth
8. Diffuseness = 1 - confidence
9. Cumulative angle tracking → IR rebuild trigger

**SIMD hooks:** energy, dot-product, batch operations via function pointers

### 4.2 PCI — Platform Control Interface

**Input:** object positions, saliency array, PSS from PHSM  
**Output:** `pci_prev` array (priority scores), `obj_rendering` bool array

Algorithm:
1. PCI oracle smoothing (EMA of previous PCI values)
2. Saliency hysteresis: obj_rendering[i] persists between SAL_EXIT and SAL_ENTER
3. Non-rendering objects → ambient bus
4. Pass 1: spatial distinctness via 3×3 hash-grid neighborhood analysis
5. Pass 1: temporal weighting from object lifetime
6. Pass 2: PSS-modulated PCI weights (w1/w2/w3 modulated by confidence/diffuseness)
7. Tiebreaker sort: primary key = PCI score, secondary = object index

### 4.3 POE — Platform Output Engine

**Input:** sorted object list with render gains  
**Output:** rendered stereo buffer

Sub-stages:
1. **Masking pass:** cascading gain reduction when count exceeds N_soft. Uses angular masking LUT. Objects with zero render gain → ambient bus.
2. **Hard-limit cluster merge:** if count > N_hard, energy-weighted position merge of nearest pairs.
3. **Spawn tick:** activate up to 16 queued spawns per frame.
4. **HRTF convolution:** frequency-domain partitioned convolution (pffft-based). Per-object IR lookup + overlap-add.
5. **Ambient bus:** all non-rendered objects sum → diffuse stereo output.
6. **Post-process:** EQ, limiting, gain staging.

### 4.4 SIMD Dispatch

At init, CPUID check selects best available SIMD path:
- `simd_bind(ctx)` sets function pointers for `fn_energy`, `fn_dot`, `fn_pci_batch`
- Fallback: scalar path (always available)
- Scalar path doubles as the reference implementation for testing

---

## 5. Adaptive Quality (CPU Watchdog)

Measured per frame via rdtsc (or `std::chrono::high_resolution_clock` on ARM).

| Load Range | Action |
|------------|--------|
| >90% | Emergency tier downgrade |
| >85% | Tier downgrade |
| >80% | Reduce N_soft by 25% |
| >75% | Disable ITD |
| >70% | Increase cluster resolution to 10° |
| <70% & tier < last_auto | Upgrade tier |

All actions have cooldown intervals (500 frames for tier changes) to prevent oscillation.

---

## 6. Memory Model

- **Zero dynamic allocation in hot path.** All per-frame arrays pre-allocated at `ba_create()`.
- Single allocation block for per-object arrays (positions, saliency, PCI, lifetimes, etc.).
- HRTF database allocated at init (loaded from SOFA file).
- Convolution slots use pre-allocated workspace; no allocation per object per frame.
- User provides `alloc`/`free` at init (default to `malloc`/`free`).

---

## 7. Platform Portability Audit

### 7.1 Platform-Specific Surfaces

| Surface | Usage | Portability | Fix Applied |
|---------|-------|-------------|-------------|
| `__rdtsc()` | CPU watchdog timer | x86 only | Replaced with `std::chrono::high_resolution_clock` |
| `__cpuid` / `__cpuidex` | SIMD feature detection | x86 only | Guarded by `_MSC_VER`/`__GNUC__`; ARM NEON via `__ARM_NEON` |
| `<immintrin.h>` | SIMD intrinsics | x86 AVX/SSE | Conditional include; ARM path uses `<arm_neon.h>` |
| `pffft_aligned_malloc` | FFT aligned alloc | Portable | pffft wraps per-platform alloc internally |
| `fopen`/`fclose` | HRTF file loading | Standard C — everywhere | No change needed |
| `malloc`/`free` | General alloc | Standard C — everywhere | User-provided allocator at init |

### 7.2 Tested Target Platforms

| Platform | Arch | Status |
|----------|------|--------|
| Windows (MSVC) | x86-64 | Primary dev target |
| Windows (MinGW) | x86-64 | CI verified |
| Linux (GCC/Clang) | x86-64, ARM64 | Build verified |
| macOS (Apple Clang) | x86-64, ARM64 | Build verified (Apple Silicon requires chrono timer) |
| Android (NDK) | ARM64 | Requires NEON dispatch |
| iOS | ARM64 | Requires NEON dispatch |

### 7.3 Platform Independence Strategy

All platform-specific code is isolated to two files:
- `src/dsp/simd.h/.cpp` — SIMD dispatch (CPUID for x86, compile-time for ARM)
- `src/util/cpu.h/.cpp` — Timer (chrono as portable default, rdtsc as optional x86 fast-path)

Everything else is pure C++17 with zero platform dependencies.

---

## 8. Thread Safety

- **Single `BeasAudioContext` is NOT thread-safe.** One context = one processing thread.
- `beas_flush()`, `beas_set_mode()`, `beas_set_tier()` use atomics for deferred control from other threads.
- For multi-threaded use, create separate contexts per processing thread.

---

## 8. Third-Party Dependencies

| Library | Purpose | License | Notes |
|---------|---------|---------|-------|
| pffft | FFT (partitioned convolution) | MIT | Compiled as static lib; no GPL copyleft |

MIT-licensed code is safe for proprietary use (no obligation to open-source the consuming library).

---

## 9. Proprietary Protection

- Every source file begins with a copyright block: `Copyright (c) 2026 Beas Audio Engineering. All Rights Reserved.`
- No LICENSE file grants any open source rights — the file states "All Rights Reserved. Proprietary — no license granted."
- Export control: source code may be obfuscated for distribution builds in a future phase.
- The public API is a thin C wrapper; all DSP IP lives in internal `src/` files.
