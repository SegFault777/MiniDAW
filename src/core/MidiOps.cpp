// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MidiOps.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace core {

namespace {
/// Position of grid slot 'k' when every second slot is delayed: slot 2m sits on the pair start, slot 2m+1
/// at 'swing' of the pair (two grid steps long).
double slotPosition(i64 k, double grid, double swing) {
    const i64 pair = k >= 0 ? k / 2 : -((-k + 1) / 2);
    const bool odd = (k - pair * 2) != 0;
    return double(pair) * 2.0 * grid + (odd ? swing * 2.0 * grid : 0.0);
}

struct Lcg {
    std::uint32_t s;
    explicit Lcg(int seed) : s(std::uint32_t(seed) * 2654435761u + 12345u) {}
    std::uint32_t next() { s = s * 1664525u + 1013904223u; return s >> 8; }
};
}  // namespace

std::vector<Note> quantizeNotes(const std::vector<Note>& notes, const QuantizeParams& p) {
    std::vector<Note> out = notes;
    const double grid = std::max(1e-3, p.grid);
    const double swing = std::clamp(p.swing, 0.5, 0.75);
    const double strength = std::clamp(p.strength, 0.0, 1.0);
    for (Note& n : out) {
        const i64 base = i64(std::llround(n.start / grid));
        double best = slotPosition(base, grid, swing), bestDist = std::fabs(best - n.start);
        for (i64 k = base - 2; k <= base + 2; ++k) {   // swinging moves slots by up to one grid step
            const double pos = slotPosition(k, grid, swing);
            if (std::fabs(pos - n.start) < bestDist - 1e-12) { best = pos; bestDist = std::fabs(pos - n.start); }
        }
        n.start = std::max(0.0, n.start + (best - n.start) * strength);
        if (p.lengths) {
            const double snapped = std::max(grid, std::round(n.length / grid) * grid);
            n.length = n.length + (snapped - n.length) * strength;
        }
    }
    return out;
}

std::vector<Note> arpeggiate(const std::vector<Note>& notes, double regionBeats, const ArpParams& p) {
    const double rate = std::max(1.0 / 64.0, p.rate);
    const int octaves = std::clamp(p.octaves, 1, 4);
    const double gate = std::clamp(p.gate, 0.05, 1.0);
    std::vector<Note> sorted = notes;
    std::stable_sort(sorted.begin(), sorted.end(), [](const Note& a, const Note& b) { return a.start < b.start; });

    std::vector<Note> out;
    Lcg rng(p.seed);
    for (size_t i = 0; i < sorted.size();) {
        size_t j = i + 1;   // a chord: every note that starts within a 1/32 note of the first
        while (j < sorted.size() && sorted[j].start - sorted[i].start < 0.125) ++j;
        const double start = sorted[i].start;
        double end = start;
        for (size_t k = i; k < j; ++k) end = std::max(end, sorted[k].start + sorted[k].length);

        std::vector<Note> chord(sorted.begin() + std::ptrdiff_t(i), sorted.begin() + std::ptrdiff_t(j));
        if (p.pattern != ArpParams::AsPlayed)
            std::stable_sort(chord.begin(), chord.end(), [](const Note& a, const Note& b) { return a.pitch < b.pitch; });
        std::vector<Note> seq;   // the chord over all octaves, lowest first
        for (int o = 0; o < octaves; ++o)
            for (const Note& n : chord)
                if (n.pitch + 12 * o <= 127) { Note m = n; m.pitch += 12 * o; seq.push_back(m); }
        if (p.pattern == ArpParams::AsPlayed) {
            // keep the order of the chord's notes
        } else if (p.pattern == ArpParams::Down) {
            std::reverse(seq.begin(), seq.end());
        } else if ((p.pattern == ArpParams::UpDown || p.pattern == ArpParams::DownUp) && seq.size() > 2) {
            std::vector<Note> base = seq;
            if (p.pattern == ArpParams::DownUp) std::reverse(base.begin(), base.end());
            seq = base;
            for (size_t k = base.size() - 2; k >= 1; --k) {   // back again without repeating the end points
                seq.push_back(base[k]);
                if (k == 1) break;
            }
        } else if (p.pattern == ArpParams::DownUp) {
            std::reverse(seq.begin(), seq.end());
        }

        const int steps = std::max(1, int(std::floor((end - start) / rate + 1e-6)));
        int last = -1;
        for (int s = 0; s < steps; ++s) {
            size_t pick = size_t(s) % seq.size();
            if (p.pattern == ArpParams::Random && seq.size() > 1) {
                do { pick = rng.next() % seq.size(); } while (int(pick) == last);
                last = int(pick);
            }
            Note n = seq[pick];
            n.start = start + s * rate;
            if (n.start >= regionBeats) break;
            n.length = std::min(rate * gate, regionBeats - n.start);
            out.push_back(n);
        }
        i = j;
    }
    std::stable_sort(out.begin(), out.end(), [](const Note& a, const Note& b) { return a.start < b.start; });
    return out;
}

