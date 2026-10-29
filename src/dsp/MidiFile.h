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

/// The parts of a Standard MIDI File that MiniDAW cares about.
struct MidiSong {
    double bpm = 120;
    int num = 4, den = 4;
    std::vector<MidiTrack> tracks;
};

bool readMidiFile(const std::vector<std::uint8_t>& data, MidiSong& out, std::string& error);
std::vector<std::uint8_t> writeMidiFile(const MidiSong& song);

}  // namespace dsp
