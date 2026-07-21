# Beas Audio DSP Library — Software Requirements Specification

**Version:** 1.0  
**Status:** Draft  
**Confidential — Proprietary to Beas Audio Engineering**

---

## 1. Introduction

### 1.1 Purpose
Beas Audio is a platform-independent C/C++ DSP library providing a complete spatial audio processing pipeline based on the PHSM → PCI → POE architecture. It delivers high-quality binaural rendering, perceptual scene analysis, and adaptive quality management for real-time audio applications.

### 1.2 Scope
The library provides a pure C API (with internal C++ implementation) that processes multi-object audio scenes through a three-stage pipeline:
- **PHSM** — Perceptual Hardware State Manager: real-time acoustic scene analysis
- **PCI** — Platform Control Interface: intelligent object prioritization and control
- **POE** — Platform Output Engine: high-fidelity binaural rendering

Target platforms: Windows, macOS, Linux, Android, iOS (any platform with a C++17 compiler).

### 1.3 Not Open Source
This is a **proprietary, closed-source** library. All source files carry copyright notices. No open source license is granted. Distribution is under commercial terms only.

---

## 2. Functional Requirements

### FR1 — Core Processing Pipeline
| ID | Requirement | Priority |
|----|-------------|----------|
| FR1.1 | Accept up to 64 simultaneous audio objects per scene | High |
| FR1.2 | Process configurable buffer sizes (32–256 samples) | High |
| FR1.3 | Support 44100, 48000, 96000 Hz sample rates | High |
| FR1.4 | Output stereo interleaved or deinterleaved float buffers | High |
| FR1.5 | Frame-level processing with deterministic latency | Critical |

### FR2 — PHSM Scene Analysis
| ID | Requirement | Priority |
|----|-------------|----------|
| FR2.1 | Estimate ITD (Inter-aural Time Difference) from stereo input | High |
| FR2.2 | Estimate ILD (Inter-aural Level Difference) across 3 bands | High |
| FR2.3 | Compute per-band Inter-channel Coherence (IC) | High |
| FR2.4 | Compute spectral centroid estimation | Medium |
| FR2.5 | Detect onset events with dual-resolution window | Medium |
| FR2.6 | Compute directional confidence from multi-band fusion | High |
| FR2.7 | Detect diffuseness vs directional dominance | High |
| FR2.8 | Auto-trigger HRTF IR rebuild on cumulative angle delta | Medium |

### FR3 — PCI Object Control
| ID | Requirement | Priority |
|----|-------------|----------|
| FR3.1 | Compute per-object saliency from energy and masking | Critical |
| FR3.2 | Apply hysteresis-based rendering state (enter/exit thresholds) | High |
| FR3.3 | Compute spatial distinctness via hash-grid neighbor analysis | High |
| FR3.4 | Apply temporal weighting from object lifetime | Medium |
| FR3.5 | Modulate PCI weights by PSS confidence and diffuseness | High |
| FR3.6 | Produce sorted priority list via tiebreaker sort | High |
| FR3.7 | Route non-rendered objects to ambient bus | Medium |

### FR4 — POE Binaural Rendering
| ID | Requirement | Priority |
|----|-------------|----------|
| FR4.1 | HRTF convolution via frequency-domain partitioned convolution | Critical |
| FR4.2 | Masking-aware gain reduction with configurable soft/hard limits | High |
| FR4.3 | Energy-weighted object merging for hard-limit cluster reduction | High |
| FR4.4 | Deferred spawn activation (queued activation per frame) | Medium |
| FR4.5 | Ambient bus accumulation and diffuse rendering | Medium |
| FR4.6 | Post-processing: EQ, gain staging, optional limiting | Low |

### FR5 — Adaptive Quality (CPU Watchdog)
| ID | Requirement | Priority |
|----|-------------|----------|
| FR5.1 | Measure per-frame CPU load via rdtsc or portable clock | High |
| FR5.2 | EMA-smooth CPU load measurement | High |
| FR5.3 | Auto-downgrade tier when load exceeds thresholds | Medium |
| FR5.4 | Auto-upgrade tier when load falls below recovery threshold | Medium |
| FR5.5 | Cooldown interval between tier changes | Low |

### FR6 — Modes and Configuration
| ID | Requirement | Priority |
|----|-------------|----------|
| FR6.1 | 10 preset audio modes (Generic, Action, Adventure, Racing, etc.) | High |
| FR6.2 | 4 quality tiers (0=economy through 3=ultra) | High |
| FR6.3 | Explicit-objects and legacy-stereo processing paths | High |
| FR6.4 | HRTF database hot-reload | Medium |
| FR6.5 | Ear FIR profile loading | Low |
| FR6.6 | Multi-band parametric EQ | Medium |
| FR6.7 | Surround-to-binaural virtualization | Low |

---

## 3. Non-Functional Requirements

| ID | Requirement | Target |
|----|-------------|--------|
| NFR1 | Maximum per-frame latency | <1ms at 48kHz/128 frames |
| NFR2 | Maximum CPU budget per frame | 80% of frame time at tier 3 |
| NFR3 | Memory allocation | Zero dynamic alloc after init |
| NFR4 | Thread safety | Context is single-threaded; no internal locks |
| NFR5 | SIMD acceleration | SSE4.1, AVX2, FMA where available |
| NFR6 | Portability | C++17, no platform-specific APIs in DSP core |
| NFR7 | Object size limit | 64 objects max |
| NFR8 | Buffer size limit | 32–256 samples |
| NFR9 | HRTF IR length | 32–2048 taps depending on tier |

---

## 4. Constraints

- C++17 language standard (public API is C-compatible with `extern "C"`)
- Single-header third-party dependency: pffft (MIT, used as static compiled library)
- No STL dependencies in hot path; STL allowed for init/control only
- All per-frame arrays must be pre-allocated at init time
- Proprietary — every source file must carry copyright and "All Rights Reserved"

---

## 5. External Interfaces

| Interface | Description |
|-----------|-------------|
| C API (`beas_audio.h`) | Primary public API — create/destroy/process/control |
| C++ internal API | Internal pipeline implementation, not exposed |
| HRTF SOFA files | External HRTF measurement database loaded at init |
| Ear FIR profiles | Custom head-related filter files |