namespace {
const std::vector<std::vector<int>>& scaleSteps() {
    static const std::vector<std::vector<int>> s = {
        {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 8, 11}, {0, 2, 3, 5, 7, 9, 11},
        {0, 2, 3, 5, 7, 9, 10}, {0, 1, 3, 5, 7, 8, 10}, {0, 2, 4, 6, 7, 9, 11}, {0, 2, 4, 5, 7, 9, 10},
        {0, 2, 4, 7, 9}, {0, 3, 5, 7, 10}, {0, 3, 5, 6, 7, 10}, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}};
    return s;
}
}  // namespace

std::vector<Note> scaleQuantize(const std::vector<Note>& notes, const ScaleParams& p) {
    const auto& steps = scaleSteps()[size_t(std::clamp(p.scale, 0, int(ScaleParams::Count) - 1))];
    std::vector<Note> out = notes;
    for (Note& n : out) {
        const int rel = ((n.pitch - p.root) % 12 + 12) % 12;   // position inside the octave, relative to the key
        int best = steps[0], bestDist = 99;
        for (int s : steps) {
            for (int shift : {-12, 0, 12}) {   // the neighbouring octaves matter near the octave boundary
                const int d = std::abs(rel - (s + shift));
                if (d < bestDist || (d == bestDist && s + shift < best)) { bestDist = d; best = s + shift; }
            }
        }
        n.pitch = std::clamp(n.pitch + (best - rel), 0, 127);
    }
    return out;
}

std::vector<Note> humanize(const std::vector<Note>& notes, const HumanizeParams& p) {
    std::vector<Note> out = notes;
    Lcg rng(p.seed);
    auto uniform = [&] { return double(rng.next() & 0xFFFF) / 32767.5 - 1.0; };   // -1 .. 1
    for (Note& n : out) {
        n.start = std::max(0.0, n.start + uniform() * std::max(0.0, p.timing));
        n.velocity = std::clamp(n.velocity + int(std::lround(uniform() * std::max(0, p.velocity))), 1, 127);
    }
    std::stable_sort(out.begin(), out.end(), [](const Note& a, const Note& b) { return a.start < b.start; });
    return out;
}

std::vector<int> chordIntervals(int type) {
    switch (type) {
    case ChordParams::Major: return {0, 4, 7};
    case ChordParams::Minor: return {0, 3, 7};
    case ChordParams::Diminished: return {0, 3, 6};
    case ChordParams::Augmented: return {0, 4, 8};
    case ChordParams::Sus2: return {0, 2, 7};
    case ChordParams::Sus4: return {0, 5, 7};
    case ChordParams::Major7: return {0, 4, 7, 11};
    case ChordParams::Minor7: return {0, 3, 7, 10};
    case ChordParams::Dominant7: return {0, 4, 7, 10};
    case ChordParams::Octave: return {0, 12};
    }
    return {0};
}

std::vector<Note> chordTrigger(const std::vector<Note>& notes, const ChordParams& p) {
    const std::vector<int> iv = chordIntervals(p.type);
    const int inversion = std::clamp(p.inversion, 0, int(iv.size()) - 1);
    std::vector<Note> out;
    for (const Note& n : notes) {
        std::vector<int> pitches;
        for (int i : iv) pitches.push_back(n.pitch + i);
        for (int k = 0; k < inversion; ++k) {   // each inversion lifts the lowest remaining note by an octave
            std::sort(pitches.begin(), pitches.end());
            pitches.front() += 12;
        }
        std::sort(pitches.begin(), pitches.end());
        for (int pitch : pitches) {
            if (pitch < 0 || pitch > 127) continue;
            Note c = n;
            c.pitch = pitch;
            out.push_back(c);
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const Note& a, const Note& b) { return a.start < b.start; });
    return out;
}

