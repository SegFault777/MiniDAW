// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Pure MIDI note transformations (v0.7): arpeggiator and quantize with swing / strength.
#include <string>
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

// ---- v0.9 ----

/// Shortens notes that overlap a later note of the same pitch so that they end where that note starts
/// (Logic's Fix Overlaps). The result is sorted by start.
std::vector<Note> fixOverlaps(const std::vector<Note>& notes);

/// Removes notes that repeat the same pitch at (nearly) the same start; the longer one is kept.
std::vector<Note> removeDuplicateNotes(const std::vector<Note>& notes, double tolerance = 0.01);

/// Gives every note the same length (never longer than the rest of the region).
std::vector<Note> fixedLength(const std::vector<Note>& notes, double regionBeats, double length);

/// Plays the notes backwards inside the region (Logic's Retrograde).
std::vector<Note> retrograde(const std::vector<Note>& notes, double regionBeats);

/// Mirrors the pitches around the middle of the lowest and the highest note (Logic's Invert).
std::vector<Note> invertPitches(const std::vector<Note>& notes);

/// velocity = velocity * percent / 100 + offset, kept in 1 .. 127.
std::vector<Note> scaleVelocity(const std::vector<Note>& notes, int percent, int offset);

// ---- v0.12: Ableton / FL Studio / Cubase / Logic style note tools ----

/// True when the pitch belongs to the scale (used for Fold / Scale Mode highlighting in the piano roll).
bool inScale(int pitch, int root, int scale);
const char* scaleName(int scale);
const char* chordName(int type);
const char* noteName(int pitchClass);   // "C", "C#", ...

/// FL Studio style chord stamp: one click puts a whole chord into the piano roll.
struct StampParams {
    int type = ChordParams::Major;   // fixed chord shape (ignored when 'diatonic')
    int inversion = 0;
    bool diatonic = false;           // stack thirds on the scale degrees of the key instead
    int size = 3;                    // diatonic chord size: 3 = triad, 4 = seventh chord
    int keyRoot = 0;
    int scale = ScaleParams::Major;
};
std::vector<Note> stampChord(int rootPitch, double start, double length, int velocity, const StampParams& p);

/// FL Studio style Riff Machine: a random but musical melody inside a scale.
struct RiffParams {
    int root = 0;                    // pitch class of the key
    int scale = ScaleParams::Minor;
    int octave = 4;                  // octave of the lowest note (4 -> C4 = 60)
    int range = 12;                  // semitones available above the lowest note
    double stepLength = 0.25;        // beats per step
    double density = 0.7;            // 0..1 chance that a step has a note
    int leap = 2;                    // largest jump in scale degrees between two notes
    int velocityMin = 70, velocityMax = 112;
    int seed = 1;
};
std::vector<Note> generateRiff(const RiffParams& p, double regionBeats);

/// Cubase style Logical Editor: every condition must hold (or none must, with 'invert'); the action is applied to the hits.
struct LogicalCondition {
    enum Property { Pitch, Velocity, Length, Position };
    int property = Pitch;
    double lo = 0, hi = 127;         // inclusive; Length and Position are in beats
};
struct LogicalParams {
    enum Action { Delete, Transpose, SetVelocity, AddVelocity, ScaleVelocity, SetLength, ScaleLength, SetPitch, Move };
    std::vector<LogicalCondition> conditions;
    bool invert = false;
    int action = Transpose;
    double value = 0;                // semitones, velocity, percent, beats ...
};
std::vector<Note> logicalEdit(const std::vector<Note>& notes, const LogicalParams& p, int* affected = nullptr);
const char* logicalPropertyName(int property);
const char* logicalActionName(int action);

/// Logic Pro style groove template: the timing and velocity feel of one region, applicable to another.
struct Groove {
    double grid = 0.25;
    std::vector<double> timing;      // offset in beats of each grid slot inside a 4 beat cycle
    std::vector<double> velocity;    // velocity factor of each slot
    bool valid() const { return !timing.empty(); }
};
Groove makeGroove(const std::vector<Note>& notes, double grid);
std::vector<Note> applyGroove(const std::vector<Note>& notes, const Groove& g, double strength);

// ---- v0.12: Ableton Live "Transform" tools and Logic Pro chord display ----

/// Multiplies every start and length by 'factor' (Ableton Live Stretch: 2 = twice as slow, 0.5 = twice as fast).
std::vector<Note> stretchNotes(const std::vector<Note>& notes, double factor);
/// Ableton Live Strum: notes that start together are spread out in pitch order, 'spacing' beats apart (down = from the lowest note).
std::vector<Note> strumNotes(const std::vector<Note>& notes, double spacing, bool down);
/// Ableton Live "Duplicate Loop": the notes followed by a copy shifted by 'regionBeats' (the region has to grow to twice its length).
std::vector<Note> doublePattern(const std::vector<Note>& notes, double regionBeats);
/// Name of the chord the pitches form ("C", "Am", "G7", "Cmaj7", "C/E"), or "" when they do not form a known chord.
std::string detectChord(const std::vector<int>& pitches);

}  // namespace core
