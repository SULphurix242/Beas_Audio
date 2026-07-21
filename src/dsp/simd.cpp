// Copyright (c) 2026 Beas Audio Engineering. All Rights Reserved.
// Proprietary — no license granted.
// Beas Audio DSP Library — SIMD dispatch implementation
#include "simd.h"
#include <cstring>

BaSimdCaps detect_simd() {
    BaSimdCaps caps{};
    caps.scalar_only = true;

#if defined(__x86_64__) || defined(_M_X64)
    int info[4];
#if defined(_MSC_VER)
    __cpuid(info, 1);
    caps.sse41 = (info[2] >> 19) & 1;
    caps.fma   = (info[2] >> 12) & 1;
    __cpuidex(info, 7, 0);
    caps.avx2  = (info[1] >> 5) & 1;
#else
    __cpuid(1, info[0], info[1], info[2], info[3]);
    caps.sse41 = (info[2] >> 19) & 1;
    caps.fma   = (info[2] >> 12) & 1;
    __cpuid_count(7, 0, info[0], info[1], info[2], info[3]);
    caps.avx2  = (info[1] >> 5) & 1;
#endif
    caps.scalar_only = !caps.sse41;
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
    // ponytail: NEON assumed available on ARM64; runtime check via getauxval on Linux if needed
    caps.neon = true;
    caps.scalar_only = false;
#endif

    return caps;
}

float hsum128(__m128 v) {
    __m128 hi = _mm_movehl_ps(v, v);
    __m128 sum = _mm_add_ps(v, hi);
    __m128 shuf = _mm_shuffle_ps(sum, sum, _MM_SHUFFLE(1, 0, 3, 2));
    __m128 res = _mm_add_ss(sum, shuf);
    return _mm_cvtss_f32(res);
}

float hsum256(__m256 v) {
    __m128 hi = _mm256_extractf128_ps(v, 1);
    __m128 lo = _mm256_castps256_ps128(v);
    __m128 sum = _mm_add_ps(lo, hi);
    return hsum128(sum);
}

// ---- Scalar ----
float energy_scalar(const float* x, uint32_t N) {
    float r = 0.0f;
    for (uint32_t i = 0; i < N; ++i) r += x[i] * x[i];
    return r;
}

float dot_scalar(const float* a, const float* b, uint32_t N) {
    float r = 0.0f;
    for (uint32_t i = 0; i < N; ++i) r += a[i] * b[i];
    return r;
}

void pci_batch_scalar(const float* sal, const float* spa, const float* tmp,
                      float* out, float w1, float w2, float w3, uint32_t N) {
    for (uint32_t i = 0; i < N; ++i)
        out[i] = sal[i] * (1.0f + w2 * spa[i] + w3 * tmp[i]);
}

// ---- SSE4.1 ----
float energy_sse41(const float* x, uint32_t N) {
    __m128 acc = _mm_setzero_ps();
    uint32_t i = 0;
    for (; i + 4 <= N; i += 4)
        acc = _mm_add_ps(acc, _mm_mul_ps(_mm_loadu_ps(x + i), _mm_loadu_ps(x + i)));
    float r = hsum128(acc);
    for (; i < N; ++i) r += x[i] * x[i];
    return r;
}

float dot_sse41(const float* a, const float* b, uint32_t N) {
    __m128 acc = _mm_setzero_ps();
    uint32_t i = 0;
    for (; i + 4 <= N; i += 4)
        acc = _mm_add_ps(acc, _mm_mul_ps(_mm_loadu_ps(a + i), _mm_loadu_ps(b + i)));
    float r = hsum128(acc);
    for (; i < N; ++i) r += a[i] * b[i];
    return r;
}

void pci_batch_sse41(const float* sal, const float* spa, const float* tmp,
                     float* out, float w1, float w2, float w3, uint32_t N) {
    __m128 v_w2 = _mm_set1_ps(w2);
    __m128 v_w3 = _mm_set1_ps(w3);
    __m128 v_one = _mm_set1_ps(1.0f);
    uint32_t i = 0;
    for (; i + 4 <= N; i += 4) {
        __m128 s = _mm_loadu_ps(sal + i);
        __m128 sum = _mm_add_ps(v_one, _mm_add_ps(_mm_mul_ps(v_w2, _mm_loadu_ps(spa + i)),
                                                   _mm_mul_ps(v_w3, _mm_loadu_ps(tmp + i))));
        _mm_storeu_ps(out + i, _mm_mul_ps(s, sum));
    }
    for (; i < N; ++i) out[i] = sal[i] * (1.0f + w2 * spa[i] + w3 * tmp[i]);
}

// ---- AVX2 ----
float energy_avx2(const float* x, uint32_t N) {
    __m256 acc = _mm256_setzero_ps();
    uint32_t i = 0;
    for (; i + 8 <= N; i += 8)
        acc = _mm256_fmadd_ps(_mm256_loadu_ps(x + i), _mm256_loadu_ps(x + i), acc);
    float r = hsum256(acc);
    for (; i < N; ++i) r += x[i] * x[i];
    return r;
}

float dot_avx2(const float* a, const float* b, uint32_t N) {
    __m256 acc = _mm256_setzero_ps();
    uint32_t i = 0;
    for (; i + 8 <= N; i += 8)
        acc = _mm256_fmadd_ps(_mm256_loadu_ps(a + i), _mm256_loadu_ps(b + i), acc);
    float r = hsum256(acc);
    for (; i < N; ++i) r += a[i] * b[i];
    return r;
}

void pci_batch_avx2(const float* sal, const float* spa, const float* tmp,
                    float* out, float w1, float w2, float w3, uint32_t N) {
    __m256 v_w2 = _mm256_set1_ps(w2);
    __m256 v_w3 = _mm256_set1_ps(w3);
    __m256 v_one = _mm256_set1_ps(1.0f);
    uint32_t i = 0;
    for (; i + 8 <= N; i += 8) {
        __m256 s = _mm256_loadu_ps(sal + i);
        __m256 t = _mm256_fmadd_ps(v_w3, _mm256_loadu_ps(tmp + i),
                    _mm256_fmadd_ps(v_w2, _mm256_loadu_ps(spa + i), v_one));
        _mm256_storeu_ps(out + i, _mm256_mul_ps(s, t));
    }
    for (; i < N; ++i) out[i] = sal[i] * (1.0f + w2 * spa[i] + w3 * tmp[i]);
}

void simd_bind(BaAudioContext* ctx) {
    ctx->simd = detect_simd();
    if (ctx->simd.neon) {
        // ponytail: NEON paths — add when hand-tuned NEON implementations exist
        ctx->fn_energy = energy_scalar;
        ctx->fn_dot = dot_scalar;
        ctx->fn_pci_batch = pci_batch_scalar;
    } else if (ctx->simd.avx2 && ctx->simd.fma) {
        ctx->fn_energy = energy_avx2;
        ctx->fn_dot = dot_avx2;
        ctx->fn_pci_batch = pci_batch_avx2;
    } else if (ctx->simd.sse41) {
        ctx->fn_energy = energy_sse41;
        ctx->fn_dot = dot_sse41;
        ctx->fn_pci_batch = pci_batch_sse41;
    } else {
        ctx->fn_energy = energy_scalar;
        ctx->fn_dot = dot_scalar;
        ctx->fn_pci_batch = pci_batch_scalar;
    }
}