std::vector<Note> legato(const std::vector<Note>& notes, double regionBeats) {
    std::vector<Note> out = notes;
    std::stable_sort(out.begin(), out.end(), [](const Note& a, const Note& b) { return a.start < b.start; });
    for (Note& n : out) {
        double next = regionBeats;   // the next different start time
        for (const Note& m : out)
            if (m.start > n.start + 1e-6) { next = std::min(next, m.start); break; }   // sorted: the first later start is the nearest
        n.length = std::max(1.0 / 64.0, next - n.start);
    }
    return out;
}

std::vector<Note> transposeNotes(const std::vector<Note>& notes, int semitones) {
    std::vector<Note> out = notes;
    for (Note& n : out) n.pitch = std::clamp(n.pitch + semitones, 0, 127);
    return out;
}

std::vector<Note> velocityRamp(const std::vector<Note>& notes, double regionBeats, int fromVelocity, int toVelocity) {
    std::vector<Note> out = notes;
    const double span = std::max(regionBeats, 1e-6);
    for (Note& n : out) {
        const double k = std::clamp(n.start / span, 0.0, 1.0);
        n.velocity = std::clamp(int(std::lround(fromVelocity + (toVelocity - fromVelocity) * k)), 1, 127);
    }
    return out;
}

// ---- v0.9 ----

static void sortByStart(std::vector<Note>& v) {
    std::stable_sort(v.begin(), v.end(), [](const Note& a, const Note& b) { return a.start < b.start; });
}

std::vector<Note> fixOverlaps(const std::vector<Note>& notes) {
    std::vector<Note> v = notes;
    sortByStart(v);
    for (size_t i = 0; i < v.size(); ++i)
        for (size_t j = i + 1; j < v.size(); ++j) {
            if (v[j].pitch != v[i].pitch || v[j].start <= v[i].start + 1e-9) continue;   // same start: a duplicate, not an overlap
            if (v[j].start < v[i].start + v[i].length) v[i].length = v[j].start - v[i].start;
            break;   // only the next note of this pitch matters
        }
    return v;
}

std::vector<Note> removeDuplicateNotes(const std::vector<Note>& notes, double tolerance) {
    std::vector<Note> v = notes, out;
    sortByStart(v);
    for (const Note& n : v) {
        bool dup = false;
        for (auto it = out.rbegin(); it != out.rend() && n.start - it->start <= tolerance; ++it)
            if (it->pitch == n.pitch) { it->length = std::max(it->length, n.length); dup = true; break; }
        if (!dup) out.push_back(n);
    }
    return out;
}

std::vector<Note> fixedLength(const std::vector<Note>& notes, double regionBeats, double length) {
    std::vector<Note> v = notes;
    for (Note& n : v) n.length = std::max(0.01, std::min(length, regionBeats - n.start));
    return v;
}

std::vector<Note> retrograde(const std::vector<Note>& notes, double regionBeats) {
    std::vector<Note> v = notes;
    for (Note& n : v) n.start = std::max(0.0, regionBeats - (n.start + n.length));
    sortByStart(v);
    return v;
}

std::vector<Note> invertPitches(const std::vector<Note>& notes) {
    if (notes.empty()) return {};
    int lo = 127, hi = 0;
    for (const Note& n : notes) { lo = std::min(lo, n.pitch); hi = std::max(hi, n.pitch); }
    std::vector<Note> v = notes;
    for (Note& n : v) n.pitch = std::clamp(lo + hi - n.pitch, 0, 127);
    return v;
}

std::vector<Note> scaleVelocity(const std::vector<Note>& notes, int percent, int offset) {
    std::vector<Note> v = notes;
    for (Note& n : v) n.velocity = std::clamp(int(std::lround(n.velocity * percent / 100.0)) + offset, 1, 127);
    return v;
}

// ---- v0.12 ----

bool inScale(int pitch, int root, int scale) {
    const auto& steps = scaleSteps()[size_t(std::clamp(scale, 0, int(ScaleParams::Count) - 1))];
    const int rel = ((pitch - root) % 12 + 12) % 12;
    return std::find(steps.begin(), steps.end(), rel) != steps.end();
}

const char* scaleName(int scale) {
    static const char* n[] = {"Major", "Minor", "Harmonic Minor", "Melodic Minor", "Dorian", "Phrygian", "Lydian", "Mixolydian",
                              "Pentatonic Major", "Pentatonic Minor", "Blues", "Chromatic"};
    return n[std::clamp(scale, 0, int(ScaleParams::Count) - 1)];
}

