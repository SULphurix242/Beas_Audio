// Copyright (c) 2026 Beas Audio Engineering. All Rights Reserved.
// Proprietary — no license granted.
// Beas Audio DSP Library — post-processing (gain staging + EQ + compressor)
#include "postprocess.h"
#include "eq.h"
#include <cmath>
#include <algorithm>

void post_process(BaAudioContext* ctx, float* out_L, float* out_R, uint32_t N) {
    // Compressor
    static constexpr float RMS_COEFF = 0.3f;      // RMS averaging factor
    static constexpr float THRESHOLD = 0.5f;        // -6dB threshold
    static constexpr float RATIO     = 4.0f;         // 4:1
    static constexpr float KNEE      = 0.2f;         // soft knee width
    static constexpr float ATTACK    = 0.1f;         // attack smoothing
    static constexpr float RELEASE   = 0.05f;        // release smoothing

    float max_env = 0.0f;
    for (uint32_t i = 0; i < N; ++i) {
        float abs_samp = std::max(std::fabs(out_L[i]), std::fabs(out_R[i]));
        max_env = RMS_COEFF * abs_samp + (1.0f - RMS_COEFF) * max_env;
    }
    // It's a static gain computer (ponytail: fast feed-forward)
    float db = 20.0f * std::log10(max_env + 1e-10f);
    float db_thr = 20.0f * std::log10(THRESHOLD);
    float db_knee = KNEE * 20.0f;
    float reduction = 0.0f;
    if (db > db_thr - db_knee / 2.0f) {
        float over = db - (db_thr - db_knee / 2.0f);
        if (over < db_knee) {
            // Soft knee
            float x = over / db_knee;
            reduction = x * x * db_knee * (1.0f / RATIO - 1.0f) / 3.0f;
        } else {
            // Hard knee
            reduction = (db - db_thr) * (1.0f - 1.0f / RATIO);
        }
    }
    float gain_reduction = std::pow(10.0f, reduction / 20.0f);
    // Smooth gain (ponytail: one-pole per frame — sample-accurate when needed)
    static float smooth_gain = 1.0f;
    float target = std::min(1.0f, gain_reduction);
    smooth_gain += (max_env > smooth_gain ? ATTACK : RELEASE) * (target - smooth_gain);

    // Hard limiter ceiling at 0dBFS
    float ceiling = 0.99f;

    for (uint32_t i = 0; i < N; ++i) {
        out_L[i] *= smooth_gain;
        out_R[i] *= smooth_gain;
        if (std::fabs(out_L[i]) > ceiling) out_L[i] = (out_L[i] > 0.0f ? ceiling : -ceiling);
        if (std::fabs(out_R[i]) > ceiling) out_R[i] = (out_R[i] > 0.0f ? ceiling : -ceiling);
    }

    // Apply EQ if configured
    if (ctx->eq) eq_process(ctx->eq, out_L, out_R, N);
}
