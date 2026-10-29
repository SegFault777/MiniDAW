// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "TempoMap.h"

#include <algorithm>
#include <cmath>

namespace dsp {
namespace {
constexpr double kSr = 44100.0;

// Frames needed to travel 'd' beats from the start of a segment.
double framesIn(double bpm0, double k, double d) {
    if (std::fabs(k) < 1e-9) return d * 60.0 * kSr / bpm0;
    return kSr * 60.0 / k * std::log((bpm0 + k * d) / bpm0);
}

// Beats travelled in 'frames' frames from the start of a segment (inverse of framesIn).
double beatsIn(double bpm0, double k, double frames) {
    if (std::fabs(k) < 1e-9) return frames * bpm0 / (60.0 * kSr);
    return bpm0 * (std::exp(k * frames / (60.0 * kSr)) - 1.0) / k;
}

bool validDen(int d) { return d == 1 || d == 2 || d == 4 || d == 8 || d == 16 || d == 32; }
}  // namespace

void TempoMap::setTempo(std::vector<TempoPoint> p) {
    m_tempo = std::move(p);
    rebuild();
}

void TempoMap::setMeter(std::vector<MeterPoint> p) {
    m_meter = std::move(p);
    rebuild();
}

void TempoMap::rebuild() {
    if (m_tempo.empty()) m_tempo.push_back({});
    std::sort(m_tempo.begin(), m_tempo.end(), [](const TempoPoint& a, const TempoPoint& b) { return a.beat < b.beat; });
    m_tempo.front().beat = 0;
    for (TempoPoint& t : m_tempo) t.bpm = std::clamp(t.bpm, 20.0, 400.0);
    for (size_t i = 1; i < m_tempo.size();)   // drop points that share a position
        if (m_tempo[i].beat - m_tempo[i - 1].beat < 1e-6) m_tempo.erase(m_tempo.begin() + std::ptrdiff_t(i));
        else ++i;
    m_tempo.back().ramp = false;

    m_ts.clear();
    double frame = 0;
    for (size_t i = 0; i < m_tempo.size(); ++i) {
        const TempoPoint& t = m_tempo[i];
        double k = 0;
        if (t.ramp && i + 1 < m_tempo.size()) k = (m_tempo[i + 1].bpm - t.bpm) / (m_tempo[i + 1].beat - t.beat);
        m_ts.push_back({t.beat, t.bpm, k, frame});
        if (i + 1 < m_tempo.size()) frame += framesIn(t.bpm, k, m_tempo[i + 1].beat - t.beat);
    }

    if (m_meter.empty()) m_meter.push_back({});
    std::sort(m_meter.begin(), m_meter.end(), [](const MeterPoint& a, const MeterPoint& b) { return a.bar < b.bar; });
    m_meter.front().bar = 1;
    for (MeterPoint& m : m_meter) {
        m.num = std::clamp(m.num, 1, 32);
        if (!validDen(m.den)) m.den = 4;
    }
    for (size_t i = 1; i < m_meter.size();)
        if (m_meter[i].bar <= m_meter[i - 1].bar) m_meter.erase(m_meter.begin() + std::ptrdiff_t(i));
        else ++i;
    m_ms.clear();
    double beat = 0;
    for (size_t i = 0; i < m_meter.size(); ++i) {
        const MeterPoint& m = m_meter[i];
        m_ms.push_back({m.bar, beat, m.num, m.den});
        if (i + 1 < m_meter.size()) beat += (m_meter[i + 1].bar - m.bar) * m.num * 4.0 / m.den;
    }
}

double TempoMap::bpmAt(double beat) const {
    auto it = std::upper_bound(m_ts.begin(), m_ts.end(), beat, [](double b, const TSeg& s) { return b < s.beat0; });
    const TSeg& s = (it == m_ts.begin()) ? m_ts.front() : *(it - 1);
    return s.bpm0 + s.k * std::max(0.0, beat - s.beat0);
}

double TempoMap::beatToFrame(double beat) const {
    beat = std::max(0.0, beat);
    auto it = std::upper_bound(m_ts.begin(), m_ts.end(), beat, [](double b, const TSeg& s) { return b < s.beat0; });
    const TSeg& s = *(it - 1);
    return s.frame0 + framesIn(s.bpm0, s.k, beat - s.beat0);
}

double TempoMap::frameToBeat(double frame) const {
    frame = std::max(0.0, frame);
    auto it = std::upper_bound(m_ts.begin(), m_ts.end(), frame, [](double f, const TSeg& s) { return f < s.frame0; });
    const TSeg& s = *(it - 1);
    return s.beat0 + beatsIn(s.bpm0, s.k, frame - s.frame0);
}

MeterPoint TempoMap::meterAtBeat(double beat) const {
    auto it = std::upper_bound(m_ms.begin(), m_ms.end(), beat + 1e-9, [](double b, const MSeg& s) { return b < s.beat0; });
    const MSeg& s = (it == m_ms.begin()) ? m_ms.front() : *(it - 1);
    return {s.bar0, s.num, s.den};
}

double TempoMap::barLength(int bar) const {
    auto it = std::upper_bound(m_ms.begin(), m_ms.end(), bar, [](int b, const MSeg& s) { return b < s.bar0; });
    const MSeg& s = (it == m_ms.begin()) ? m_ms.front() : *(it - 1);
    return s.num * 4.0 / s.den;
}

double TempoMap::barStart(int bar) const {
    bar = std::max(1, bar);
    auto it = std::upper_bound(m_ms.begin(), m_ms.end(), bar, [](int b, const MSeg& s) { return b < s.bar0; });
    const MSeg& s = *(it - 1);
    return s.beat0 + (bar - s.bar0) * s.num * 4.0 / s.den;
}

int TempoMap::barAt(double beat) const {
    beat = std::max(0.0, beat);
    auto it = std::upper_bound(m_ms.begin(), m_ms.end(), beat + 1e-9, [](double b, const MSeg& s) { return b < s.beat0; });
    const MSeg& s = *(it - 1);
    const double len = s.num * 4.0 / s.den;
    return s.bar0 + int(std::floor((beat - s.beat0) / len + 1e-9));
}

double TempoMap::nextBarLine(double beat) const {
    const int bar = barAt(beat);
    const double s = barStart(bar);
    return s >= beat - 1e-9 ? s : barStart(bar + 1);
}

void TempoMap::forEachClick(double a, double b, const std::function<void(double, bool)>& f) const {
    a = std::max(0.0, a);
    for (size_t i = 0; i < m_ms.size(); ++i) {
        const MSeg& s = m_ms[i];
        const double end = (i + 1 < m_ms.size()) ? m_ms[i + 1].beat0 : 1e12;
        const double lo = std::max(a, s.beat0), hi = std::min(b, end);
        if (lo >= hi) continue;
        const double unit = 4.0 / s.den, len = s.num * unit;
        for (double beat = s.beat0 + std::ceil((lo - s.beat0) / unit - 1e-9) * unit; beat < hi - 1e-9; beat += unit)
            f(beat, std::fmod(beat - s.beat0 + 1e-9, len) < 2e-9 + 1e-6);
    }
}

}  // namespace dsp
