// Copyright (c) 2026 Beas Audio Engineering. All Rights Reserved.
// Proprietary — no license granted.
// Beas Audio DSP Library — flush & deferred control checks
#pragma once
#include "internal.h"
void check_pending_flush(BaAudioContext* ctx);
void check_pending_tier(BaAudioContext* ctx);
void check_pending_path(BaAudioContext* ctx);
void check_pending_mode(BaAudioContext* ctx);
