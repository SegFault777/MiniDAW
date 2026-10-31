// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MidiOps.h"

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

}  // namespace core
