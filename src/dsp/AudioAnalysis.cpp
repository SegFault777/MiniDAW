// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "AudioAnalysis.h"

#include <algorithm>
#include <cmath>

namespace dsp {

namespace {
constexpr double kRate = 44100.0;
constexpr double kPi = 3.14159265358979323846;

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double run(double x) {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// The two K-weighting stages of BS.1770 for any sample rate.
Biquad shelf() {
    const double f0 = 1681.974450955533, g = 3.999843853973347, q = 0.7071752369554196;
    const double k = std::tan(kPi * f0 / kRate), vh = std::pow(10.0, g / 20.0), vb = std::pow(vh, 0.4996667741545416);
    const double a0 = 1.0 + k / q + k * k;
    Biquad b;
    b.b0 = (vh + vb * k / q + k * k) / a0;
    b.b1 = 2.0 * (k * k - vh) / a0;
    b.b2 = (vh - vb * k / q + k * k) / a0;
    b.a1 = 2.0 * (k * k - 1.0) / a0;
    b.a2 = (1.0 - k / q + k * k) / a0;
    return b;
}

Biquad highpass() {
    const double f0 = 38.13547087602444, q = 0.5003270373238773;
    const double k = std::tan(kPi * f0 / kRate);
    const double a0 = 1.0 + k / q + k * k;
    Biquad b;
    b.b0 = 1.0; b.b1 = -2.0; b.b2 = 1.0;
    b.a1 = 2.0 * (k * k - 1.0) / a0;
    b.a2 = (1.0 - k / q + k * k) / a0;
    return b;
}
}  // namespace

double integratedLoudness(const float* st, size_t frames) {
    const size_t block = size_t(0.4 * kRate), hop = size_t(0.1 * kRate);
    if (!st || frames < block) return kSilentLufs;
    // Per-sample K-weighted power (summed over both channels, channel weights are 1).
    std::vector<double> power(frames);
    Biquad s[2] = {shelf(), shelf()}, h[2] = {highpass(), highpass()};
    for (size_t i = 0; i < frames; ++i) {
        double p = 0;
        for (int c = 0; c < 2; ++c) {
            const double y = h[c].run(s[c].run(double(st[2 * i + size_t(c)])));
            p += y * y;
        }
        power[i] = p;
    }
    std::vector<double> prefix(frames + 1, 0.0);
    for (size_t i = 0; i < frames; ++i) prefix[i + 1] = prefix[i] + power[i];
    std::vector<double> blocks;
    for (size_t at = 0; at + block <= frames; at += hop) blocks.push_back((prefix[at + block] - prefix[at]) / double(block));
    auto lufs = [](double ms) { return -0.691 + 10.0 * std::log10(ms); };
    double sum = 0; size_t n = 0;
    for (double b : blocks) if (b > 0 && lufs(b) > -70.0) { sum += b; ++n; }
    if (n == 0) return kSilentLufs;
    const double relGate = lufs(sum / double(n)) - 10.0;
    sum = 0; n = 0;
    for (double b : blocks) if (b > 0 && lufs(b) > relGate) { sum += b; ++n; }
    if (n == 0) return kSilentLufs;
    return lufs(sum / double(n));
}

std::vector<std::pair<size_t, size_t>> findSoundRanges(const float* st, size_t frames, double thresholdDb, size_t minSilenceFrames, size_t padFrames) {
    std::vector<std::pair<size_t, size_t>> out;
    if (!st || frames == 0) return out;
    const size_t win = std::max<size_t>(1, size_t(0.005 * kRate));
    const float thr = float(std::pow(10.0, thresholdDb / 20.0));
    std::vector<std::pair<size_t, size_t>> raw;   // windows above the threshold, merged when the gap is short
    for (size_t at = 0; at < frames; at += win) {
        const size_t end = std::min(frames, at + win);
        float peak = 0;
        for (size_t i = at; i < end; ++i) peak = std::max({peak, std::fabs(st[2 * i]), std::fabs(st[2 * i + 1])});
        if (peak < thr) continue;
        if (!raw.empty() && at - raw.back().second < std::max<size_t>(minSilenceFrames, 1)) raw.back().second = end;
        else raw.emplace_back(at, end);
    }
    for (auto [b, e] : raw) {
        b = b > padFrames ? b - padFrames : 0;
        e = std::min(frames, e + padFrames);
        if (!out.empty() && b <= out.back().second) out.back().second = std::max(out.back().second, e);
        else out.emplace_back(b, e);
    }
    return out;
}

}  // namespace dsp
