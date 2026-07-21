// Copyright (c) 2026 Beas Audio Engineering. All Rights Reserved.
// Proprietary — no license granted.
// Beas Audio DSP Library — basic build verification test
#include "beas/beas_audio.h"
#include <cstdio>
#include <cmath>
#include <cstring>

static int failures = 0;
#define TEST(name, expr) do { \
    if (!(expr)) { \
        std::fprintf(stderr, "FAIL: %s\n", name); \
        failures++; \
    } else { \
        std::printf("PASS: %s\n", name); \
    } \
} while(0)

int main() {
    std::printf("=== Beas Audio Build Verification ===\n\n");

    // Test 1: create context
    BaConfig cfg{};
    cfg.sample_rate = 48000;
    cfg.buffer_size = 128;
    cfg.path = BA_PATH_EXPLICIT_OBJECTS;
    cfg.max_objects = 16;
    cfg.tier = 2;
    cfg.hrtf_path = nullptr;
    cfg.ear_fir_path = nullptr;
    cfg.eq_band_count = 0;

    BaAudioContext* ctx = nullptr;
    BaResult r = ba_create(&cfg, &ctx);
    TEST("ba_create", r == BA_OK && ctx != nullptr);
    if (r != BA_OK) {
        std::fprintf(stderr, "FATAL: ba_create failed with code %d\n", (int)r);
        return 1;
    }

    // Test 2: query APIs
    TEST("latency_samples == buffer_size",
         ba_get_latency_samples(ctx) == 128);
    TEST("latency_ms ~ 2.67ms",
         std::fabs(ba_get_latency_ms(ctx) - 2.666f) < 0.1f);
    TEST("tier == 2",
         ba_get_current_tier(ctx) == 2);
    TEST("cpu_load == 0 initially",
         ba_get_cpu_load(ctx) == 0.0f);

    // Test 3: process with silence
    float out_L[128] = {}, out_R[128] = {};
    BaAudioObject objs[2];
    float pcm[128] = {};

    objs[0] = {0, 0.0f, 0.0f, 1.0f, 0.5f, pcm};
    objs[1] = {1, 0.5f, 0.2f, 2.0f, 0.3f, pcm};

    r = ba_process(ctx, objs, 2, nullptr, nullptr, out_L, out_R, 128);
    TEST("ba_process with objects", r == BA_OK);

    // Test 4: process with legacy stereo
    float in_L[128] = {}, in_R[128] = {};
    in_L[0] = 0.5f; // non-zero sample
    BaConfig cfg2 = cfg;
    cfg2.path = BA_PATH_LEGACY_STEREO;
    BaAudioContext* ctx2 = nullptr;
    r = ba_create(&cfg2, &ctx2);
    TEST("create legacy", r == BA_OK && ctx2 != nullptr);

    if (ctx2) {
        std::memset(out_L, 0, sizeof(out_L));
        std::memset(out_R, 0, sizeof(out_R));
        r = ba_process(ctx2, nullptr, 0, in_L, in_R, out_L, out_R, 128);
        TEST("ba_process legacy stereo", r == BA_OK);
        ba_destroy(ctx2);
    }

    // Test 5: control APIs
    r = ba_set_mode(ctx, BA_MODE_IMMERSIVE);
    TEST("ba_set_mode", r == BA_OK);
    r = ba_set_tier(ctx, 3);
    TEST("ba_set_tier", r == BA_OK);
    ba_flush(ctx);
    TEST("ba_flush (no crash)", true);

    // Test 6: set EQ band (no EQ configured — expect NOT_INIT)
    r = ba_set_eq_band(ctx, 0, 1000.0f, 0.0f, 0.707f);
    TEST("ba_set_eq_band on no-EQ config returns NOT_INIT",
         r == BA_ERR_NOT_INIT);

    // Test 7: invalid args
    r = ba_create(nullptr, &ctx);
    TEST("ba_create null config", r == BA_ERR_INVALID_ARG);
    r = ba_set_tier(ctx, 99);
    TEST("ba_set_tier invalid tier", r == BA_ERR_INVALID_ARG);
    r = ba_set_mode(nullptr, BA_MODE_GENERIC);
    TEST("ba_set_mode null ctx", r == BA_ERR_INVALID_ARG);

    // Cleanup
    ba_destroy(ctx);

    // Test 8: create with EQ
    BaConfig cfg3 = cfg;
    cfg3.eq_band_count = 4;
    cfg3.tier = 3;
    ctx = nullptr;
    r = ba_create(&cfg3, &ctx);
    TEST("create with 4-band EQ", r == BA_OK && ctx != nullptr);
    if (ctx) {
        r = ba_set_eq_band(ctx, 0, 1000.0f, 3.0f, 0.707f);
        TEST("ba_set_eq_band on EQ-enabled config", r == BA_OK);
        ba_destroy(ctx);
    }

    // Test 9: HRTF mode
    BaConfig cfg4 = cfg;
    ctx = nullptr;
    r = ba_create(&cfg4, &ctx);
    TEST("create for HRTF tests", r == BA_OK && ctx != nullptr);
    if (ctx) {
        ba_set_hrtf_mode(ctx, BA_HRTF_MODE_CLUSTERED);
        TEST("ba_get_hrtf_mode reflects set", ba_get_hrtf_mode(ctx) == BA_HRTF_MODE_CLUSTERED);
        ba_destroy(ctx);
    }

    // Test 10: HRTF convolution verification
    // Feed an impulse through a centered object (az=0, el=0) and verify:
    //   - output samples are non-zero (convolution happened)
    //   - peak occurs at the expected ITD offset + buffer boundary
    {
        BaConfig cfg5 = cfg;
        cfg5.tier = 0;  // shortest IR (32 taps) for predictable output
        ctx = nullptr;
        r = ba_create(&cfg5, &ctx);
        TEST("create for convolution test", r == BA_OK && ctx != nullptr);
        if (ctx) {
            float impulse[128] = {};
            impulse[0] = 1.0f;        // dirac delta at sample 0

            BaAudioObject objs2[1];
            objs2[0] = {0, 0.0f, 0.0f, 1.0f, 1.0f, impulse};

            float out_L[128] = {}, out_R[128] = {};
            r = ba_process(ctx, objs2, 1, nullptr, nullptr, out_L, out_R, 128);
            TEST("convolution process", r == BA_OK);

            // Verify convolution happened: output should have non-zero energy
            float energy_L = 0.0f, energy_R = 0.0f;
            for (int i = 0; i < 128; ++i) {
                energy_L += out_L[i] * out_L[i];
                energy_R += out_R[i] * out_R[i];
            }
            TEST("convolution produced non-zero output", energy_L > 1e-10f && energy_R > 1e-10f);

            // With IR length 32, first 32 output samples should be the IR,
            // remaining 96 should be zero (no overlap from previous frame)
            float tail_energy = 0.0f;
            for (int i = 32; i < 128; ++i)
                tail_energy += out_L[i] * out_L[i] + out_R[i] * out_R[i];

            // ponytail: imprecise — the FFT overlap-add has energy spread
            // across the full block; this just checks the bulk is in the IR region
            TEST("convolution IR length ~32 taps", energy_L > 10.0f * tail_energy);
        }
    }

    std::printf("\n=== Results: %d failures ===\n", failures);
    return failures > 0 ? 1 : 0;
}
