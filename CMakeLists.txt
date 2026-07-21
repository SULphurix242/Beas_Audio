# Copyright (c) 2026 Beas Audio Engineering. All Rights Reserved.
# Proprietary — no license granted.
cmake_minimum_required(VERSION 3.20)
project(beas_audio LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- pffft (MIT, static) ----
add_library(pffft STATIC third_party/pffft/pffft/pffft.c)
target_include_directories(pffft PUBLIC third_party/pffft/pffft/)
target_compile_options(pffft PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/arch:AVX2>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-mstackrealign -DPFFFT_SIMD_DISABLE>
)

# ---- Beas Audio library ----
add_library(beas_audio STATIC
    src/beas_audio.cpp
    src/phsm/phsm.cpp
    src/pci/pci.cpp
    src/poe/poe.cpp
    src/masking/masking.cpp
    src/dsp/simd.cpp
    src/dsp/eq.cpp
    src/dsp/hrtf.cpp
    src/dsp/ambient.cpp
    src/dsp/postprocess.cpp
    src/dsp/sanitize.cpp
    src/dsp/fft.cpp
    src/util/cpu.cpp
    src/util/flush.cpp
)

target_include_directories(beas_audio
    PUBLIC  include/
    PRIVATE src/
)

target_compile_options(beas_audio PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /arch:AVX2>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/O2>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -mavx2 -mfma -msse4.1 -mstackrealign -O2>
)

target_link_libraries(beas_audio PRIVATE pffft)

# ---- Test ----
add_executable(beas_audio_test tests/test_basic.cpp)
target_link_libraries(beas_audio_test PRIVATE beas_audio)
