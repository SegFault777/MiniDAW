// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QAudioFormat>
#include <QFile>
#include <QString>
#include <cmath>
#include <vector>
#include "Engine.h"

/// Streaming linear resampler that stays continuous across chunk boundaries.
struct Resampler {
    double pos = 0;
    float last[2] = {0, 0};

    /// Converts interleaved stereo float from srcRate to kSampleRate and appends it to out.
    void process(const std::vector<float>& in, int srcRate, std::vector<float>& out) {
        const int n = int(in.size() / 2);
        if (n == 0) return;
        if (srcRate == kSampleRate || srcRate <= 0) {
            out.insert(out.end(), in.begin(), in.end());
            return;
        }
        const double ratio = double(srcRate) / kSampleRate;
        auto get = [&](int i, int c) { return i < 0 ? last[c] : in[size_t(2 * i + c)]; };
        while (pos < n - 1) {
            const int i0 = int(std::floor(pos));
            const float fr = float(pos - i0);
            for (int c = 0; c < 2; ++c) out.push_back(get(i0, c) * (1.f - fr) + get(i0 + 1, c) * fr);
            pos += ratio;
        }
        pos -= n;  // sample n-1 becomes index -1 of the next chunk
        last[0] = in[size_t(2 * (n - 1))];
        last[1] = in[size_t(2 * (n - 1) + 1)];
    }
};

/// Converts raw device / decoder samples (any channel count and sample format) to stereo float.
bool toStereoFloat(const void* data, int frames, const QAudioFormat& fmt, std::vector<float>& out);

/// Fills AudioData::peaks from AudioData::pcm.
void computePeaks(AudioData& d);

/// Writes the 44-byte PCM WAV header (16-bit, stereo, 44.1 kHz).
void writeWavHeader(QFile& f, quint32 dataBytes);

/// Writes interleaved stereo float samples as a 16-bit WAV file.
bool writeWavFile(const QString& path, const std::vector<float>& pcm, QString* error);
