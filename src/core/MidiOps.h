// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Pure MIDI note transformations (v0.7): arpeggiator and quantize with swing / strength.
#include <vector>
#include "Model.h"

namespace core {

struct ArpParams {
    enum Pattern { Up, Down, UpDown, DownUp, Random, AsPlayed };
    int pattern = Up;
    double rate = 0.25;      ///< Step length in beats (0.25 = 1/16 note).
    int octaves = 1;         ///< 1 .. 4 octaves of the chord.
    double gate = 0.8;       ///< Note length as a fraction of the step (0.05 .. 1).
    int seed = 1;            ///< Random pattern.
};

/// Replaces every chord (notes that start together) by an arpeggio over the chord's duration.
/// Note positions are relative to the region; notes at or after 'regionBeats' are dropped.
std::vector<Note> arpeggiate(const std::vector<Note>& notes, double regionBeats, const ArpParams& p);

struct QuantizeParams {
    double grid = 0.25;      ///< Grid in beats (0.25 = 1/16 note).
    double strength = 1.0;   ///< 0 = notes stay, 1 = notes snap completely.
    double swing = 0.5;      ///< Position of the second note of every grid pair: 0.5 straight, 0.667 triplet feel, 0.75 maximum.
    bool lengths = false;    ///< Also snap the note lengths to the grid.
};

/// Quantizes note starts to the (swung) grid, like Logic Pro's Q-Strength and Q-Swing.
std::vector<Note> quantizeNotes(const std::vector<Note>& notes, const QuantizeParams& p);

struct ScaleParams {
    enum Scale { Major, Minor, HarmonicMinor, MelodicMinor, Dorian, Phrygian, Lydian, Mixolydian, PentatonicMajor, PentatonicMinor, Blues, Chromatic, Count };
    int root = 0;            ///< Key: 0 = C ... 11 = B.
    int scale = Major;
};

/// Moves every note to the nearest pitch of the scale (a tie goes down), like Logic's Scale Quantizer.
std::vector<Note> scaleQuantize(const std::vector<Note>& notes, const ScaleParams& p);

struct HumanizeParams {
    double timing = 0.03;    ///< Largest start-time change in beats, in both directions.
    int velocity = 10;       ///< Largest velocity change, in both directions.
    int seed = 1;
};

/// Randomises note starts and velocities a little, so programmed parts sound played.
std::vector<Note> humanize(const std::vector<Note>& notes, const HumanizeParams& p);

struct ChordParams {
    enum Type { Major, Minor, Diminished, Augmented, Sus2, Sus4, Major7, Minor7, Dominant7, Octave, Count };
    int type = Major;
    int inversion = 0;       ///< 0 .. 3: raises the lowest notes of every chord by an octave.
};

/// Turns every note into a chord built on it, like Logic's Chord Trigger. Notes above 127 are dropped.
std::vector<Note> chordTrigger(const std::vector<Note>& notes, const ChordParams& p);

/// Semitone steps above the root for a chord type (the root itself is 0).
std::vector<int> chordIntervals(int type);

/// Extends or shortens every note so that it ends where the next note starts (the last one ends with the region),
/// like Logic's Force Legato.
std::vector<Note> legato(const std::vector<Note>& notes, double regionBeats);

/// Moves all notes by 'semitones' (clamped to 0 .. 127).
std::vector<Note> transposeNotes(const std::vector<Note>& notes, int semitones);

/// Velocity ramp over the region: a crescendo (from < to) or decrescendo (from > to).
std::vector<Note> velocityRamp(const std::vector<Note>& notes, double regionBeats, int fromVelocity, int toVelocity);

}  // namespace core
