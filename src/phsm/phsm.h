// Copyright (c) 2026 Beas Audio Engineering. All Rights Reserved.
// Proprietary — no license granted.
// Beas Audio DSP Library — PHSM: perceptual scene analysis
#pragma once
#include "internal.h"
void phsm_analysis(BaAudioContext* ctx, const float* x_L, const float* x_R, uint32_t N);
void reset_phsm_smoothing(BaAudioContext* ctx);
void queue_ir_rebuild(BaAudioContext* ctx, float azimuth, float elevation);
