// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// MP3 encoding with the operating system's encoder (Media Foundation on Windows). Nothing is linked or shipped for it.
#include <string>
#include <vector>

namespace platform {

/// True when this platform can encode MP3 files.
bool mp3EncoderAvailable();

/// Encodes interleaved stereo float samples to an MP3 file (constant bit rate). 'sampleRate' must be 44100 or 48000 and 'kbps' one of
/// 128, 160, 192, 224, 256, 320. Blocking: call it from a worker thread or accept the wait. Tags are added by the caller.
bool encodeMp3(const std::vector<float>& stereo, int sampleRate, int kbps, const std::string& path, std::string* error);

}  // namespace platform
