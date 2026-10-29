// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <vector>

namespace dsp {

/// Settings of a Drummer region (the XY pad, style, swing and fills of the Drummer editor).
struct DrummerParams {
    int style = 0;
    double complexity = 0.5;   ///< 0 = simple, 1 = busy (ghost notes, extra kicks, 16th hats)
    double loudness = 0.6;     ///< 0 = soft, 1 = loud (velocity, open hats, crash)
    double swing = 0.0;        ///< 0..1
    bool fills = true;
    int seed = 1;              ///< Changing the seed produces a new variation.
    int bars = 4;
};

struct DrumHit {
    int pitch;        ///< General MIDI drum note number.
    double beat;      ///< Position in quarter-note beats from the start of the region.
    double length;
    int velocity;
};

int drummerStyleCount();
const char* drummerStyleName(int style);

/// Generates the drum pattern of a region for a time signature of num/den.
std::vector<DrumHit> generateDrummer(const DrummerParams& p, int num, int den);

}  // namespace dsp
