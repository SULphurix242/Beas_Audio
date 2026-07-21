// Copyright (c) 2026 Beas Audio Engineering. All Rights Reserved.
// Proprietary — no license granted.
// Beas Audio DSP Library — CPU timer & adaptive quality watchdog
// Uses std::chrono for portability (no rdtsc — works on ARM/Apple Silicon)
#include "cpu.h"
#include <chrono>
#include <algorithm>

uint64_t timer_ns() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<nanoseconds>(
        high_resolution_clock::now().time_since_epoch()).count();
}

void cpu_watchdog(BaAudioContext* ctx) {
    if (ctx->tier_cooldown_frames > 0) {
        ctx->tier_cooldown_frames--;
        return;
    }

    float R = ctx->cpu_load_ema;

    if (R > 0.90f && ctx->tier > 0) {
        // Emergency downgrade
        apply_tier_internal(ctx, ctx->tier - 1);
        ctx->last_auto_tier = ctx->tier;
        ctx->tier_cooldown_frames = BA_WATCHDOG_COOLDOWN;
    } else if (R > 0.85f && ctx->tier > 0) {
        // Downgrade
        apply_tier_internal(ctx, ctx->tier - 1);
        ctx->last_auto_tier = ctx->tier;
        ctx->tier_cooldown_frames = BA_WATCHDOG_COOLDOWN;
    } else if (R > 0.80f) {
        // Reduce N_soft by 25%
        ctx->N_soft = std::max(1, (int)(ctx->N_soft * 0.75f));
        ctx->tier_cooldown_frames = BA_WATCHDOG_COOLDOWN / 2;
    } else if (R > 0.75f) {
        // Disable ITD
        ctx->itd_tau_max = 0;
        ctx->tier_cooldown_frames = BA_WATCHDOG_COOLDOWN / 2;
    } else if (R > 0.70f) {
        // Coarsen cluster resolution
        if (ctx->cluster_res_deg < 10) {
            ctx->cluster_res_deg = 10;
            masking_lut_build(ctx);
        }
        ctx->tier_cooldown_frames = BA_WATCHDOG_COOLDOWN / 2;
    } else if (R < BA_WATCHDOG_UP_THR && ctx->tier < ctx->last_auto_tier) {
        // Upgrade back
        apply_tier_internal(ctx, ctx->tier + 1);
        ctx->tier_cooldown_frames = BA_WATCHDOG_COOLDOWN;
    }
}
