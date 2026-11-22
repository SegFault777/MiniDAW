// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace dsp {

struct MidiNote {
    int channel = 0;
    int pitch = 60;
    int velocity = 100;
    double startBeat = 0;     ///< Quarter-note beats.
    double lengthBeats = 1;
};

struct MidiTrack {
    std::string name;
    std::vector<MidiNote> notes;
};

struct MidiTempoChange { double beat = 0; double bpm = 120; };
struct MidiMeterChange { double beat = 0; int num = 4, den = 4; };
struct MidiMarker { double beat = 0; std::string name; };

/// The parts of a Standard MIDI File that MiniDAW cares about: notes, the tempo map, time signature changes and markers
/// (what Logic Pro, Ableton Live, Cubase and the others write into the conductor track).
struct MidiSong {
    double bpm = 120;                        ///< the tempo at the start
    int num = 4, den = 4;                    ///< the time signature at the start
    std::vector<MidiTempoChange> tempos;     ///< every tempo change in beat order (the first one is at beat 0); empty = only 'bpm'
    std::vector<MidiMeterChange> meters;     ///< every time signature change (the first one is at beat 0); empty = only 'num' / 'den'
    std::vector<MidiMarker> markers;
    std::vector<MidiTrack> tracks;
};

bool readMidiFile(const std::vector<std::uint8_t>& data, MidiSong& out, std::string& error);
std::vector<std::uint8_t> writeMidiFile(const MidiSong& song);

}  // namespace dsp
