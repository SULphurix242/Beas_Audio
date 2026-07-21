// Copyright (c) 2026 Beas Audio Engineering. All Rights Reserved.
// Proprietary — no license granted.
// Beas Audio DSP Library — post-processing (gain staging + EQ)
#pragma once
#include "internal.h"
void post_process(BaAudioContext* ctx, float* out_L, float* out_R, uint32_t N);
