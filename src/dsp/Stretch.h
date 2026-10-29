// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <vector>

namespace dsp {

/// Time-stretches interleaved stereo audio with WSOLA. stretch > 1 makes the result longer.
std::vector<float> timeStretch(const float* in, size_t frames, double stretch);

/// Linear resampler: rate > 1 plays faster (shorter, higher pitch).
std::vector<float> resample(const std::vector<float>& in, double rate);

/// Changes the duration by 'durationRatio' (output / input length) and transposes by 'semitones'
/// without changing the duration any further.
std::vector<float> flex(const float* in, size_t frames, double durationRatio, double semitones);

}  // namespace dsp
