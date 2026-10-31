// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>

namespace dsp {

/// Estimates the tempo of a rhythmic recording (interleaved 44.1 kHz stereo float): onset strength curve,
/// autocorrelation with a comb over the beat multiples and a mild preference for common tempi. The result is
/// folded into [minBpm, maxBpm) by doubling or halving. Returns 0 when the audio is too short (< 3 s), silent
/// or has no clear pulse.
double detectTempo(const float* stereo, size_t frames, double minBpm = 70.0, double maxBpm = 170.0);

}  // namespace dsp
