// Copyright (c) 2026 Beas Audio Engineering. All Rights Reserved.
// Proprietary — no license granted.
// Beas Audio DSP Library — multi-band parametric EQ (biquad cascade)
#pragma once
#include "internal.h"
struct BaEQState;
BaEQState* eq_create(int band_count, uint32_t sample_rate, void*(alloc_fn)(size_t));
void eq_destroy(BaEQState* eq, void(free_fn)(void*));
void eq_set_band(BaEQState* eq, int band, int filter_type,
                 float freq_hz, float gain_db, float q);
void eq_process(BaEQState* eq, float* out_L, float* out_R, uint32_t N);
