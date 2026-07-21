// Copyright (c) 2026 Beas Audio Engineering. All Rights Reserved.
// Proprietary — no license granted.
// Beas Audio DSP Library — masking model & saliency computation
#include "masking.h"
#include <cmath>
#include <algorithm>

void masking_set_mode_ratios(BaAudioContext* ctx, BaMode mode) {
    switch (mode) {
    case BA_MODE_GENERIC:   ctx->mask_ratio[0]=1.0f; ctx->mask_ratio[1]=1.0f; ctx->mask_ratio[2]=1.0f; break;
    case BA_MODE_ACTION:    ctx->mask_ratio[0]=0.8f; ctx->mask_ratio[1]=1.2f; ctx->mask_ratio[2]=1.0f; break;
    case BA_MODE_ADVENTURE: ctx->mask_ratio[0]=1.0f; ctx->mask_ratio[1]=1.0f; ctx->mask_ratio[2]=1.0f; break;
    case BA_MODE_RACING:    ctx->mask_ratio[0]=0.7f; ctx->mask_ratio[1]=1.3f; ctx->mask_ratio[2]=1.0f; break;
    case BA_MODE_IMMERSIVE: ctx->mask_ratio[0]=1.2f; ctx->mask_ratio[1]=1.0f; ctx->mask_ratio[2]=0.8f; break;
    case BA_MODE_MMORPG:    ctx->mask_ratio[0]=0.9f; ctx->mask_ratio[1]=1.1f; ctx->mask_ratio[2]=1.0f; break;
    case BA_MODE_STRATEGY:  ctx->mask_ratio[0]=1.0f; ctx->mask_ratio[1]=1.0f; ctx->mask_ratio[2]=1.0f; break;
    case BA_MODE_SPEECH:    ctx->mask_ratio[0]=0.5f; ctx->mask_ratio[1]=0.5f; ctx->mask_ratio[2]=2.0f; break;
    case BA_MODE_MUSIC:     ctx->mask_ratio[0]=1.0f; ctx->mask_ratio[1]=0.8f; ctx->mask_ratio[2]=1.2f; break;
    case BA_MODE_CINEMATIC: ctx->mask_ratio[0]=1.2f; ctx->mask_ratio[1]=1.0f; ctx->mask_ratio[2]=0.8f; break;
    }
}

void masking_lut_build(BaAudioContext* ctx) {
    // ERB-based masking spread — frequency-dependent
    // ERB(f) = 24.7 + 0.108 * f (Hz). Three bands: 300, 1000, 6000 Hz centroids.
    // Normalise to angle: map ERB bandwidth to degrees.
    static constexpr float ERB_CENTROIDS[3] = {300.0f, 1000.0f, 6000.0f};
    float max_erb = 24.7f + 0.108f * 6000.0f; // ~673Hz at 6kHz
    // The ERB width in degrees is approximated by a simple power-law
    // mapping: erb_deg ≈ 5 + (f - 300) / 6000 * 25, which gives about
    // 5° at low frequencies, ~30° at high
    for (int i = 0; i < BA_LUT_STEPS; ++i) {
        float angle_deg = i * 2.0f;
        // Weighted by frequency band — use the average centroid
        float erb_hz = 0.0f;
        for (int b = 0; b < 3; ++b)
            erb_hz += (24.7f + 0.108f * ERB_CENTROIDS[b]) / 3.0f;
        // Convert ERB to degrees: rough mapping using 30° ≈ 1 ERB at
        // the max frequency, scaling down linearly
        float erb_deg = 5.0f + (erb_hz / max_erb) * 25.0f;
        // ERB asymmetry: mask stronger below (closer objects below = more masking)
        float x = angle_deg / erb_deg;
        // Asymmetric sinc-like spread: exp(-x^1.5) for x < 1, exp(-x^2) for x > 1
        float mask = (x < 1.0f)
            ? std::exp(-std::pow(x, 1.5f))
            : std::exp(-x * x);
        ctx->masking_lut[i] = mask;
    }
}

void masking_floor_update(BaAudioContext* ctx, const BaPerceptualSceneState& pss) {
    // ponytail: energy-weighted masking floor per band
    float total = pss.energy[0] + pss.energy[1] + pss.energy[2] + 1e-10f;
    ctx->masking_floor[0] = pss.energy[0] / total;
    ctx->masking_floor[1] = pss.energy[1] / total;
    ctx->masking_floor[2] = pss.energy[2] / total;
    // If total is negligible, set default floor so all objects are salient
    if (pss.energy[0] + pss.energy[1] + pss.energy[2] < 1e-9f) {
        ctx->masking_floor[0] = ctx->masking_floor[1] = ctx->masking_floor[2] = 0.01f;
    }
}

void masking_saliency_compute(BaAudioContext* ctx, const float* e_low,
                              const float* e_mid, const float* e_high,
                              uint32_t N_objects) {
    for (uint32_t i = 0; i < N_objects; ++i) {
        float sal = (e_low[i]  * ctx->mask_ratio[0] +
                     e_mid[i]  * ctx->mask_ratio[1] +
                     e_high[i] * ctx->mask_ratio[2]) / 3.0f;
        // ponytail: floor comparison produces relative saliency in 0..1 range
        float floor_avg = (ctx->masking_floor[0] + ctx->masking_floor[1] + ctx->masking_floor[2]) / 3.0f;
        if (floor_avg > 1e-6f)
            sal = std::min(1.0f, sal / floor_avg);
        else
            sal = 0.0f;
        ctx->saliency[i] = sal;
    }
}