const char* chordName(int type) {
    static const char* n[] = {"Major", "Minor", "Diminished", "Augmented", "Sus2", "Sus4", "Major 7", "Minor 7", "Dominant 7", "Octave"};
    return n[std::clamp(type, 0, int(ChordParams::Count) - 1)];
}

const char* noteName(int pc) {
    static const char* n[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return n[((pc % 12) + 12) % 12];
}

std::vector<Note> stampChord(int rootPitch, double start, double length, int velocity, const StampParams& p) {
    std::vector<int> pitches;
    if (p.diatonic) {
        const auto& steps = scaleSteps()[size_t(std::clamp(p.scale, 0, int(ScaleParams::Count) - 1))];
        const int n = int(steps.size());
        const int rel = ((rootPitch - p.keyRoot) % 12 + 12) % 12;
        const int base = rootPitch - rel;   // the key's pitch class in the octave of the clicked note
        int idx = 0;                        // nearest scale degree at or below the clicked note
        for (int i = 0; i < n; ++i) if (steps[size_t(i)] <= rel) idx = i;
        const int size = std::clamp(p.size, 2, 5);
        const int skip = n >= 7 ? 2 : 1;    // thirds in a seven note scale, neighbouring notes in a pentatonic one
        for (int k = 0; k < size; ++k) {
            const int i = idx + k * skip;
            pitches.push_back(base + 12 * (i / n) + steps[size_t(i % n)]);
        }
    } else {
        for (int iv : chordIntervals(p.type)) pitches.push_back(rootPitch + iv);
    }
    std::sort(pitches.begin(), pitches.end());
    for (int inv = 0; inv < p.inversion && !pitches.empty(); ++inv) {
        pitches.push_back(pitches.front() + 12);
        pitches.erase(pitches.begin());
    }
    std::vector<Note> out;
    for (int pitch : pitches) {
        if (pitch < 0 || pitch > 127) continue;
        out.push_back(Note{pitch, start, length, std::clamp(velocity, 1, 127)});
    }
    return out;
}

std::vector<Note> generateRiff(const RiffParams& p, double regionBeats) {
    const auto& steps = scaleSteps()[size_t(std::clamp(p.scale, 0, int(ScaleParams::Count) - 1))];
    const int low = 12 * (std::clamp(p.octave, 0, 9) + 1) + ((p.root % 12) + 12) % 12;
    std::vector<int> pool;   // every scale pitch from 'low' up to the end of the range
    for (int oct = 0; oct < 8; ++oct)
        for (int s : steps) {
            const int pitch = low + 12 * oct + s;
            if (pitch - low <= std::max(2, p.range) && pitch <= 127) pool.push_back(pitch);
        }
    std::vector<Note> out;
    if (pool.empty() || regionBeats <= 0) return out;
    Lcg rng(p.seed);
    auto unit = [&] { return double(rng.next() & 0xFFFF) / 65536.0; };
    const double step = std::max(0.0625, p.stepLength);
    const int count = int(std::floor(regionBeats / step + 1e-6));
    const int perBeat = std::max(1, int(std::lround(1.0 / step)));
    int cur = 0, run = 0;
    for (int i = 0; i < count; ++i) {
        const bool downbeat = i % perBeat == 0;
        const bool barStart = i % (perBeat * 4) == 0;
        if (!(i == 0 || barStart || unit() < std::clamp(p.density, 0.0, 1.0) * (downbeat ? 1.15 : 1.0))) continue;
        int next = cur;
        if (i > 0 && !barStart) {
            const int leap = std::max(1, p.leap);
            next = std::clamp(cur + int(std::lround((unit() * 2 - 1) * leap)), 0, int(pool.size()) - 1);
        } else if (barStart) {
            next = unit() < 0.6 ? 0 : std::clamp(int(unit() * 4), 0, int(pool.size()) - 1);   // phrases start near the root
        }
        run = next == cur ? run + 1 : 0;
        if (run >= 2) next = std::clamp(next + (unit() < 0.5 ? -1 : 1), 0, int(pool.size()) - 1);
        cur = next;
        Note n;
        n.pitch = pool[size_t(cur)];
        n.start = i * step;
        n.length = step * (unit() < 0.25 ? 1.8 : 0.9);
        const int vLo = std::clamp(p.velocityMin, 1, 127), vHi = std::clamp(std::max(p.velocityMax, p.velocityMin), 1, 127);
        n.velocity = vLo + int(unit() * (vHi - vLo));
        if (barStart) n.velocity = vHi;
        else if (downbeat) n.velocity = std::min(127, n.velocity + 8);
        out.push_back(n);
    }
    for (size_t i = 0; i + 1 < out.size(); ++i) out[i].length = std::min(out[i].length, out[i + 1].start - out[i].start);   // monophonic line
    if (!out.empty()) out.back().length = std::min(out.back().length, regionBeats - out.back().start);
    return out;
}

const char* logicalPropertyName(int p) {
    static const char* n[] = {"Pitch", "Velocity", "Length", "Position"};
    return n[std::clamp(p, 0, 3)];
}
const char* logicalActionName(int a) {
    static const char* n[] = {"Delete", "Transpose", "Set Velocity", "Add Velocity", "Scale Velocity %", "Set Length", "Scale Length %", "Set Pitch", "Move"};
    return n[std::clamp(a, 0, 8)];
}

std::vector<Note> logicalEdit(const std::vector<Note>& notes, const LogicalParams& p, int* affected) {
    std::vector<Note> out;
    int hits = 0;
    for (const Note& n : notes) {
        bool match = true;
        for (const LogicalCondition& c : p.conditions) {
            const double v = c.property == LogicalCondition::Pitch ? n.pitch : c.property == LogicalCondition::Velocity ? n.velocity
                           : c.property == LogicalCondition::Length ? n.length : n.start;
            if (v < c.lo - 1e-9 || v > c.hi + 1e-9) { match = false; break; }
        }
        if (match == p.invert) { out.push_back(n); continue; }
        ++hits;
        Note m = n;
        switch (p.action) {
        case LogicalParams::Delete: continue;
        case LogicalParams::Transpose: m.pitch = std::clamp(m.pitch + int(std::lround(p.value)), 0, 127); break;
        case LogicalParams::SetVelocity: m.velocity = std::clamp(int(std::lround(p.value)), 1, 127); break;
        case LogicalParams::AddVelocity: m.velocity = std::clamp(m.velocity + int(std::lround(p.value)), 1, 127); break;
        case LogicalParams::ScaleVelocity: m.velocity = std::clamp(int(std::lround(m.velocity * p.value / 100.0)), 1, 127); break;
        case LogicalParams::SetLength: m.length = std::max(0.01, p.value); break;
        case LogicalParams::ScaleLength: m.length = std::max(0.01, m.length * p.value / 100.0); break;
        case LogicalParams::SetPitch: m.pitch = std::clamp(int(std::lround(p.value)), 0, 127); break;
        case LogicalParams::Move: m.start = std::max(0.0, m.start + p.value); break;
        default: break;
        }
        out.push_back(m);
    }
    std::stable_sort(out.begin(), out.end(), [](const Note& a, const Note& b) { return a.start < b.start; });
    if (affected) *affected = hits;
    return out;
}

Groove makeGroove(const std::vector<Note>& notes, double grid) {
    Groove g;
    if (notes.empty() || grid <= 0.0) return g;
    g.grid = grid;
    const int slots = std::max(1, int(std::lround(4.0 / grid)));
    std::vector<double> off(size_t(slots), 0.0), vel(size_t(slots), 0.0);
    std::vector<int> cnt(size_t(slots), 0);
    double meanVel = 0;
    for (const Note& n : notes) meanVel += n.velocity;
    meanVel = std::max(1.0, meanVel / double(notes.size()));
    for (const Note& n : notes) {
        const long s = std::lround(n.start / grid);
        const size_t slot = size_t(((s % slots) + slots) % slots);
        off[slot] += n.start - double(s) * grid;
        vel[slot] += n.velocity / meanVel;
        ++cnt[slot];
    }
    g.timing.assign(size_t(slots), 0.0);
    g.velocity.assign(size_t(slots), 1.0);
    for (size_t i = 0; i < size_t(slots); ++i)
        if (cnt[i]) { g.timing[i] = off[i] / cnt[i]; g.velocity[i] = vel[i] / cnt[i]; }
    return g;
}

std::vector<Note> applyGroove(const std::vector<Note>& notes, const Groove& g, double strength) {
    std::vector<Note> out = notes;
    if (!g.valid()) return out;
    const double k = std::clamp(strength, 0.0, 1.0);
    const long slots = long(g.timing.size());
    for (Note& n : out) {
        const long s = std::lround(n.start / g.grid);
        const size_t slot = size_t(((s % slots) + slots) % slots);
        const double target = double(s) * g.grid + g.timing[slot];
        n.start = std::max(0.0, n.start + (target - n.start) * k);
        n.velocity = std::clamp(int(std::lround(n.velocity * (1.0 + (g.velocity[slot] - 1.0) * k))), 1, 127);
    }
    std::stable_sort(out.begin(), out.end(), [](const Note& a, const Note& b) { return a.start < b.start; });
    return out;
}

std::vector<Note> stretchNotes(const std::vector<Note>& notes, double factor) {
    std::vector<Note> out = notes;
    const double f = std::clamp(factor, 0.0625, 16.0);
    for (Note& n : out) { n.start *= f; n.length = std::max(0.01, n.length * f); }
    return out;
}

std::vector<Note> strumNotes(const std::vector<Note>& notes, double spacing, bool down) {
    std::vector<Note> out = notes;
    std::vector<size_t> order(out.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        if (std::fabs(out[a].start - out[b].start) > 1e-6) return out[a].start < out[b].start;
        return down ? out[a].pitch < out[b].pitch : out[a].pitch > out[b].pitch;
    });
    size_t i = 0;
    while (i < order.size()) {
        size_t j = i;
        while (j < order.size() && std::fabs(out[order[j]].start - out[order[i]].start) < 1e-6) ++j;   // one chord = notes with the same start
        for (size_t k = i; k < j; ++k) {
            Note& n = out[order[k]];
            const double shift = spacing * double(k - i);
            n.start += shift;
            n.length = std::max(0.01, n.length - shift);
        }
        i = j;
    }
    std::stable_sort(out.begin(), out.end(), [](const Note& a, const Note& b) { return a.start < b.start; });
    return out;
}

