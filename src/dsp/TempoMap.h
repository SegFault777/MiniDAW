// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <functional>
#include <vector>

namespace dsp {

/// A tempo change. Beats are quarter notes. With 'ramp' the tempo glides linearly to the next point.
struct TempoPoint {
    double beat = 0;
    double bpm = 120;
    bool ramp = false;
};

/// A time signature change starting at a (1-based) bar.
struct MeterPoint {
    int bar = 1;
    int num = 4;
    int den = 4;
};

/// Tempo and time-signature track. Converts between musical time (beats) and audio frames.
/// Instances are immutable once shared with the audio thread; build a new map to change anything.
class TempoMap {
public:
    TempoMap() { rebuild(); }

    void setTempo(std::vector<TempoPoint> points);
    void setMeter(std::vector<MeterPoint> points);
    const std::vector<TempoPoint>& tempoPoints() const { return m_tempo; }
    const std::vector<MeterPoint>& meterPoints() const { return m_meter; }

    double bpmAt(double beat) const;
    double beatToFrame(double beat) const;     ///< Frames at 44.1 kHz from the start of the project.
    double frameToBeat(double frame) const;

    MeterPoint meterAtBeat(double beat) const;
    double barLength(int bar) const;           ///< In quarter-note beats.
    double barStart(int bar) const;
    int barAt(double beat) const;              ///< 1-based bar number containing the beat.
    double nextBarLine(double beat) const;     ///< First bar line at or after the beat.
    /// Calls f(beat, accent) for every metronome click in [a, b). Accents mark the first beat of a bar.
    void forEachClick(double a, double b, const std::function<void(double, bool)>& f) const;

private:
    struct TSeg { double beat0, bpm0, k, frame0; };   // k = bpm change per beat (0 for a constant tempo)
    struct MSeg { int bar0; double beat0; int num, den; };
    void rebuild();
    std::vector<TempoPoint> m_tempo;
    std::vector<MeterPoint> m_meter;
    std::vector<TSeg> m_ts;
    std::vector<MSeg> m_ms;
};

}  // namespace dsp
