// Copyright (c) 2026 Beas Audio Engineering. All Rights Reserved.
// Proprietary — no license granted.
// Beas Audio DSP Library — FFT wrapper (pffft)
#include "fft.h"
#include "pffft.h"
#include <cstring>

void fft_conv_init(BaAudioContext* ctx) {
    ctx->fft_size = 2 * (int)ctx->buffer_size;

    // Main FFT plan
    ctx->pffft_setup = pffft_new_setup(ctx->fft_size, PFFFT_REAL);
    ctx->pffft_256   = pffft_new_setup(BA_CONV_FFT_SIZE, PFFFT_REAL);

    // Pre-alloc aligned FFT workspace — in-place forward transform
    // writes 2*(N/2+1) packed bins (N real → N+2 packed), so allocate N+2
    ctx->fft_workspace_X   = (float*)pffft_aligned_malloc((ctx->fft_size + 2) * sizeof(float));
    ctx->fft_workspace_Y_L = (float*)pffft_aligned_malloc((ctx->fft_size + 2) * sizeof(float));
    ctx->fft_workspace_Y_R = (float*)pffft_aligned_malloc((ctx->fft_size + 2) * sizeof(float));
    ctx->overlap_L         = (float*)pffft_aligned_malloc(ctx->fft_size * sizeof(float));
    ctx->overlap_R         = (float*)pffft_aligned_malloc(ctx->fft_size * sizeof(float));
    ctx->pffft_scratch     = (float*)pffft_aligned_malloc((ctx->fft_size + 2) * sizeof(float));

    std::memset(ctx->overlap_L, 0, ctx->fft_size * sizeof(float));
    std::memset(ctx->overlap_R, 0, ctx->fft_size * sizeof(float));

    // Init convolution slots
    std::memset(ctx->conv_slots, 0, BA_MAX_CONV_SLOTS * sizeof(BaConvSlot));
    for (int i = 0; i < BA_MAX_CONV_SLOTS; ++i) {
        ctx->conv_slots[i].fdl_head = 0;
        ctx->conv_slots[i].P = 0;
        ctx->conv_slots[i].active = false;
    }
}
