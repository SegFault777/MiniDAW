// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "TempoDetect.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace dsp {

double detectTempo(const float* st, size_t frames, double minBpm, double maxBpm) {
    constexpr double kRate = 44100.0;
    constexpr size_t kHop = 256;
    if (!st || frames < size_t(kRate * 3) || !(minBpm > 0) || !(maxBpm > minBpm)) return 0;

    // Energy of the high-passed (first difference) mono signal per hop.
    const size_t n = frames / kHop;
    std::vector<double> env(n, 0.0);
    float prev = 0;
    for (size_t k = 0; k < n; ++k) {
        double e = 0;
        for (size_t i = k * kHop; i < (k + 1) * kHop; ++i) {
            const float x = 0.5f * (st[2 * i] + st[2 * i + 1]);
            const double d = double(x) - prev;
            prev = x;
            e += d * d;
        }
        env[k] = std::sqrt(e / double(kHop));
    }
    double top = 0;
    for (double v : env) top = std::max(top, v);
    if (top < 1e-6) return 0;   // silence

    // Onset strength: rises of the log-compressed energy.
    std::vector<double> onset(n, 0.0);
    for (size_t k = 1; k < n; ++k) onset[k] = std::max(0.0, std::log1p(200.0 * env[k] / top) - std::log1p(200.0 * env[k - 1] / top));
    double mean = 0;
    for (double v : onset) mean += v;
    mean /= double(n);
    for (double& v : onset) v -= mean;

    const double frameRate = kRate / double(kHop);
    // Search a wide range of lags so that 2x and 4x multiples of the beat can be added to the score.
    auto acf = [&](size_t lag) {
        if (lag >= n / 2) return 0.0;
        double s = 0;
        for (size_t k = 0; k + lag < n; ++k) s += onset[k] * onset[k + lag];
        return s / double(n - lag);
    };
    const size_t lagMin = size_t(std::floor(60.0 / (maxBpm * 2.0) * frameRate));   // allow folding from double tempo
    const size_t lagMax = size_t(std::ceil(60.0 / (minBpm / 2.0) * frameRate));
    std::vector<double> a(lagMax * 4 + 8, 0.0);
    for (size_t l = std::max<size_t>(lagMin / 2, 2); l < a.size(); ++l) a[l] = acf(l);
    double var = acf(0) > 0 ? acf(0) : 1.0;

    // Comb: a beat period also repeats at 2x and 4x its length.
    std::vector<double> score(lagMax + 1, 0.0);
    double best = 0;
    size_t bestLag = 0;
    for (size_t l = lagMin; l <= lagMax; ++l) {
        double s = a[l] + 0.5 * a[2 * l] + 0.25 * a[4 * l];
        const double bpm = 60.0 * frameRate / double(l);
        const double prior = std::exp(-0.5 * std::pow(std::log2(bpm / 120.0) / 1.2, 2.0));   // mild preference for common tempi
        s *= prior;
        score[l] = s;
        if (s > best) { best = s; bestLag = l; }
    }
    if (bestLag == 0 || best < 0.02 * var) return 0;   // no clear pulse

    // Parabolic interpolation around the best lag for a fractional period.
    double lag = double(bestLag);
    if (bestLag > lagMin && bestLag < lagMax) {
        const double y0 = a[bestLag - 1], y1 = a[bestLag], y2 = a[bestLag + 1];
        const double den = y0 - 2 * y1 + y2;
        if (std::fabs(den) > 1e-12) lag += std::clamp(0.5 * (y0 - y2) / den, -1.0, 1.0);
    }
    double bpm = 60.0 * frameRate / lag;
    while (bpm >= maxBpm) bpm *= 0.5;
    while (bpm < minBpm) bpm *= 2.0;
    return bpm;
}

}  // namespace dsp
