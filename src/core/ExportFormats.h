// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The choices of the Bounce / Export dialogs, in one place for every user interface (and testable).
#include <string>
#include <vector>

#include "Model.h"

namespace core {

/// "WAV 16-bit", "WAV 24-bit", "WAV 32-bit float", "AIFF 16-bit", "AIFF 24-bit", "MP3".
const std::vector<std::string>& exportFormatNames();
/// "44.1 kHz", "48 kHz", "88.2 kHz", "96 kHz".
const std::vector<std::string>& exportRateNames();
/// "128 kbps" ... "320 kbps".
const std::vector<std::string>& exportMp3RateNames();

/// Fills container / sample format / sample rate / MP3 bit rate of 'opt' from the indices of the three choices above.
void applyExportChoices(ExportOptions& opt, int formatIndex, int rateIndex, int mp3Index);

std::string exportExtension(ExportContainer c);   ///< ".wav", ".aiff" or ".mp3"
std::string exportFilter(ExportContainer c);      ///< file dialog filter, "WAV audio|*.wav" ...
/// Keeps the extension the user typed when it fits the container (.aif for AIFF), replaces any other and appends one when missing.
std::string withExportExtension(const std::string& path, ExportContainer c);

}  // namespace core
