// Copyright (c) 2026 Beas Audio Engineering. All Rights Reserved.
// Proprietary — no license granted.
// Beas Audio DSP Library — ambient bus accumulation & render
#pragma once
#include "internal.h"
void ambient_bus_clear(BaAudioContext* ctx);
void ambient_bus_render(BaAudioContext* ctx, float* out_L, float* out_R, uint32_t N);
