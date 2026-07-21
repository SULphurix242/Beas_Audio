// Copyright (c) 2026 Beas Audio Engineering. All Rights Reserved.
// Proprietary — no license granted.
// Beas Audio DSP Library — PCI: object control & prioritization
#include "pci.h"
#include <cmath>
#include <algorithm>
#include <vector>

static constexpr float SAL_ENTER = 0.07f;
static constexpr float SAL_EXIT  = 0.03f;
static constexpr float DEG2RAD   = 3.14159265f / 180.0f;

static float ang_dist_deg(const BaOSPos& a, const BaOSPos& b) {
    float dot = std::cos(a.elevation) * std::cos(b.elevation) * std::cos(a.azimuth - b.azimuth)
              + std::sin(a.elevation) * std::sin(b.elevation);
    dot = std::max(-1.0f, std::min(1.0f, dot));
    return std::acos(dot) / DEG2RAD;
}

static int hash_cell(const BaOSPos& pos, int res_deg) {
    float az_d = pos.azimuth / DEG2RAD;
    float el_d = pos.elevation / DEG2RAD;
    int ca = (int)((az_d + 180.0f) / res_deg) % BA_GRID_AZ_CELLS;
    int ce = (int)((el_d + 90.0f) / (res_deg * 3.0f)) % BA_GRID_EL_CELLS;
    return ca * BA_GRID_EL_CELLS + ce;
}

float hash_grid_distinctness(BaAudioContext* ctx, int idx, const BaOSPos* pos, uint32_t N) {
    int cell = hash_cell(pos[idx], 10);
    float min_d = 180.0f;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int de = -1; de <= 1; ++de) {
            int ca = (cell / BA_GRID_EL_CELLS + dz + BA_GRID_AZ_CELLS) % BA_GRID_AZ_CELLS;
            int ce = (cell % BA_GRID_EL_CELLS + de + BA_GRID_EL_CELLS) % BA_GRID_EL_CELLS;
            int nc = ca * BA_GRID_EL_CELLS + ce;
            for (uint32_t j = 0; j < N; ++j) {
                if ((int)j == idx) continue;
                if (hash_cell(pos[j], 10) == nc) {
                    float d = ang_dist_deg(pos[idx], pos[j]);
                    if (d < min_d) min_d = d;
                }
            }
        }
    }
    return (min_d == 180.0f) ? 1.0f : min_d / 180.0f;
}

float hash_grid_nearest_any(BaAudioContext* ctx, int idx, const BaOSPos* pos, uint32_t N) {
    int cell = hash_cell(pos[idx], 10);
    float min_d = 180.0f;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int de = -1; de <= 1; ++de) {
            int ca = (cell / BA_GRID_EL_CELLS + dz + BA_GRID_AZ_CELLS) % BA_GRID_AZ_CELLS;
            int ce = (cell % BA_GRID_EL_CELLS + de + BA_GRID_EL_CELLS) % BA_GRID_EL_CELLS;
            int nc = ca * BA_GRID_EL_CELLS + ce;
            for (uint32_t j = 0; j < N; ++j) {
                if ((int)j == idx) continue;
                if (hash_cell(pos[j], 10) == nc) {
                    float d = ang_dist_deg(pos[idx], pos[j]);
                    if (d < min_d) min_d = d;
                }
            }
        }
    }
    return min_d;
}

extern void ambient_bus_add(BaAudioContext* ctx, int object_idx, const BaAudioObject* objects);

void pci_two_pass(BaAudioContext* ctx, const BaOSPos* positions,
                  const BaAudioObject* objects, uint32_t N_objects) {
    // Oracle smoothing
    for (uint32_t i = 0; i < N_objects; ++i)
        ctx->pci_smooth[i] = 0.7f * ctx->pci_smooth[i] + 0.3f * ctx->pci_prev[i];

    // Saliency hysteresis
    bool passes[BA_MAX_OBJECTS] = {};
    for (uint32_t i = 0; i < N_objects; ++i) {
        if (ctx->saliency[i] < 1e-10f) {
            ctx->pci_prev[i] = 0.0f;
            ambient_bus_add(ctx, i, objects);
            continue;
        }
        if (ctx->obj_rendering[i]) {
            if (ctx->saliency[i] < SAL_EXIT) {
                ctx->obj_rendering[i] = false;
                ambient_bus_add(ctx, i, objects);
                continue;
            }
        } else {
            if (ctx->saliency[i] < SAL_ENTER) {
                ambient_bus_add(ctx, i, objects);
                continue;
            }
            ctx->obj_rendering[i] = true;
        }
        passes[i] = true;
    }

    // Pass 1 — spatial + temporal components
    float spatial[BA_MAX_OBJECTS] = {}, temporal[BA_MAX_OBJECTS] = {};
    for (uint32_t i = 0; i < N_objects; ++i) {
        if (!passes[i]) continue;
        spatial[i] = hash_grid_distinctness(ctx, i, positions, N_objects);
        float near = hash_grid_nearest_any(ctx, i, positions, N_objects);
        spatial[i] = std::min(spatial[i], near / 180.0f);
        temporal[i] = (ctx->obj_lifetime[i] < ctx->buffer_duration)
                      ? ctx->saliency[i]
                      : std::min(ctx->obj_lifetime[i] / 2.0f, 1.0f);
    }

    // Pass 2 — PSS-modulated PCI
    float conf = ctx->pss.confidence;
    float w2_eff = ctx->w2 * conf;
    float w3_eff = ctx->w3 + (ctx->w2 - w2_eff);
    if (ctx->pss.diffuseness > 0.8f) { w2_eff = 0.0f; w3_eff = 1.0f - ctx->w1; }
    ctx->fn_pci_batch(ctx->saliency, spatial, temporal, ctx->pci_prev,
                      ctx->w1, w2_eff, w3_eff, N_objects);

    // Tiebreaker sort
    std::vector<int> indices;
    for (uint32_t i = 0; i < N_objects; ++i) if (passes[i]) indices.push_back(i);
    std::stable_sort(indices.begin(), indices.end(), [&](int a, int b) {
        if (std::fabs(ctx->pci_prev[a] - ctx->pci_prev[b]) > 1e-4f)
            return ctx->pci_prev[a] > ctx->pci_prev[b];
        return a < b;
    });

    // Update lifetimes
    for (uint32_t i = 0; i < N_objects; ++i)
        ctx->obj_lifetime[i] = passes[i] ? ctx->obj_lifetime[i] + ctx->buffer_duration : 0.0f;
}
