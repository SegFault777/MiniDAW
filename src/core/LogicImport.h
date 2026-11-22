// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Logic Pro projects (.logicx). A .logicx is a package: a folder with the arrangement in a proprietary binary file
// (Alternatives/<n>/ProjectData) and the recordings in Media/. We do not read ProjectData (it is not documented, and its
// regions, plug-ins and automation have no stable public description). What we can do honestly is bring in the audio:
// every recording and bounce in the package becomes a track, so the material of a Logic session can be used here.
#include <string>
#include <vector>

#include "LogicModel.h"

namespace core {

class Session;

struct LogicPackage {
    bool found = false;                       ///< the path is a folder that looks like a Logic Pro package
    std::string name;                         ///< project name (folder name without .logicx)
    bool hasProjectData = false;              ///< Alternatives/*/ProjectData exists (the part we cannot read)
    std::vector<std::string> audioFiles;      ///< Media/Audio Files, readable formats, sorted by path
    std::vector<std::string> bounces;         ///< Media/Bounces (and Bounces/), readable formats
    int skippedUnreadable = 0;                ///< audio files in formats we cannot read (e.g. .caf)
};

/// True for a path ending in .logicx (file or folder; a file cannot be a package, see importLogicPackage).
bool isLogicPath(const std::string& path);

/// Looks inside a package folder. Never throws; 'found' is false for anything else.
LogicPackage scanLogicPackage(const std::string& folder);

/// Stage "apply": creates tracks, audio regions (at their positions), MIDI regions with notes and the tempo / meter map of 'project'.
/// Audio files are looked up by name in the package's Media folder. Counts are reported in 'summary'.
struct LogicApplyStats { int tracks = 0, audioRegions = 0, midiRegions = 0, missingAudio = 0; bool tempo = false; };
/// Reads what the plists of a package say: Resources/ProjectInformation.plist (LastSavedFrom) and the newest
/// Alternatives/<n>/MetaData.plist (BeatsPerMinute, SampleRate, NumberOfTracks). Fills only what is found and not yet set.
void readLogicMetadata(const std::string& packageFolder, LogicProject& project);

LogicApplyStats applyLogicProject(Session& session, const LogicProject& project, const LogicPackage& package);

/// Whole pipeline: package reader -> ProjectData parser -> project model -> session. When no decoder understands ProjectData the
/// recordings are imported as below.
/// Imports the recordings of a package, one track per file, all starting at bar 1 (like Logic's own "Import > Audio Files" of a
/// whole recording). Bounces are only imported when there are no recordings. At most 'maxFiles' files. Returns the number of
/// files queued (decoding runs in the background) and sets 'summary' to a message for the user (also on 0).
int importLogicPackage(Session& session, const std::string& folder, std::string* summary, int maxFiles = 64);

}  // namespace core
