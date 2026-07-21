# Beas Audio — Project Structure & Conventions

**Status:** Initial scaffold  
**Last updated:** 2026-07-21  

---

## Folder Structure

```
D:\BEAS\Beas_Audio\
├── SRS.md              # Requirements
├── SDD.md              # Architecture & design
├── API.md              # Public API reference
├── PROJECT_STATUS.md   # This file — conventions & status
├── CMakeLists.txt      # Build system (root)
├── LICENSE             # Proprietary — All Rights Reserved
│
├── include/beas/       # Public headers (flat — one header)
│   └── beas_audio.h
│
├── src/                # All implementation, layer-organized
│   ├── internal.h      # Shared internal context
│   ├── beas_audio.cpp  # Entry point / orchestration
│   ├── phsm/           # PHSM analysis layer
│   ├── pci/            # PCI control layer
│   ├── poe/            # POE rendering layer
│   ├── masking/        # Masking model & saliency
│   ├── dsp/            # DSP primitives
│   └── util/           # Utilities (CPU, flush)
│
├── third_party/        # Vendored dependencies
│   └── pffft/          # MIT-licensed FFT
│
└── tests/              # Verification
    └── test_basic.cpp
```

**Structure choice: LAYER-BASED** — maps to pipeline data flow (PHSM → PCI → POE). Not feature-based (would disperse pipeline across folders) and not spec-based (specs are docs, not code org). Stable as the library grows.

---

## Conventions

### Naming
| Scope | Convention | Example |
|-------|-----------|---------|
| Public API | `ba_` prefix, C-style | `ba_create`, `BeasResult` |
| Source files | lowercase, no prefix | `phsm.cpp`, `pci.cpp` |
| Header guards | `#pragma once` | |
| Internal types | `BeasXxx` or `OSXxx` | `BeasAudioContext`, `OSPos` |
| Internal functions | unprefixed | `phsm_analysis()` |

### Copyright header (every source file)
```cpp
// Copyright (c) 2026 Beas Audio Engineering. All Rights Reserved.
// Proprietary — no license granted.
// Beas Audio DSP Library — <brief description>
```

### C++ Standards
- `C++17` — mandatory for `std::clamp`, `if constexpr`, structured bindings
- No STL in hot path (allowed at init/control)
- `extern "C"` for public API

### File size guidelines
- `.cpp` files: ideally <600 lines. Split if >1000.
- `.h` files: keep focused. Public API is single header.
- Each layer directory has its own `*.h` and `*.cpp`.

### Dependency direction
```
include/ → src/beas_audio.cpp → phsm/ → dsp/
                                → pci/  → dsp/
                                → poe/  → dsp/
                                → masking/
                                → util/
```
DSP primitives are leaf modules. No reverse dependencies.

---

## Build System

CMake 3.20+. Statically linked library. No install targets initially.

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

Supported generators: Ninja, MSBuild, Makefiles. Cross-compilation via toolchain files.

---

## Licensing

This is a **PROPRIETARY** library. All rights reserved. No open source license is granted.

- Every source file contains a copyright notice.
- The `LICENSE` file states "All Rights Reserved — No License Granted."
- Third-party code (pffft, MIT) is kept strictly separate under `third_party/`.
- Distribution of the compiled library or source requires a commercial agreement.

---

## Current Status

| Component | Status |
|-----------|--------|
| SRS | ✓ Done |
| SDD | ✓ Done |
| API reference | ✓ Done |
| CMakeLists.txt | Pending |
| `beas_audio.h` (public API) | Pending |
| `internal.h` | Pending |
| `beas_audio.cpp` (orchestration) | Pending |
| PHSM | Pending |
| PCI | Pending |
| POE | Pending |
| DSP primitives | Pending |
| pffft integration | Pending |
| Test | Pending |
| Build verification | Pending |

**Next step:** Create CMakeLists.txt, copy pffft, then implement source files.
