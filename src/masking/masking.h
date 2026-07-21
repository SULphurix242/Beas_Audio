// Copyright (c) 2026 Beas Audio Engineering. All Rights Reserved.
// Proprietary — no license granted.
// Beas Audio DSP Library — masking model & saliency computation
#pragma once
#include "internal.h"
void masking_floor_update(BaAudioContext* ctx, const BaPerceptualSceneState& pss);
void masking_saliency_compute(BaAudioContext* ctx, const float* e_low,
                              const float* e_mid, const float* e_high,
                              uint32_t N_objects);
void masking_lut_build(BaAudioContext* ctx);
void masking_set_mode_ratios(BaAudioContext* ctx, BaMode mode);
