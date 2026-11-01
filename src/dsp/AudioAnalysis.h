// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <utility>
#include <vector>

namespace dsp {

/// Integrated loudness in LUFS (ITU-R BS.1770 / EBU R128: K-weighting, 400 ms blocks with 75 % overlap, absolute
/// gate -70 LUFS and relative gate -10 LU) of interleaved 44.1 kHz stereo audio. Returns kSilentLufs for silence or
/// audio shorter than 400 ms.
constexpr double kSilentLufs = -120.0;
double integratedLoudness(const float* stereo, size_t frames);

/// Sections of interleaved 44.1 kHz stereo audio that are louder than 'thresholdDb' (dBFS peak per 5 ms window),
/// like Logic's Strip Silence. Quiet gaps shorter than 'minSilenceFrames' are kept inside a section; every section
/// is widened by 'padFrames' on both sides. Returns [begin, end) frame ranges, sorted and non-overlapping.
std::vector<std::pair<size_t, size_t>> findSoundRanges(const float* stereo, size_t frames, double thresholdDb,
                                                       size_t minSilenceFrames, size_t padFrames);

}  // namespace dsp
