// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Qt-free audio helpers: waveform peaks, resampling and WAV files.
#include <string>
#include <vector>
#include "Model.h"

namespace core {

/// Streaming linear resampler that stays continuous across chunk boundaries.
struct Resampler {
    double pos = 0;
    float last[2] = {0, 0};

    /// Converts interleaved stereo float from srcRate to kSampleRate and appends it to out.
    void process(const std::vector<float>& in, int srcRate, std::vector<float>& out);
};

/// Fills AudioData::peaks from AudioData::pcm.
void computePeaks(AudioData& d);

/// Writes interleaved stereo float samples as a 16-bit 44.1 kHz WAV file.
bool writeWavFile(const std::string& path, const std::vector<float>& pcm, std::string* error);

/// Reads a RIFF/WAVE file (8/16/24/32-bit PCM or 32-bit float, any channel count and sample rate) as
/// 44.1 kHz stereo float. Other formats (MP3, FLAC, ...) are decoded by the platform layer.
bool readWavFile(const std::string& path, std::vector<float>& pcm, std::string* error);

}  // namespace core
