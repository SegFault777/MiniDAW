// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "AudioOps.h"

#include <algorithm>
#include <cstring>

namespace dsp {

std::vector<float> reverseFrames(const float* pcm, std::size_t frames) {
    std::vector<float> out(frames * 2);
    for (std::size_t i = 0; i < frames; ++i) {
        const std::size_t s = frames - 1 - i;
        out[2 * i] = pcm[2 * s];
        out[2 * i + 1] = pcm[2 * s + 1];
    }
    return out;
}

float peakOf(const float* pcm, std::size_t frames) {
    float pk = 0;
    for (std::size_t i = 0; i < frames * 2; ++i) pk = std::max(pk, std::fabs(pcm[i]));
    return pk;
}

float normalizeGain(float peak, float targetDb) {
    if (peak < 1e-6f) return 1.f;
    return dbToLinear(targetDb) / peak;
}

void applyRecordingOffset(std::vector<float>& stereo, long offsetFrames) {
    const long frames = long(stereo.size() / 2);
    if (offsetFrames > 0) {
        const long drop = std::min(offsetFrames, std::max(0L, frames - 1));   // keep at least one frame
        stereo.erase(stereo.begin(), stereo.begin() + std::ptrdiff_t(drop * 2));
    } else if (offsetFrames < 0) {
        stereo.insert(stereo.begin(), std::size_t(-offsetFrames) * 2, 0.f);
    }
}

namespace {
void put16(std::vector<std::uint8_t>& v, std::uint16_t x) { v.push_back(std::uint8_t(x)); v.push_back(std::uint8_t(x >> 8)); }
void put32(std::vector<std::uint8_t>& v, std::uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back(std::uint8_t(x >> (8 * i))); }
void putTag(std::vector<std::uint8_t>& v, const char* t) { v.insert(v.end(), t, t + 4); }

inline float nextRand(std::uint32_t& s) {   // uniform in [-0.5, 0.5)
    s = s * 1664525u + 1013904223u;
    return float(s >> 8) / 16777216.f - 0.5f;
}
}  // namespace

std::vector<std::uint8_t> wavHeader(WavFormat f, int sampleRate, std::uint64_t frames) {
    const std::uint32_t bps = std::uint32_t(bytesPerSample(f));
    const std::uint64_t dataBytes64 = frames * 2 * bps;
    const std::uint32_t dataBytes = dataBytes64 > 0xFFFFFFF0ull ? 0xFFFFFFF0u : std::uint32_t(dataBytes64);
    const bool isFloat = f == WavFormat::Float32;
    const std::uint32_t factSize = isFloat ? 12 : 0;   // "fact" chunk: 8 bytes header + 4 bytes
    std::vector<std::uint8_t> h;
    putTag(h, "RIFF");
    put32(h, 4 + (8 + 16) + factSize + (8 + dataBytes));
    putTag(h, "WAVE");
    putTag(h, "fmt ");
    put32(h, 16);
    put16(h, isFloat ? 3 : 1);
    put16(h, 2);
    put32(h, std::uint32_t(sampleRate));
    put32(h, std::uint32_t(sampleRate) * 2 * bps);
    put16(h, std::uint16_t(2 * bps));
    put16(h, std::uint16_t(bps * 8));
    if (isFloat) {
        putTag(h, "fact");
        put32(h, 4);
        put32(h, std::uint32_t(frames));
    }
    putTag(h, "data");
    put32(h, dataBytes);
    return h;
}

void encodeSamples(const float* in, std::size_t frames, WavFormat f, float gain, bool dither, std::uint32_t& seed,
                   std::vector<std::uint8_t>& out) {
    const std::size_t n = frames * 2;
    out.reserve(out.size() + n * std::size_t(bytesPerSample(f)));
    for (std::size_t i = 0; i < n; ++i) {
        const float x = in[i] * gain;
        switch (f) {
        case WavFormat::Float32: {
            std::uint8_t b[4];
            std::memcpy(b, &x, 4);   // the file is little-endian, like every supported platform
            out.insert(out.end(), b, b + 4);
            break;
        }
        case WavFormat::Pcm24: {
            const double v = std::clamp(double(x), -1.0, 1.0) * 8388607.0;
            const std::int32_t q = std::int32_t(std::lround(v));
            out.push_back(std::uint8_t(q));
            out.push_back(std::uint8_t(q >> 8));
            out.push_back(std::uint8_t(q >> 16));
            break;
        }
        case WavFormat::Pcm16: {
            float v = std::clamp(x, -1.f, 1.f) * 32767.f;
            if (dither) v += nextRand(seed) + nextRand(seed);   // TPDF, +-1 LSB
            const int q = int(std::clamp(std::lround(v), -32768L, 32767L));
            put16(out, std::uint16_t(std::int16_t(q)));
            break;
        }
        }
    }
}

}  // namespace dsp
