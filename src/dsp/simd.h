// Copyright (c) 2026 Beas Audio Engineering. All Rights Reserved.
// Proprietary — no license granted.
// Beas Audio DSP Library — SIMD dispatch (x86 SSE4.1/AVX2 + ARM NEON)
#pragma once
#include "internal.h"

// x86 intrinsics
#include <immintrin.h>
#if defined(_MSC_VER)
#   include <intrin.h>
#else
#   include <cpuid.h>
#endif

// ARM NEON — defined by the compiler when building for ARM targets
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#   include <arm_neon.h>
#endif

BaSimdCaps detect_simd();

float energy_scalar(const float* x, uint32_t N);
float energy_sse41(const float* x, uint32_t N);
float energy_avx2(const float* x, uint32_t N);

float dot_scalar(const float* a, const float* b, uint32_t N);
float dot_sse41(const float* a, const float* b, uint32_t N);
float dot_avx2(const float* a, const float* b, uint32_t N);

void pci_batch_scalar(const float* sal, const float* spa, const float* tmp,
                      float* out, float w1, float w2, float w3, uint32_t N);
void pci_batch_sse41(const float* sal, const float* spa, const float* tmp,
                     float* out, float w1, float w2, float w3, uint32_t N);
void pci_batch_avx2(const float* sal, const float* spa, const float* tmp,
                    float* out, float w1, float w2, float w3, uint32_t N);

float hsum128(__m128 v);
float hsum256(__m256 v);

void simd_bind(BaAudioContext* ctx);
