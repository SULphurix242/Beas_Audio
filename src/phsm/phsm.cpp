// Copyright (c) 2026 Beas Audio Engineering. All Rights Reserved.
// Proprietary — no license granted.
// Beas Audio DSP Library — PHSM: perceptual scene analysis (ITD/ILD/IC)
#include "phsm.h"
#include <cmath>
#include <algorithm>

static constexpr float DEG2RAD = 3.14159265f / 180.0f;
static constexpr float K1 = 1.0f;   // ITD weight
static constexpr float K2 = 1.0f;   // ILD weight

static float variance3(float a, float b, float c) {
    float m = (a + b + c) / 3.0f;
    return ((a-m)*(a-m) + (b-m)*(b-m) + (c-m)*(c-m)) / 3.0f;
}

static float clampf(float x, float lo, float hi) {
    return std::max(lo, std::min(hi, x));
}

static float gcc_phat(const float* L, const float* R, uint32_t N, int tau_max) {
    float max_r = -1e10f;
    int best = 0;
    for (int tau = -tau_max; tau <= tau_max; ++tau) {
        float r = 0.0f;
        for (uint32_t n = 0; n < N; ++n) {
            int idx = (int)n + tau;
            if (idx >= 0 && idx < (int)N) r += L[n] * R[idx];
        }
        if (r > max_r) { max_r = r; best = tau; }
    }
    return (float)best;
}

static float energy_band(const float* x, uint32_t N, int band) {
    float e = 0.0f;
    if (band == 0) {                // LOW
        for (uint32_t n = 0; n < N; ++n) e += x[n] * x[n];
    } else if (band == 1) {         // MID
        for (uint32_t n = 0; n < N; n += 2) e += x[n] * x[n];
    } else {                         // HIGH
        for (uint32_t n = 1; n < N; ++n) { float d = x[n] - x[n-1]; e += d * d; }
    }
    return e;
}

void reset_phsm_smoothing(BaAudioContext* ctx) {
    ctx->pss = BaPerceptualSceneState{};
    ctx->pss_prev = BaPerceptualSceneState{};
    ctx->pss.confidence = 1.0f;
    ctx->pss_prev.confidence = 1.0f;
    ctx->cumulative_delta = 0.0f;
    ctx->last_rebuild_angle = 0.0f;
    ctx->stable_frames = 0;
}

void queue_ir_rebuild(BaAudioContext* ctx, float azimuth, float /*elevation*/) {
    ctx->last_rebuild_angle = azimuth;
    ctx->cumulative_delta = 0.0f;
}

