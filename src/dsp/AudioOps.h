// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

/// Qt-free helpers for region fades / gain, recording offset and WAV export (v0.5).
namespace dsp {

// ------------------------------------------------------------------ gain and fades
inline float dbToLinear(float db) { return std::pow(10.f, db / 20.f); }
inline float linearToDb(float lin) { return lin <= 1e-9f ? -180.f : 20.f * std::log10(lin); }

/// Gain of a region fade at frame 'rel' (0 = first frame of the region) out of 'total' frames.
/// Fades are raised-cosine curves; when both fades would overlap they are shortened proportionally.
inline float fadeGain(std::int64_t rel, std::int64_t total, std::int64_t fadeIn, std::int64_t fadeOut) {
    if (fadeIn <= 0 && fadeOut <= 0) return 1.f;
    if (fadeIn + fadeOut > total && fadeIn + fadeOut > 0) {
        const double k = double(total) / double(fadeIn + fadeOut);
        fadeIn = std::int64_t(double(fadeIn) * k);
        fadeOut = std::int64_t(double(fadeOut) * k);
    }
    float g = 1.f;
    if (fadeIn > 0 && rel < fadeIn) {
        const float x = float(rel) / float(fadeIn);
        const float s = std::sin(1.57079632679f * x);
        g *= s * s;
    }
    const std::int64_t fromEnd = total - 1 - rel;
    if (fadeOut > 0 && fromEnd < fadeOut) {
        const float x = float(fromEnd) / float(fadeOut);
        const float s = std::sin(1.57079632679f * x);
        g *= s * s;
    }
    return g;
}

// ------------------------------------------------------------------ audio helpers (interleaved stereo)
/// Returns a time-reversed copy of 'frames' stereo frames starting at 'pcm'.
std::vector<float> reverseFrames(const float* pcm, std::size_t frames);

/// Largest absolute sample value of 'frames' stereo frames.
float peakOf(const float* pcm, std::size_t frames);

/// Linear gain that brings 'peak' to 'targetDb' (dBFS). Returns 1 when the audio is silent.
float normalizeGain(float peak, float targetDb);

/// Recording latency compensation. A positive offset moves the take earlier (drops that many frames
/// from its start), a negative offset moves it later (pads silence). Never removes the whole take.
void applyRecordingOffset(std::vector<float>& stereo, long offsetFrames);

// ------------------------------------------------------------------ WAV export
enum class WavFormat { Pcm16, Pcm24, Float32 };

inline int bytesPerSample(WavFormat f) { return f == WavFormat::Pcm16 ? 2 : f == WavFormat::Pcm24 ? 3 : 4; }

/// RIFF/WAVE header for a stereo file of 'frames' frames (PCM tag for integer formats, IEEE float tag
/// plus a "fact" chunk for Float32).
std::vector<std::uint8_t> wavHeader(WavFormat f, int sampleRate, std::uint64_t frames);

/// Converts interleaved stereo float to the sample format and appends the bytes to 'out'.
/// 'gain' is applied first. With 'dither' 16-bit output uses TPDF dither (state in 'seed').
/// Integer formats are clipped to full scale.
void encodeSamples(const float* in, std::size_t frames, WavFormat f, float gain, bool dither, std::uint32_t& seed,
                   std::vector<std::uint8_t>& out);

}  // namespace dsp
