// Copyright (c) 2026 Beas Audio Engineering. All Rights Reserved.
// Proprietary — no license granted.
// Beas Audio DSP Library — CPU timer (portable chrono) + watchdog
#pragma once
#include "internal.h"
uint64_t timer_ns();
void cpu_watchdog(BaAudioContext* ctx);
