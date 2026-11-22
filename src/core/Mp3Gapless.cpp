// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Mp3Gapless.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "Base.h"
#include "Files.h"
#include "Id3.h"

namespace core {

namespace {

constexpr int kBitrates[16] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};   // MPEG-1 layer 3
constexpr int kRates[4] = {44100, 48000, 32000, 0};

struct FrameHead {
    int kbps = 0, rate = 0, size = 0, channels = 2;
    bool mpeg1l3 = false;
};

FrameHead parseHead(const std::uint8_t* p, std::size_t left) {
    FrameHead h;
    if (left < 4 || p[0] != 0xFF || (p[1] & 0xFE) != 0xFA) return h;       // sync, MPEG-1, layer 3
    const int br = p[2] >> 4, sr = (p[2] >> 2) & 3, pad = (p[2] >> 1) & 1;
    if (kBitrates[br] == 0 || kRates[sr] == 0) return h;
    h.kbps = kBitrates[br];
    h.rate = kRates[sr];
    h.size = 144 * h.kbps * 1000 / h.rate + pad;
    h.channels = ((p[3] >> 6) & 3) == 3 ? 1 : 2;
    h.mpeg1l3 = true;
    return h;
}

std::size_t tagLength(const std::uint8_t* d, std::size_t n) { return id3v2Size(d, n); }

void put32(std::uint8_t* p, std::uint32_t v) { p[0] = std::uint8_t(v >> 24); p[1] = std::uint8_t(v >> 16); p[2] = std::uint8_t(v >> 8); p[3] = std::uint8_t(v); }
std::uint32_t get32(const std::uint8_t* p) { return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 | std::uint32_t(p[2]) << 8 | p[3]; }

// offset of the "Info" / "Xing" word inside a frame: header + side information
std::size_t tagOffset(const FrameHead& h) { return 4 + (h.channels == 1 ? 17 : 32); }

std::uint16_t crc16(const std::uint8_t* p, std::size_t n) {
    std::uint16_t crc = 0;
    for (std::size_t i = 0; i < n; ++i) {
        crc ^= std::uint16_t(p[i]) << 8;
        for (int b = 0; b < 8; ++b) crc = std::uint16_t(crc & 0x8000 ? (crc << 1) ^ 0x8005 : crc << 1);
    }
    return crc;
}

}  // namespace

Mp3Gapless parseGaplessHeader(const std::uint8_t* data, std::size_t length) {
    Mp3Gapless g;
    std::size_t at = tagLength(data, length);
    // skip stray zero bytes some tools leave behind the ID3 tag
    while (at < length && data[at] == 0) ++at;
    const FrameHead h = parseHead(data + at, length - at);
    if (!h.mpeg1l3 || at + std::size_t(h.size) > length) return g;
    const std::uint8_t* f = data + at;
    const std::size_t off = tagOffset(h);
    if (std::size_t(h.size) < off + 4 + 4 + 4 + 4 + 36) return g;
    if (std::memcmp(f + off, "Info", 4) != 0 && std::memcmp(f + off, "Xing", 4) != 0) return g;
    const std::uint32_t flags = get32(f + off + 4);
    std::size_t p = off + 8;
    if (!(flags & 1u)) return g;                       // no frame count: nothing to compute from
    g.frames = get32(f + p); p += 4;
    if (flags & 2u) p += 4;
    if (flags & 4u) p += 100;
    if (flags & 8u) p += 4;
    if (p + 36 > std::size_t(h.size)) return g;
    const std::uint8_t* lame = f + p;                  // the LAME extension: 9 bytes encoder name first
    if (std::memcmp(lame, "LAME", 4) != 0 && std::memcmp(lame, "MiniDAW", 7) != 0 && std::memcmp(lame, "Lavf", 4) != 0 && std::memcmp(lame, "Lavc", 4) != 0) return g;
    const std::uint8_t* dp = lame + 21;
    g.delay = std::uint32_t(dp[0]) << 4 | (dp[1] >> 4);
    g.padding = std::uint32_t(dp[1] & 0x0F) << 8 | dp[2];
    g.sampleRate = h.rate;
    g.valid = true;
    return g;
}

Mp3Gapless readGaplessHeader(const std::string& path) {
    std::string all;
    if (!readFile(path, all)) return {};
    return parseGaplessHeader(reinterpret_cast<const std::uint8_t*>(all.data()), all.size());
}

