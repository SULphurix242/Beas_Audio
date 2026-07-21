// Copyright (c) 2026 Beas Audio Engineering. All Rights Reserved.
// Proprietary — no license granted.
// Beas Audio DSP Library — input validation & NaN guards
#include "sanitize.h"
#include <cmath>
#include <cstring>
#include <algorithm>

static bool is_invalid(float x) {
    // ponytail: std::isnan/isinf folded into one check for hot path
    return std::isnan(x) || std::isinf(x);
}

void sanitize_input(BaAudioContext* ctx, BaAudioObject* objects,
                    float* x_L, float* x_R, uint32_t N, uint32_t N_objects) {
    // N = N_samples (for PCM, x_L/x_R iteration)
    // N_objects = number of valid objects in the array
    if (!objects) return;
    // Validate object fields — only up to N_objects
    uint32_t obj_count = N_objects;
    for (uint32_t i = 0; i < obj_count; ++i) {
        BaAudioObject& obj = objects[i];
        if (is_invalid(obj.azimuth))   obj.azimuth = 0.0f;
        if (is_invalid(obj.elevation)) obj.elevation = 0.0f;
        if (is_invalid(obj.distance))  obj.distance = 1.0f;
        if (is_invalid(obj.gain))      obj.gain = 0.0f;
        // Clamp gain to sane range
        if (obj.gain < 0.0f) obj.gain = 0.0f;
        if (obj.gain > 100.0f) obj.gain = 100.0f;
        // Clamp azimuth/elevation to valid range
        if (obj.azimuth < -3.14159265f) obj.azimuth = -3.14159265f;
        if (obj.azimuth >  3.14159265f) obj.azimuth =  3.14159265f;
        if (obj.elevation < -1.57079633f) obj.elevation = -1.57079633f;
        if (obj.elevation >  1.57079633f) obj.elevation =  1.57079633f;
    }

    // Validate input audio buffers
    if (x_L && x_R) {
        for (uint32_t i = 0; i < N; ++i) {
            if (is_invalid(x_L[i])) const_cast<float*>(x_L)[i] = 0.0f;
            if (is_invalid(x_R[i])) const_cast<float*>(x_R)[i] = 0.0f;
        }
    }

    // Validate per-object PCM and zero invalid samples — only up to N_samples
    for (uint32_t i = 0; i < obj_count; ++i) {
        const BaAudioObject& obj = objects[i];
        if (obj.pcm == nullptr) continue;
        for (uint32_t s = 0; s < N; ++s) {
            if (is_invalid(obj.pcm[s]))
                const_cast<float*>(obj.pcm)[s] = 0.0f;
        }
    }
}
