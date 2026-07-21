// Copyright (c) 2026 Beas Audio Engineering. All Rights Reserved.
// Proprietary — no license granted.
// Beas Audio DSP Library — POE: binaural rendering engine
#include "poe.h"
#include <cmath>
#include <algorithm>

static constexpr float DEG2RAD = 3.14159265f / 180.0f;

static float ang_dist_deg(const BaOSPos& a, const BaOSPos& b) {
    float dot = std::cos(a.elevation) * std::cos(b.elevation) * std::cos(a.azimuth - b.azimuth)
              + std::sin(a.elevation) * std::sin(b.elevation);
    dot = std::max(-1.0f, std::min(1.0f, dot));
    return std::acos(dot) / DEG2RAD;
}

extern void ambient_bus_add(BaAudioContext* ctx, int object_idx, const BaAudioObject* objects);

void poe_spawn_tick(BaAudioContext* ctx) {
    int activate = std::min(ctx->spawn_queue_count, 16);
    for (int i = 0; i < activate; ++i) {
        int oid = ctx->spawn_queue[ctx->spawn_offset + i];
        ctx->obj_rendering[oid] = true;
        ctx->obj_lifetime[oid] = 0.0f;
    }
    ctx->spawn_offset += activate;
    ctx->spawn_queue_count -= activate;
    if (ctx->spawn_queue_count <= 0) { ctx->spawn_offset = 0; ctx->spawn_queue_count = 0; }
}

void poe_merge_objects(BaAudioContext* ctx, int a, int b,
                       const BaOSPos* pos, float* gain) {
    float Ea = gain[a]*gain[a], Eb = gain[b]*gain[b], Et = Ea + Eb + 1e-10f;
    BaOSPos merged;
    merged.azimuth   = (Ea * pos[a].azimuth   + Eb * pos[b].azimuth)   / Et;
    merged.elevation = (Ea * pos[a].elevation + Eb * pos[b].elevation) / Et;
    merged.distance  = (Ea * pos[a].distance  + Eb * pos[b].distance)  / Et;
    ctx->obj_lifetime[b] = std::max(ctx->obj_lifetime[a], ctx->obj_lifetime[b]);
    gain[a] = 0.0f;
    const_cast<BaOSPos*>(pos)[b] = merged;
}

void poe_masking_pass(BaAudioContext* ctx, const BaOSPos* pos,
                      const BaAudioObject* objects, float* gain,
                      uint32_t N_surviving) {
    if (N_surviving <= (uint32_t)ctx->N_soft) {
        for (uint32_t i = 0; i < N_surviving; ++i) gain[i] = 1.0f;
        return;
    }

    float ramp = std::min(1.0f, (float)(N_surviving - ctx->N_soft) / 4.0f);

    for (uint32_t i = 0; i < N_surviving; ++i) {
        float total_mask = 0.0f;
        for (uint32_t j = 0; j < i; ++j) {
            if (gain[j] == 0.0f) continue;
            float ang = ang_dist_deg(pos[i], pos[j]);
            int idx = std::min((int)(ang / 2.0f), 90);
            total_mask += (gain[j] * gain[j]) * ctx->masking_lut[idx];
        }
        float threshold = total_mask * ctx->mask_ratio[1] * ramp;
        float Ei = gain[i] * gain[i];
        float ratio = Ei / (threshold + 1e-10f);
        if (ratio > 1.2f) gain[i] = 1.0f;
        else if (ratio > 0.4f) gain[i] = (ratio - 0.4f) / 0.8f;
        else { gain[i] = 0.0f; ambient_bus_add(ctx, i, objects); }
    }

    if (N_surviving > (uint32_t)ctx->N_hard) {
        for (uint32_t i = 0; i < N_surviving; ++i) {
            if (gain[i] == 0.0f) continue;
            for (uint32_t j = i + 1; j < N_surviving; ++j) {
                if (gain[j] == 0.0f) continue;
                if (ang_dist_deg(pos[i], pos[j]) < (float)ctx->cluster_res_deg) {
                    if (ctx->obj_lifetime[i] >= ctx->obj_lifetime[j])
                        poe_merge_objects(ctx, j, i, pos, gain);
                    else
                        poe_merge_objects(ctx, i, j, pos, gain);
                }
            }
        }
    }
}
