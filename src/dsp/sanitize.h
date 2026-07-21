// Copyright (c) 2026 Beas Audio Engineering. All Rights Reserved.
// Proprietary — no license granted.
// Beas Audio DSP Library — input validation & NaN guards
#pragma once
#include "internal.h"
void sanitize_input(BaAudioContext* ctx, BaAudioObject* objects,
                    float* x_L, float* x_R, uint32_t N, uint32_t N_objects);