void phsm_analysis(BaAudioContext* ctx, const float* x_L, const float* x_R, uint32_t N) {
    // Step 1 — Dual-resolution onset detection
    float e_short = 0.0f, e_long = 0.0f;
    for (uint32_t n = 0; n < N; ++n) {
        float sq = x_L[n]*x_L[n] + x_R[n]*x_R[n];
        e_long += sq;
        if (n < 32) e_short += sq;
    }
    bool onset = (e_short / (e_long + 1e-10f)) > 10.0f;
    uint32_t win = onset ? 32 : N;

    // Step 2 — Per-band energy
    float EL[3], ER[3];
    for (int b = 0; b < 3; ++b) { EL[b] = energy_band(x_L, win, b); ER[b] = energy_band(x_R, win, b); }
    float ELt = EL[0]+EL[1]+EL[2], ERt = ER[0]+ER[1]+ER[2];

    // Step 3 — ILD
    float AL = std::sqrt(ELt / win + 1e-10f);
    float AR = std::sqrt(ERt / win + 1e-10f);
    float ILD = 20.0f * std::log10(AL / AR);

    // Step 4 — Spectral centroid
    float centroid = (300.0f*EL[0] + 1000.0f*EL[1] + 6000.0f*EL[2]) / (EL[0]+EL[1]+EL[2] + 1e-10f);

    // Step 5 — ITD with azimuth-aware skip
    float itd_w = clampf((centroid - 300.0f) / 500.0f, 0.3f, 1.0f);
    float az_f = std::cos(std::fabs(ctx->pss_prev.azimuth));
    float skip_db = 3.0f + (1.0f - az_f) * 6.0f;
    float ITD = 0.0f;
    if (std::fabs(ILD) < skip_db && ctx->itd_tau_max > 0) {
        ITD = gcc_phat(x_L, x_R, win, ctx->itd_tau_max);
    }

    // Step 6 — Per-band IC
    float IC[3];
    for (int b = 0; b < 3; ++b) {
        float R = ctx->fn_dot(x_L, x_R, win);
        IC[b] = R / (std::sqrt(EL[b] * ER[b]) + 1e-10f);
    }
    float ic_var = variance3(IC[0], IC[1], IC[2]);
    float ms_penalty = std::max(0.0f, 1.0f - ic_var * 4.0f);

    // Step 7 — Band confidence
    float max_e = std::max({EL[0]+ER[0], EL[1]+ER[1], EL[2]+ER[2]});
    float conf_band[3];
    for (int b = 0; b < 3; ++b)
        conf_band[b] = std::max(0.0f, IC[b]) * std::sqrt((EL[b]+ER[b]) / (max_e + 1e-10f));

    // Step 8 — Per-band direction
    float theta[3];
    for (int b = 0; b < 3; ++b) {
        float itd_b = ITD * (b==0?1.0f:b==1?0.8f:0.5f);
        float ild_b = ILD * (b==0?0.5f:b==1?1.0f:0.8f);
        if (std::fabs(itd_b) < 0.5f && std::fabs(ild_b) < 0.5f) {
            theta[b] = 0.0f;
            conf_band[b] *= 0.5f;
        } else {
            theta[b] = std::atan2(K1 * itd_w * itd_b, K2 * (1.0f - itd_w) * ild_b);
        }
    }

    // Step 9 — Direction fusion
    BaPerceptualSceneState pss{};
    float Dx=0.0f, Dy=0.0f, sc=0.0f;
    for (int b = 0; b < 3; ++b) { Dx += conf_band[b] * std::cos(theta[b]); Dy += conf_band[b] * std::sin(theta[b]); sc += conf_band[b]; }
    pss.azimuth = (sc < 1e-6f) ? ctx->pss_prev.azimuth : std::atan2(Dy/sc, Dx/sc);
    pss.elevation = 0.0f;

    // Step 10 — Overall confidence
    float ic_max = std::max({0.0f, IC[0], IC[1], IC[2]});
    float ic_f = std::max(0.0f, std::min(1.0f, (ic_max - 0.3f) / 0.7f));
    float b_avg = (conf_band[0] + conf_band[1] + conf_band[2]) / 3.0f;
    pss.confidence = std::max(0.0f, std::min(1.0f, b_avg * ic_f * ms_penalty));

    // Step 11 — Remaining fields
    pss.energy[0] = EL[0]+ER[0]; pss.energy[1] = EL[1]+ER[1]; pss.energy[2] = EL[2]+ER[2];
    pss.energy_total = pss.energy[0]+pss.energy[1]+pss.energy[2];
    pss.centroid = centroid;
    pss.onset = onset;
    pss.azimuth_prev = ctx->pss_prev.azimuth;
    pss.frame = ctx->pss_prev.frame + 1;
    pss.diffuseness = 1.0f - pss.confidence;

    // Step 12 — IR rebuild trigger
    float delta = std::fabs(pss.azimuth - ctx->last_rebuild_angle);
    ctx->cumulative_delta += delta;
    bool large_jump = delta > 10.0f * DEG2RAD;
    bool stable = ctx->stable_frames >= (int)(0.01f * ctx->sample_rate / ctx->buffer_size);
    if (large_jump || (ctx->cumulative_delta >= 2.0f * DEG2RAD && stable))
        queue_ir_rebuild(ctx, pss.azimuth, pss.elevation);
    ctx->stable_frames = (pss.confidence > 0.5f) ? ctx->stable_frames + 1 : 0;

    ctx->pss_prev = ctx->pss;
    ctx->pss = pss;
}
