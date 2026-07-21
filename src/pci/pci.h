// Copyright (c) 2026 Beas Audio Engineering. All Rights Reserved.
// Proprietary — no license granted.
// Beas Audio DSP Library — PCI: object control & prioritization
#pragma once
#include "internal.h"
void pci_two_pass(BaAudioContext* ctx, const BaOSPos* positions,
                  const BaAudioObject* objects, uint32_t N_objects);
float hash_grid_distinctness(BaAudioContext* ctx, int object_idx,
                             const BaOSPos* positions, uint32_t N_objects);
float hash_grid_nearest_any(BaAudioContext* ctx, int object_idx,
                            const BaOSPos* positions, uint32_t N_objects);
