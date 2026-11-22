// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Arrangement decoder for Logic Pro ProjectData: tempo map, meter map, markers, audio regions (file, position, trim, length) and
// MIDI regions with their notes. The byte layouts come from the reverse-engineering notes of the LogicProFormatWriter project
// (MIT, https://github.com/jonkubis/LogicProFormatWriter, PROJECTDATA_FORMAT.md and logicx/session.py), re-implemented here and
// checked against a Logic Pro X 10.5 project. See docs/dev/logicx.md.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "LogicModel.h"

namespace core {

struct LogicArrangementStats {
    int exactMidiLengths = 0, tempoPoints = 0, meterPoints = 0, markers = 0, audioRegions = 0, midiRegions = 0, notes = 0, unresolvedAudio = 0;
};

/// Track names in the order of Logic's arrange list (index 0 = track 1; a final empty row of the list is included).
/// Source: the `karT` rows of the arrange list point (object id @row+8) at Environment objects (`ivnE`: object id @+16, name length u16 @+158,
/// UTF-8 name after it). 'trackCountHint' is MetaData's NumberOfTracks (0 = unknown) and picks the arrange list among the `karT` runs.
/// A row whose object has no readable name gives "".
std::vector<std::string> readLogicTrackNames(const std::uint8_t* data, std::size_t size, int trackCountHint);

/// Fills tempo / meters / markers / tracks of 'out' from a ProjectData file in memory (the 24-byte header was already checked).
/// Never throws and never reads outside [data, data + size).
LogicArrangementStats decodeLogicArrangement(const std::uint8_t* data, std::size_t size, LogicProject& out);

}  // namespace core