bool addGaplessHeader(const std::string& path, std::uint64_t musicSamples, int delay, std::string* error) {
    std::string raw;
    if (!readFile(path, raw, error)) return false;
    const auto* d = reinterpret_cast<const std::uint8_t*>(raw.data());
    std::size_t at = tagLength(d, raw.size());
    while (at < raw.size() && d[at] == 0) ++at;
    FrameHead first = parseHead(d + at, raw.size() - at);
    if (!first.mpeg1l3 || first.channels != 2) return true;   // not something we can describe: leave the file as it is
    if (parseGaplessHeader(d, raw.size()).valid) return true;
    {   // a plain Xing / Info frame without LAME part: also leave
        const std::size_t off = at + tagOffset(first);
        if (off + 4 <= raw.size() && (std::memcmp(d + off, "Xing", 4) == 0 || std::memcmp(d + off, "Info", 4) == 0)) return true;
    }
    // count the frames
    std::uint32_t frames = 0;
    std::size_t pos = at;
    while (pos < raw.size()) {
        const FrameHead h = parseHead(d + pos, raw.size() - pos);
        if (!h.mpeg1l3 || pos + std::size_t(h.size) > raw.size()) break;
        pos += std::size_t(h.size);
        ++frames;
    }
    if (frames == 0) return true;                          // nothing we can describe: the file stays as the encoder wrote it
    const std::size_t audioEnd = pos;
    const std::uint64_t all = std::uint64_t(frames) * 1152u;
    const std::uint64_t want = delay + musicSamples;
    int padding = all > want ? int(std::min<std::uint64_t>(all - want, 4095)) : 0;
    delay = std::clamp(delay, 0, 4095);

    // the Info frame: same header as the audio (same bit rate, so every player treats it as one more frame)
    std::vector<std::uint8_t> info(std::size_t(first.size), 0);
    std::memcpy(info.data(), d + at, 4);
    info[2] = std::uint8_t(info[2] & ~0x02);              // no padding byte
    const std::size_t infoSize = std::size_t(144 * first.kbps * 1000 / first.rate);
    info.resize(infoSize, 0);
    const std::size_t off = tagOffset(first);
    std::memcpy(info.data() + off, "Info", 4);
    put32(info.data() + off + 4, 3u);                     // frames + bytes
    put32(info.data() + off + 8, frames);
    put32(info.data() + off + 12, std::uint32_t(audioEnd - at + infoSize));
    std::uint8_t* lame = info.data() + off + 16;
    std::memcpy(lame, "LAME-MDAW", 9);                  // players (ffmpeg, ...) only trust the delay fields behind a name starting with "LAME"
    lame[9] = 0x01;                                       // revision 0, CBR
    lame[10] = 0;                                         // low pass: unknown
    lame[19] = 0;                                         // flags
    lame[20] = std::uint8_t(first.kbps >= 255 ? 255 : first.kbps);
    lame[21] = std::uint8_t(delay >> 4);
    lame[22] = std::uint8_t((delay & 0x0F) << 4 | (padding >> 8));
    lame[23] = std::uint8_t(padding & 0xFF);
    put32(lame + 28, std::uint32_t(audioEnd - at + infoSize));   // music length
    const std::uint16_t mcrc = crc16(d + at, audioEnd - at);
    lame[32] = std::uint8_t(mcrc >> 8); lame[33] = std::uint8_t(mcrc);
    const std::uint16_t tcrc = crc16(info.data(), off + 16 + 34);
    lame[34] = std::uint8_t(tcrc >> 8); lame[35] = std::uint8_t(tcrc);

    std::string out;
    out.reserve(raw.size() + infoSize);
    out.append(raw, 0, at);
    out.append(reinterpret_cast<const char*>(info.data()), info.size());
    out.append(raw, at, std::string::npos);
    return writeFile(path, out, error);
}

bool trimMp3Decoded(std::vector<float>& stereo, const Mp3Gapless& info, int outRate) {
    if (!info.valid || info.sampleRate <= 0 || outRate <= 0) return false;
    const double scale = double(outRate) / info.sampleRate;
    const std::uint64_t decoded = stereo.size() / 2;
    const double music = double(info.musicSamples()) * scale;
    const double skip = double(info.delay + kMp3DecoderDelay) * scale;
    const double fullLength = double(std::uint64_t(info.frames) * 1152u) * scale;
    // A decoder that already removed the delay returns about 'music' samples; one that did not returns 'fullLength'.
    const double tolerance = 1152.0 * scale + 2.0;
    if (std::abs(double(decoded) - fullLength) > tolerance) return false;       // already trimmed (or something else): leave
    if (music <= 0 || skip + music > double(decoded) + tolerance) return false;
    const std::uint64_t from = std::uint64_t(std::llround(skip));
    const std::uint64_t count = std::min<std::uint64_t>(std::uint64_t(std::llround(music)), decoded - std::min(decoded, from));
    std::vector<float> out(stereo.begin() + std::ptrdiff_t(from * 2), stereo.begin() + std::ptrdiff_t((from + count) * 2));
    stereo.swap(out);
    return true;
}

}  // namespace core
