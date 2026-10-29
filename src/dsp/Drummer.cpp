// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Drummer.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <string>

namespace dsp {
namespace {

constexpr int kKick = 36, kSnare = 38, kHat = 42, kOpenHat = 46, kClap = 39, kCrash = 49;

struct Style {
    const char* name;
    const char* kick;    // 16-step patterns, 'x' = hit
    const char* snare;
    const char* hat;
    const char* clap;
    const char* open;    // open hat positions
};

const Style kStyles[] = {
    {"Rock",    "x.......x.x.....", "....x.......x...", "x.x.x.x.x.x.x.x.", "................", "................"},
    {"Pop",     "x.......x.......", "....x.......x...", "x.x.x.x.x.x.x.x.", "................", "..............x."},
    {"Hip-Hop", "x.....x...x.....", "....x.......x...", "x.x.x.x.x.x.x.x.", "................", "................"},
    {"House",    "x...x...x...x...", "................", "x.x.x.x.x.x.x.x.", "....x.......x...", ".x...x...x...x.."},
    {"Funk",    "x..x..x...x..x..", "....x..x.x..x...", "xxxxxxxxxxxxxxxx", "................", "................"},
    {"Disco",   "x...x...x...x...", "....x.......x...", "x...x...x...x...", "................", ".x...x...x...x.."},
};
constexpr int kStyleCount = int(sizeof(kStyles) / sizeof(kStyles[0]));

}  // namespace

int drummerStyleCount() { return kStyleCount; }
const char* drummerStyleName(int s) { return kStyles[std::clamp(s, 0, kStyleCount - 1)].name; }

std::vector<DrumHit> generateDrummer(const DrummerParams& p, int num, int den) {
    std::vector<DrumHit> hits;
    std::mt19937 rng(unsigned(p.seed) * 2654435761u + unsigned(p.style) * 97u);
    auto chance = [&](double prob) { return std::uniform_real_distribution<double>(0, 1)(rng) < prob; };
    const Style& st = kStyles[std::clamp(p.style, 0, kStyleCount - 1)];
    const double barBeats = num * 4.0 / den;
    const int steps = std::max(1, int(std::lround(barBeats * 4.0)));   // 16th notes per bar
    const double loud = std::clamp(p.loudness, 0.0, 1.0), cx = std::clamp(p.complexity, 0.0, 1.0);
    auto vel = [&](double base, double accent = 0) {
        const double v = (base + accent) * (0.5 + 0.5 * loud) + std::uniform_real_distribution<double>(-4, 4)(rng);
        return std::clamp(int(std::lround(v)), 1, 127);
    };
    auto add = [&](int pitch, int bar, int step, double len, int velocity) {
        double beat = bar * barBeats + step * 0.25;
        if (step % 2 == 1) beat += std::clamp(p.swing, 0.0, 1.0) * 0.06;   // swing delays the off-beat 16ths
        hits.push_back({pitch, beat, len, velocity});
    };

    for (int bar = 0; bar < p.bars; ++bar) {
        const bool fillBar = p.fills && p.bars > 1 && (bar % 4 == 3 || bar == p.bars - 1) && bar > 0;
        const int fillFrom = std::max(0, steps - 4);
        for (int s = 0; s < steps; ++s) {
            if (fillBar && s >= fillFrom) continue;   // the fill replaces the last beat
            // Map the step to the 16-step style grid; other meters use a plain rock feel.
            bool kick, snare, hat, clap, open;
            if (steps == 16) {
                kick = st.kick[s] == 'x'; snare = st.snare[s] == 'x'; hat = st.hat[s] == 'x';
                clap = st.clap[s] == 'x'; open = st.open[s] == 'x';
            } else {
                kick = s == 0 || (steps > 8 && s == steps / 2 + steps / 4);
                snare = s == steps / 2 || (steps % 4 == 0 && s == steps - steps / 4 && steps > 8);
                hat = s % 2 == 0; clap = false; open = false;
            }
            if (cx < 0.3 && hat && s % 4 != 0) hat = false;                 // simple: hats on the beat only
            if (kick) add(kKick, bar, s, 0.2, vel(100, s == 0 ? 12 : 0));
            if (snare) add(kSnare, bar, s, 0.2, vel(104, 6));
            if (clap) add(kClap, bar, s, 0.2, vel(96));
            if (open && loud > 0.45) add(kOpenHat, bar, s, 0.25, vel(86));
            else if (hat && !(open && loud > 0.45)) add(kHat, bar, s, 0.1, vel(s % 4 == 0 ? 82 : 66));
            // Complexity adds ghost notes, extra kicks and 16th hats.
            if (!kick && !snare && s % 2 == 1 && chance(cx * 0.3)) add(kSnare, bar, s, 0.1, vel(38));
            if (!kick && !snare && s % 4 != 0 && s > 0 && chance(cx * 0.18)) add(kKick, bar, s, 0.15, vel(78));
            if (!hat && !open && cx > 0.55 && s % 2 == 1 && st.hat[0] == 'x') add(kHat, bar, s, 0.08, vel(46));
        }
        if (fillBar) {   // a short tom / snare run into the next bar
            static const int pitches[] = {kSnare, 48, 45, 43};
            for (int i = 0; i < 4 && fillFrom + i < steps; ++i)
                add(pitches[i], bar, fillFrom + i, 0.2, vel(80, i * 10));
        }
        if ((bar == 0 || (p.fills && bar % 4 == 0)) && loud > 0.65) add(kCrash, bar, 0, 1.0, vel(96));
    }
    std::sort(hits.begin(), hits.end(), [](const DrumHit& a, const DrumHit& b) { return a.beat < b.beat; });
    return hits;
}

}  // namespace dsp
