// Copyright (c) 2026 Beas Audio Engineering. All Rights Reserved.
// Proprietary — no license granted.
// Beas Audio DSP Library — flush & deferred control checks
#include "flush.h"
#include "phsm/phsm.h"
#include <cstring>

void check_pending_flush(BaAudioContext* ctx) {
    if (!ctx->ctrl.flush_pending.load(std::memory_order_acquire))
        return;
    ctx->ctrl.flush_pending.store(false, std::memory_order_release);

    // Reset all per-object state
    std::memset(ctx->pci_prev, 0, ctx->max_objects * sizeof(float));
    std::memset(ctx->pci_smooth, 0, ctx->max_objects * sizeof(float));
    std::memset(ctx->saliency, 0, ctx->max_objects * sizeof(float));
    std::memset(ctx->obj_rendering, 0, ctx->max_objects * sizeof(bool));
    std::memset(ctx->obj_lifetime, 0, ctx->max_objects * sizeof(float));
    std::memset(ctx->cached_dist, 0, ctx->max_objects * sizeof(float));

    // Reset PSS
    reset_phsm_smoothing(ctx);

    // Clear ambient bus
    std::memset(ctx->ambient_bus.buf_L, 0, sizeof(ctx->ambient_bus.buf_L));
    std::memset(ctx->ambient_bus.buf_R, 0, sizeof(ctx->ambient_bus.buf_R));
    ctx->ambient_bus.count = 0;
    ctx->ambient_bus.diffuseness = 0.0f;
}

void check_pending_tier(BaAudioContext* ctx) {
    int t = ctx->ctrl.pending_tier.load(std::memory_order_acquire);
    if (t < 0) return;
    ctx->ctrl.pending_tier.store(-1, std::memory_order_release);
    apply_tier_internal(ctx, t);
}

void check_pending_path(BaAudioContext* ctx) {
    int p = ctx->ctrl.pending_path.load(std::memory_order_acquire);
    if (p < 0) return;
    ctx->ctrl.pending_path.store(-1, std::memory_order_release);
    ctx->path = (BaPath)p;
}

void check_pending_mode(BaAudioContext* ctx) {
    int m = ctx->ctrl.pending_mode.load(std::memory_order_acquire);
    if (m < 0) return;
    ctx->ctrl.pending_mode.store(-1, std::memory_order_release);
    ctx->mode = (BaMode)m;
    // Re-apply mode weights
    masking_set_mode_ratios(ctx, ctx->mode);
}

void apply_tier_internal(BaAudioContext* ctx, int tier) {
    if (tier < 0 || tier > 3) return;
    ctx->tier = tier;

    // Set object limits per tier
    switch (tier) {
    case 0: ctx->N_soft = 8;  ctx->N_hard = 16; break;
    case 1: ctx->N_soft = 16; ctx->N_hard = 32; break;
    case 2: ctx->N_soft = 32; ctx->N_hard = 48; break;
    case 3: ctx->N_soft = 48; ctx->N_hard = 64; break;
    }

    // Set ITD max lag
    ctx->itd_tau_max = (tier >= 1) ? (int)(0.00069f * ctx->sample_rate + 0.5f) : 0;

    // Set cluster resolution
    ctx->cluster_res_deg = (tier >= 2) ? 5 : 10;

    // IR length handled externally (HRTF load)
}
