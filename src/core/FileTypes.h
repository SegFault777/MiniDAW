// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Which kind of file is this? One place decides it, so File > Open, drag and drop and "open with" all agree.
#include <string>
#include <vector>

namespace core {

enum class FileKind { Project, Audio, Midi, Logic, Unknown };   ///< Logic = a Logic Pro package (.logicx folder), see core/LogicImport

/// Lower-case extension including the dot (".mp3"), or "" when there is none. Works with / and \ separators.
std::string fileExtension(const std::string& path);

/// Decides by extension: .mdaw = Project, audio formats = Audio, .mid / .midi / .kar = Midi.
FileKind classifyFile(const std::string& path);

/// Audio extensions the program tries to read (the platform layer may still fail on a given file, for example when the
/// operating system has no codec for it).
const std::vector<std::string>& audioExtensions();
const std::vector<std::string>& midiExtensions();

/// File dialog filters in the "Name|*.a;*.b|Name 2|*.c" form used by the dialogs of the user interfaces.
std::string openFilter();    ///< "All supported files" first, then projects, audio and MIDI files
std::string audioFilter();   ///< audio files only
std::string midiFilter();    ///< MIDI files only

/// "mdaw, wav, mp3, ..." for messages.
std::string supportedExtensionsText();

}  // namespace core
