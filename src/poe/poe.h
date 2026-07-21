// Copyright (c) 2026 Beas Audio Engineering. All Rights Reserved.
// Proprietary — no license granted.
// Beas Audio DSP Library — POE: binaural rendering engine
#pragma once
#include "internal.h"
void poe_masking_pass(BaAudioContext* ctx, const BaOSPos* positions,
                      const BaAudioObject* objects, float* render_gain,
                      uint32_t N_surviving);
void poe_merge_objects(BaAudioContext* ctx, int idx_a, int idx_b,
                       const BaOSPos* positions, float* render_gain);
void poe_spawn_tick(BaAudioContext* ctx);