std::vector<Note> doublePattern(const std::vector<Note>& notes, double regionBeats) {
    std::vector<Note> out = notes;
    for (const Note& n : notes) { Note c = n; c.start += regionBeats; out.push_back(c); }
    return out;
}

std::string detectChord(const std::vector<int>& pitches) {
    if (pitches.size() < 2) return {};
    int bass = 127;
    unsigned mask = 0;
    for (int p : pitches) { bass = std::min(bass, p); mask |= 1u << (((p % 12) + 12) % 12); }
    struct T { const char* name; unsigned mask; };
    auto m = [](std::initializer_list<int> iv) { unsigned x = 0; for (int i : iv) x |= 1u << i; return x; };
    const T table[] = {
        {"", m({0, 4, 7})}, {"m", m({0, 3, 7})}, {"dim", m({0, 3, 6})}, {"aug", m({0, 4, 8})}, {"sus2", m({0, 2, 7})}, {"sus4", m({0, 5, 7})},
        {"7", m({0, 4, 7, 10})}, {"maj7", m({0, 4, 7, 11})}, {"m7", m({0, 3, 7, 10})}, {"m7b5", m({0, 3, 6, 10})}, {"dim7", m({0, 3, 6, 9})},
        {"6", m({0, 4, 7, 9})}, {"m6", m({0, 3, 7, 9})}, {"add9", m({0, 2, 4, 7})}, {"m(maj7)", m({0, 3, 7, 11})}, {"5", m({0, 7})},
    };
    const int bassPc = ((bass % 12) + 12) % 12;
    auto rotate = [&](int root) { unsigned r = 0; for (int i = 0; i < 12; ++i) if (mask & (1u << i)) r |= 1u << (((i - root) % 12 + 12) % 12); return r; };
    int bestRoot = -1;
    const char* bestName = nullptr;
    for (int off = 0; off < 12; ++off) {   // try the bass note first, then the other roots
        const int root = (bassPc + off) % 12;
        if (!(mask & (1u << root))) continue;
        const unsigned r = rotate(root);
        for (const T& t : table) if (t.mask == r) { bestRoot = root; bestName = t.name; break; }
        if (bestName) break;
    }
    if (!bestName) return {};
    std::string s = noteName(bestRoot);
    s += bestName;
    if (bestRoot != bassPc) { s += "/"; s += noteName(bassPc); }
    return s;
}

}  // namespace core
