// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// What we take from a Logic Pro project, independent of how it was read: metadata, tempo, tracks, audio and MIDI regions.
// The ProjectData parser (core/LogicProjectData) fills it, core/LogicImport applies it to a Session.
#include <string>
#include <vector>

#include "../dsp/TempoMap.h"
#include "Model.h"

namespace core {

struct LogicAudioRegion {
    std::string name;
    std::string file;          ///< file name or path relative to Media/ (as found in the package)
    double startBeat = 0;      ///< position on the timeline in quarter notes
    double lengthBeats = 0;    ///< 0 = whole file
    long long trimInSamples = 0;     ///< start inside the file, in samples of the project sample rate
    long long lengthSamples = 0;     ///< used length, in samples of the project sample rate (0 = whole file)
};

struct LogicMidiRegion {
    std::string name;
    double startBeat = 0;
    double lengthBeats = 4;
    std::vector<Note> notes;   ///< start relative to the region start, in beats
};

enum class LogicTrackKind { Audio, Software, Drummer, Other };

struct LogicTrack {
    int number = 0;            ///< Logic's track number (row of the arrange list, 1 = first), 0 = unknown
    std::string name;
    LogicTrackKind kind = LogicTrackKind::Audio;
    std::vector<LogicAudioRegion> audio;
    std::vector<LogicMidiRegion> midi;
};

struct LogicProject {
    std::string name;
    std::string creator;                          ///< "Logic Pro 10.8" ... when the file says so
    std::vector<dsp::TempoPoint> tempo;           ///< empty = unknown
    std::vector<dsp::MeterPoint> meters;
    std::vector<LogicTrack> tracks;
    std::vector<std::pair<double, std::string>> markers;   ///< (beat, name)
    int sampleRate = 0;                           ///< project sample rate (MetaData.plist)
    int trackCountHint = 0;                       ///< "NumberOfTracks" of MetaData.plist (information only)
    std::vector<std::string> mediaPool;           ///< audio file names of the project's media pool (ProjectData AuFl chunks)
    std::vector<std::pair<std::string, int>> chunkCounts;   ///< ProjectData chunk types and how many of each (diagnostics)
    bool empty() const { return tracks.empty() && tempo.empty() && mediaPool.empty() && markers.empty(); }
};

}  // namespace core
