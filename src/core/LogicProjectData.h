// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Stage "ProjectData binary parser" of the Logic Pro import:
//   .logicx folder -> package reader -> ProjectData parser -> LogicProject -> Session
// ProjectData is Logic's proprietary binary file and has no public specification. A decoder for a given layout is therefore a
// pluggable function; this file holds the plug-in point, the choice of the right ProjectData file, and a structural survey
// that tells what a file looks like (used for diagnostics and for writing a decoder against real files).
// No decoder for real Logic files ships yet: see docs/dev/logicx.md.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "LogicModel.h"

namespace core {

struct ProjectDataSurvey {
    std::size_t size = 0;
    std::uint32_t magic = 0;                                  ///< first four bytes, big endian
    std::vector<std::pair<std::string, int>> fourCCs;         ///< printable 4-character tags seen often (either byte order), with counts
    std::vector<std::string> strings;                         ///< printable strings (ASCII or UTF-16LE) of 4+ characters, first 200
};

struct ProjectDataResult {
    bool decoded = false;          ///< a decoder understood the file and 'project' is filled
    LogicProject project;
    ProjectDataSurvey survey;
    std::string note;              ///< why nothing was decoded, or which decoder worked
};

/// A decoder returns true when it recognises the layout. It must not trust the file: bounds-check every read.
using ProjectDataDecoder = std::function<bool(const std::uint8_t* data, std::size_t size, LogicProject& out, std::string& decoderName)>;

/// Registers a decoder; they are tried in the order of registration. Returns false for an empty function.
bool registerProjectDataDecoder(ProjectDataDecoder d);
void clearProjectDataDecoders();      ///< for tests

ProjectDataSurvey surveyProjectData(const std::uint8_t* data, std::size_t size);
ProjectDataResult parseProjectData(const std::uint8_t* data, std::size_t size);

/// The ProjectData file to read of a package: the highest-numbered Alternatives/<n>/ProjectData (Logic's current alternative is
/// usually the last one saved), or "" when there is none.
std::string findProjectDataFile(const std::string& packageFolder);

}  // namespace core
