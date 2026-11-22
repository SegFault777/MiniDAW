// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Gapless MP3: the "Info" frame (Xing / LAME header) that tells a player how many samples of encoder delay and padding
// surround the music, so exported files start and end exactly where the project does (Logic Pro, Ableton Live, foobar2000 ...
// read it). The same header is read back when an MP3 is imported so that the decoder delay does not shift the audio.
#include <cstdint>
#include <string>
#include <vector>

namespace core {

/// Samples a typical decoder adds before the first music sample (MPEG layer 3 synthesis filter bank).
constexpr int kMp3DecoderDelay = 529;
/// Encoder delay we declare for files from the Windows (Media Foundation) encoder: its output starts about 1105 samples late.
constexpr int kMp3EncoderDelay = 576;

struct Mp3Gapless {
    bool valid = false;            ///< an Info / Xing frame with a LAME extension was found
    std::uint32_t frames = 0;      ///< number of audio frames (without the Info frame)
    std::uint32_t delay = 0;       ///< encoder delay in samples
    std::uint32_t padding = 0;     ///< padding at the end in samples
    int sampleRate = 0;
    /// Number of music samples per channel a gapless decoder returns.
    std::uint64_t musicSamples() const {
        const std::uint64_t all = std::uint64_t(frames) * 1152u;
        return all > std::uint64_t(delay) + padding ? all - delay - padding : 0;
    }
};

/// Inserts an Info frame in front of the audio frames of an MP3 file (without ID3 tag; call before prependId3v2).
/// 'musicSamples' is the length the music should have after decoding. A file that already has an Info / Xing frame is left alone
/// (returns true). Only MPEG-1 layer 3 stereo at a constant bit rate is supported (what our encoder writes).
bool addGaplessHeader(const std::string& path, std::uint64_t musicSamples, int delay, std::string* error);

/// Reads the header of an MP3 file already in memory (an ID3 tag in front is skipped).
Mp3Gapless parseGaplessHeader(const std::uint8_t* data, std::size_t length);
Mp3Gapless readGaplessHeader(const std::string& path);

/// Removes decoder delay and padding from decoded stereo samples (interleaved, at 'outRate'). The decoder may already have done it
/// (some do): the length decides. Returns true when samples were removed.
bool trimMp3Decoded(std::vector<float>& stereo, const Mp3Gapless& info, int outRate);

}  // namespace core
