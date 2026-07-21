// Copyright (c) 2026 Beas Audio Engineering. All Rights Reserved.
// Proprietary — no license granted.
// Beas Audio DSP Library — multi-band parametric EQ (biquad cascade)
#include "eq.h"
#include <cmath>
#include <cstring>

struct BaBiquad { float b0,b1,b2,a1,a2; float z1,z2; };
struct BaEQState { int count; uint32_t sr; BaBiquad bands[16]; };

// All biquad coefficients in one switch. gain_db ignored for filter types
// that don't use it (low/high pass, notch, bandpass, allpass).
static BaBiquad biquad_coeffs(int type, float freq, float gain_db, float q, uint32_t sr) {
    BaBiquad f{};
    float w0 = 6.2831853f * freq / (float)sr;
    float c = std::cos(w0); float s = std::sin(w0);
    float alpha = s / (2.0f * q);
    float A = std::pow(10.0f, gain_db / 40.0f);

    switch (type) {
    case 0: { // PEAKING — bell shape, gain_db active
        float a0 = 1.0f + alpha / A;
        f.b0 = (1.0f + alpha * A) / a0; f.b1 = (-2.0f * c) / a0; f.b2 = (1.0f - alpha * A) / a0;
        f.a1 = (-2.0f * c) / a0;         f.a2 = (1.0f - alpha / A) / a0;
        break;
    }
    case 1: { // LOW_SHELF
        float a0 = (A + 1.0f) + (A - 1.0f) * c + 2.0f * std::sqrt(A) * alpha;
        f.b0 = (A * ((A + 1.0f) - (A - 1.0f) * c + 2.0f * std::sqrt(A) * alpha)) / a0;
        f.b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * c)) / a0;
        f.b2 = (A * ((A + 1.0f) - (A - 1.0f) * c - 2.0f * std::sqrt(A) * alpha)) / a0;
        f.a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * c)) / a0;
        f.a2 = ((A + 1.0f) + (A - 1.0f) * c - 2.0f * std::sqrt(A) * alpha) / a0;
        break;
    }
    case 2: { // HIGH_SHELF
        float a0 = (A + 1.0f) - (A - 1.0f) * c + 2.0f * std::sqrt(A) * alpha;
        f.b0 = (A * ((A + 1.0f) + (A - 1.0f) * c + 2.0f * std::sqrt(A) * alpha)) / a0;
        f.b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * c)) / a0;
        f.b2 = (A * ((A + 1.0f) + (A - 1.0f) * c - 2.0f * std::sqrt(A) * alpha)) / a0;
        f.a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * c)) / a0;
        f.a2 = ((A + 1.0f) - (A - 1.0f) * c - 2.0f * std::sqrt(A) * alpha) / a0;
        break;
    }
    case 3: { // LOW_PASS
        float a0 = 1.0f + alpha;
        f.b0 = ((1.0f - c) / 2.0f) / a0; f.b1 = (1.0f - c) / a0; f.b2 = f.b0;
        f.a1 = (-2.0f * c) / a0;         f.a2 = (1.0f - alpha) / a0;
        break;
    }
    case 4: { // HIGH_PASS
        float a0 = 1.0f + alpha;
        f.b0 = ((1.0f + c) / 2.0f) / a0; f.b1 = (-(1.0f + c)) / a0; f.b2 = f.b0;
        f.a1 = (-2.0f * c) / a0;         f.a2 = (1.0f - alpha) / a0;
        break;
    }
    case 5: { // NOTCH
        float a0 = 1.0f + alpha;
        f.b0 = 1.0f / a0; f.b1 = (-2.0f * c) / a0; f.b2 = 1.0f / a0;
        f.a1 = (-2.0f * c) / a0; f.a2 = (1.0f - alpha) / a0;
        break;
    }
    case 6: { // BANDPASS
        float a0 = 1.0f + alpha;
        f.b0 = (s / 2.0f) / a0; f.b1 = 0.0f; f.b2 = (-s / 2.0f) / a0;
        f.a1 = (-2.0f * c) / a0; f.a2 = (1.0f - alpha) / a0;
        break;
    }
    case 7: { // ALLPASS
        float a0 = 1.0f + alpha;
        f.b0 = (1.0f - alpha) / a0; f.b1 = (-2.0f * c) / a0; f.b2 = (1.0f + alpha) / a0;
        f.a1 = (-2.0f * c) / a0;    f.a2 = (1.0f - alpha) / a0;
        break;
    }
    default: { f.b0 = 1.0f; f.b1 = 0.0f; f.b2 = 0.0f; f.a1 = 0.0f; f.a2 = 0.0f; break; }
    }
    f.z1 = f.z2 = 0.0f;
    return f;
}

BaEQState* eq_create(int bc, uint32_t sr, void*(a)(size_t)) {
    if (bc <= 0 || bc > 16) return nullptr;
    void* m = a(sizeof(BaEQState));
    if (!m) return nullptr;
    auto* eq = new(m) BaEQState{};
    eq->count = bc; eq->sr = sr;
    for (int i = 0; i < bc; ++i) eq->bands[i] = biquad_coeffs(0, 1000.0f, 0.0f, 0.707f, sr);
    return eq;
}

void eq_destroy(BaEQState* eq, void(f)(void*)) { if (!eq) return; eq->~BaEQState(); f(eq); }

void eq_set_band(BaEQState* eq, int band, int type, float freq, float gain_db, float q) {
    if (!eq || band < 0 || band >= eq->count) return;
    if (freq < 10.0f || freq > (float)eq->sr * 0.49f) return;
    if (q < 0.1f) q = 0.707f;
    eq->bands[band] = biquad_coeffs(type, freq, gain_db, q, eq->sr);
}

void eq_process(BaEQState* eq, float* L, float* R, uint32_t N) {
    if (!eq) return;
    for (int b = 0; b < eq->count; ++b) {
        auto& f = eq->bands[b];
        for (uint32_t i = 0; i < N; ++i) {
            float in = L[i];
            float out = f.b0 * in + f.z1;
            f.z1 = f.b1 * in - f.a1 * out + f.z2;
            f.z2 = f.b2 * in - f.a2 * out;
            L[i] = out;
            in = R[i];
            out = f.b0 * in + f.z1;
            f.z1 = f.b1 * in - f.a1 * out + f.z2;
            f.z2 = f.b2 * in - f.a2 * out;
            R[i] = out;
        }
    }
}
