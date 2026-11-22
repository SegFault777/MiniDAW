// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// ID3v2.3 tags for exported MP3 files (title, artist, ... in the form every player and DAW reads).
#include <cstdint>
#include <string>
#include <vector>

namespace core {

struct Id3Tags {
    std::string title, artist, album, year, comment;   ///< UTF-8
    bool empty() const { return title.empty() && artist.empty() && album.empty() && year.empty() && comment.empty(); }
};

/// The bytes of an ID3v2.3 tag (always contains the encoder name "MiniDAW"). Text is stored as UTF-16 with a byte order mark.
std::vector<std::uint8_t> id3v2Tag(const Id3Tags& tags);

/// Writes 'tag' in front of the audio data of an existing MP3 file. A tag that is already there is replaced.
bool prependId3v2(const std::string& path, const Id3Tags& tags, std::string* error);

/// Size in bytes of an ID3v2 tag at the start of 'data' (0 when there is none).
std::size_t id3v2Size(const std::uint8_t* data, std::size_t length);

}  // namespace core
