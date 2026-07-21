// Copyright (c) 2026 Beas Audio Engineering. All Rights Reserved.
// Proprietary — no license granted.
// Beas Audio DSP Library — ambient bus accumulation, late reverb, render
#include "ambient.h"
#include <cstring>
#include <cmath>

// Prime-based all-pass delays for decorrelation
static constexpr int REV_DELAYS[4] = {311, 443, 673, 919};
static constexpr float REV_FEEDBACK = 0.6f;
static constexpr float REV_MIX = 0.3f;

static void init_reverb(BaAudioContext* ctx) {
    for (int i = 0; i < 4; ++i) {
        std::memset(ctx->ambient_bus.rev_delay_L[i], 0, 1024 * sizeof(float));
        std::memset(ctx->ambient_bus.rev_delay_R[i], 0, 1024 * sizeof(float));
        ctx->ambient_bus.rev_ptr[i] = 0;
        ctx->ambient_bus.rev_gain[i] = 0.7f; // All-pass gain
    }
    ctx->ambient_bus.rev_init = true;
}

void ambient_bus_add(BaAudioContext* ctx, int object_idx, const BaAudioObject* objects) {
    (void)object_idx; (void)objects;
    // ponytail: just increments count — no per-object audio accumulation.
    // Proper impl: mix object's contribution into buf_L/R with simple pan.
    // Upgrade when per-object audio buffers are wired.
    ctx->ambient_bus.count++;
}

void ambient_bus_clear(BaAudioContext* ctx) {
    std::memset(ctx->ambient_bus.buf_L, 0, sizeof(ctx->ambient_bus.buf_L));
    std::memset(ctx->ambient_bus.buf_R, 0, sizeof(ctx->ambient_bus.buf_R));
    ctx->ambient_bus.count = 0;
    ctx->ambient_bus.diffuseness = 0.0f;
}

void ambient_bus_render(BaAudioContext* ctx, float* out_L, float* out_R, uint32_t N) {
    if (ctx->ambient_bus.count == 0) return;
    if (!ctx->ambient_bus.rev_init) init_reverb(ctx);

    float scale = 1.0f / std::sqrt((float)ctx->ambient_bus.count + 1.0f);

    // Late reverb pass through 4 all-pass cascades
    float wet_L[1024], wet_R[1024];
    for (uint32_t i = 0; i < N; ++i) {
        float dry = ctx->ambient_bus.buf_L[i] * scale;
        for (int ap = 0; ap < 4; ++ap) {
            int ptr = ctx->ambient_bus.rev_ptr[ap];
            int dly = REV_DELAYS[ap];
            float delayed = ctx->ambient_bus.rev_delay_L[ap][(ptr - dly + 1024) % 1024];
            ctx->ambient_bus.rev_delay_L[ap][ptr] = dry + delayed * REV_FEEDBACK;
            dry = delayed - dry * ctx->ambient_bus.rev_gain[ap];
            dry *= REV_FEEDBACK;
            ctx->ambient_bus.rev_ptr[ap] = (ptr + 1) % 1024;
        }
        wet_L[i] = dry;
    }
    for (uint32_t i = 0; i < N; ++i) {
        float dry = ctx->ambient_bus.buf_R[i] * scale;
        for (int ap = 0; ap < 4; ++ap) {
            int ptr = ctx->ambient_bus.rev_ptr[ap];
            int dly = REV_DELAYS[ap];
            float delayed = ctx->ambient_bus.rev_delay_R[ap][(ptr - dly + 1024) % 1024];
            ctx->ambient_bus.rev_delay_R[ap][ptr] = dry + delayed * REV_FEEDBACK;
            dry = delayed - dry * ctx->ambient_bus.rev_gain[ap];
            dry *= REV_FEEDBACK;
            ctx->ambient_bus.rev_ptr[ap] = (ptr + 1) % 1024;
        }
        wet_R[i] = dry;
    }

    // Mix dry + wet back to output
    for (uint32_t i = 0; i < N; ++i) {
        out_L[i] += ctx->ambient_bus.buf_L[i] * scale * (1.0f - REV_MIX)
                    + wet_L[i] * REV_MIX;
        out_R[i] += ctx->ambient_bus.buf_R[i] * scale * (1.0f - REV_MIX)
                    + wet_R[i] * REV_MIX;
    }
}
