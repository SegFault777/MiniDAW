// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Stretch.h"

#include <algorithm>
#include <cmath>

namespace dsp {

std::vector<float> timeStretch(const float* in, size_t frames, double stretch) {
    if (frames == 0) return {};
    stretch = std::clamp(stretch, 0.25, 4.0);
    if (std::fabs(stretch - 1.0) < 1e-3) return std::vector<float>(in, in + frames * 2);

    constexpr int N = 2048, H = N / 2, kTol = 384;   // window, hop and search tolerance (samples)
    const size_t outFrames = size_t(std::llround(double(frames) * stretch));
    const size_t pad = size_t(N + kTol);

    // Pad both ends with silence so every candidate window stays in range.
    std::vector<float> src((frames + 2 * pad) * 2, 0.f);
    std::copy(in, in + frames * 2, src.begin() + std::ptrdiff_t(pad * 2));
    std::vector<float> mono(frames + 2 * pad);
    for (size_t i = 0; i < mono.size(); ++i) mono[i] = 0.5f * (src[2 * i] + src[2 * i + 1]);

    std::vector<float> win(N);
    for (int i = 0; i < N; ++i) win[size_t(i)] = 0.5f * (1.f - float(std::cos(2.0 * 3.14159265358979 * i / N)));

    std::vector<float> out((outFrames + N) * 2, 0.f);
    const double ha = double(H) / stretch;   // analysis hop
    const long maxStart = long(mono.size()) - N - 1;
    long prev = long(pad);

    for (size_t m = 0; m * H < outFrames; ++m) {
        const long ideal = long(pad) + long(std::llround(double(m) * ha));
        long pos = std::clamp(ideal, 0L, maxStart);
        if (m > 0) {
            // Pick the candidate whose start best continues the previous segment's natural successor.
            const long ref = prev + H;
            const long lo = std::max(0L, pos - kTol), hi = std::min(maxStart, pos + kTol);
            double best = -1e30;
            long bestPos = pos;
            for (long c = lo; c <= hi; c += 2) {
                double s = 0, e = 1e-9;
                for (int i = 0; i < H; i += 4) {
                    s += double(mono[size_t(ref + i)]) * mono[size_t(c + i)];
                    e += double(mono[size_t(c + i)]) * mono[size_t(c + i)];
                }
                const double score = s / std::sqrt(e);
                if (score > best) { best = score; bestPos = c; }
            }
            pos = bestPos;
        }
        const size_t o = m * H;
        for (int i = 0; i < N; ++i) {
            const float w = (m == 0 && i < H) ? 1.f : win[size_t(i)];   // no fade-in on the very first segment
            out[(o + size_t(i)) * 2] += src[size_t(pos + i) * 2] * w;
            out[(o + size_t(i)) * 2 + 1] += src[size_t(pos + i) * 2 + 1] * w;
        }
        prev = pos;
    }
    out.resize(outFrames * 2);
    return out;
}

std::vector<float> resample(const std::vector<float>& in, double rate) {
    const size_t n = in.size() / 2;
    if (n < 2 || std::fabs(rate - 1.0) < 1e-6) return in;
    const size_t outN = size_t(double(n) / rate);
    std::vector<float> out(outN * 2);
    for (size_t i = 0; i < outN; ++i) {
        const double x = double(i) * rate;
        const size_t i0 = std::min(size_t(x), n - 1), i1 = std::min(i0 + 1, n - 1);
        const float fr = float(x - double(i0));
        for (int c = 0; c < 2; ++c) out[2 * i + size_t(c)] = in[2 * i0 + size_t(c)] * (1.f - fr) + in[2 * i1 + size_t(c)] * fr;
    }
    return out;
}

std::vector<float> flex(const float* in, size_t frames, double durationRatio, double semitones) {
    const double p = std::pow(2.0, semitones / 12.0);
    std::vector<float> stretched = timeStretch(in, frames, durationRatio * p);
    return std::fabs(semitones) < 0.01 ? stretched : resample(stretched, p);
}

}  // namespace dsp
