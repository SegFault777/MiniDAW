// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Id3.h"

#include <fstream>
#include <iterator>

#include "Base.h"
#include "Files.h"

namespace core {

namespace {

// UTF-8 -> UTF-16LE code units
std::vector<std::uint16_t> utf16(const std::string& s) {
    std::vector<std::uint16_t> out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        std::uint32_t cp = 0xFFFD;
        int len = 1;
        if (c < 0x80) cp = c;
        else if ((c >> 5) == 6 && i + 1 < s.size()) { cp = (c & 0x1Fu) << 6 | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu); len = 2; }
        else if ((c >> 4) == 14 && i + 2 < s.size()) { cp = (c & 0x0Fu) << 12 | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 6 | (static_cast<unsigned char>(s[i + 2]) & 0x3Fu); len = 3; }
        else if ((c >> 3) == 30 && i + 3 < s.size()) { cp = (c & 0x07u) << 18 | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 12 | (static_cast<unsigned char>(s[i + 2]) & 0x3Fu) << 6 | (static_cast<unsigned char>(s[i + 3]) & 0x3Fu); len = 4; }
        i += size_t(len);
        if (cp >= 0x10000) { cp -= 0x10000; out.push_back(std::uint16_t(0xD800 + (cp >> 10))); out.push_back(std::uint16_t(0xDC00 + (cp & 0x3FF))); }
        else out.push_back(std::uint16_t(cp));
    }
    return out;
}

void putUtf16(std::vector<std::uint8_t>& b, const std::string& s) {   // BOM + text + terminator
    b.push_back(0xFF); b.push_back(0xFE);
    for (std::uint16_t u : utf16(s)) { b.push_back(std::uint8_t(u)); b.push_back(std::uint8_t(u >> 8)); }
    b.push_back(0); b.push_back(0);
}

void frame(std::vector<std::uint8_t>& tag, const char* id, const std::vector<std::uint8_t>& body) {
    for (int i = 0; i < 4; ++i) tag.push_back(std::uint8_t(id[i]));
    const std::uint32_t n = std::uint32_t(body.size());
    tag.push_back(std::uint8_t(n >> 24)); tag.push_back(std::uint8_t(n >> 16)); tag.push_back(std::uint8_t(n >> 8)); tag.push_back(std::uint8_t(n));   // v2.3: plain big-endian size
    tag.push_back(0); tag.push_back(0);                                                                                                                // flags
    tag.insert(tag.end(), body.begin(), body.end());
}

void textFrame(std::vector<std::uint8_t>& tag, const char* id, const std::string& text) {
    if (text.empty()) return;
    std::vector<std::uint8_t> body = {1};   // encoding: UTF-16 with BOM
    putUtf16(body, text);
    frame(tag, id, body);
}

}  // namespace

std::vector<std::uint8_t> id3v2Tag(const Id3Tags& t) {
    std::vector<std::uint8_t> frames;
    textFrame(frames, "TIT2", t.title);
    textFrame(frames, "TPE1", t.artist);
    textFrame(frames, "TALB", t.album);
    textFrame(frames, "TYER", t.year);
    if (!t.comment.empty()) {
        std::vector<std::uint8_t> body = {1, 'e', 'n', 'g'};
        body.push_back(0xFF); body.push_back(0xFE); body.push_back(0); body.push_back(0);   // empty description
        putUtf16(body, t.comment);
        frame(frames, "COMM", body);
    }
    textFrame(frames, "TENC", "MiniDAW");
    std::vector<std::uint8_t> tag = {'I', 'D', '3', 3, 0, 0};
    const std::uint32_t n = std::uint32_t(frames.size());   // the header stores the size as 4 x 7 bits
    tag.push_back(std::uint8_t((n >> 21) & 0x7F)); tag.push_back(std::uint8_t((n >> 14) & 0x7F)); tag.push_back(std::uint8_t((n >> 7) & 0x7F)); tag.push_back(std::uint8_t(n & 0x7F));
    tag.insert(tag.end(), frames.begin(), frames.end());
    return tag;
}

std::size_t id3v2Size(const std::uint8_t* d, std::size_t len) {
    if (len < 10 || d[0] != 'I' || d[1] != 'D' || d[2] != '3') return 0;
    if ((d[6] | d[7] | d[8] | d[9]) & 0x80) return 0;   // not a synch-safe size: not a tag
    std::size_t size = std::size_t(d[6]) << 21 | std::size_t(d[7]) << 14 | std::size_t(d[8]) << 7 | d[9];
    size += 10 + ((d[5] & 0x10) ? 10 : 0);              // header (+ footer when the flag says so)
    return size <= len ? size : 0;
}

bool prependId3v2(const std::string& path, const Id3Tags& tags, std::string* error) {
    std::vector<std::uint8_t> audio;
    {
        std::ifstream in(toPath(path), std::ios::binary);
        if (!in) return fail(error, "cannot open " + path);
        audio.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    const std::size_t old = id3v2Size(audio.data(), audio.size());
    const auto tag = id3v2Tag(tags);
    std::ofstream out(toPath(path), std::ios::binary | std::ios::trunc);
    if (!out) return fail(error, "cannot write " + path);
    out.write(reinterpret_cast<const char*>(tag.data()), std::streamsize(tag.size()));
    out.write(reinterpret_cast<const char*>(audio.data() + old), std::streamsize(audio.size() - old));
    if (!out) return fail(error, "cannot write " + path);
    return true;
}

}  // namespace core
