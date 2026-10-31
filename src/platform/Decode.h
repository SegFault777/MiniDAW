// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Qt-free audio file decoding. WAV is read by the core everywhere; MP3, AAC / M4A, FLAC and WMA are decoded
// with the operating system's codecs (Media Foundation on Windows).
#include <string>
#include <vector>

namespace platform {

/// Decodes a whole file to interleaved 44.1 kHz stereo float. Blocking: call it from a worker thread.
bool decodeFile(const std::string& path, std::vector<float>& pcm, std::string* error);

/// File name extensions (lower case, with the dot) that decodeFile may be able to read on this platform.
std::vector<std::string> decodableExtensions();

}  // namespace platform
