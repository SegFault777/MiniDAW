// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <algorithm>
#include "AudioIO.h"

#include <cstring>
#include <fstream>
#include "Files.h"

namespace core {

void Resampler::process(const std::vector<float>& in, int srcRate, std::vector<float>& out) {
    const int n = int(in.size() / 2);
    if (n == 0) return;
    if (srcRate == kSampleRate || srcRate <= 0) {
        out.insert(out.end(), in.begin(), in.end());
        return;
    }
    const double ratio = double(srcRate) / kSampleRate;
    auto get = [&](int i, int c) { return i < 0 ? last[c] : in[size_t(2 * i + c)]; };
    while (pos < n - 1) {
        const int i0 = int(std::floor(pos));
        const float fr = float(pos - i0);
        for (int c = 0; c < 2; ++c) out.push_back(get(i0, c) * (1.f - fr) + get(i0 + 1, c) * fr);
        pos += ratio;
    }
    pos -= n;  // sample n-1 becomes index -1 of the next chunk
    last[0] = in[size_t(2 * (n - 1))];
    last[1] = in[size_t(2 * (n - 1) + 1)];
}

void computePeaks(AudioData& d) {
    d.peaks.assign(size_t((d.frames() + kPeakStride - 1) / kPeakStride), 0.f);
    for (i64 i = 0; i < d.frames(); ++i) {
        float& pk = d.peaks[size_t(i / kPeakStride)];
        pk = std::max({pk, std::fabs(d.pcm[size_t(2 * i)]), std::fabs(d.pcm[size_t(2 * i + 1)])});
    }
}

bool writeWavFile(const std::string& path, const std::vector<float>& pcm, std::string* error) {
    std::ofstream f(toPath(path), std::ios::binary | std::ios::trunc);
    if (!f) return fail(error, "cannot write " + path);
    const auto header = dsp::wavHeader(dsp::WavFormat::Pcm16, kSampleRate, pcm.size() / 2);
    f.write(reinterpret_cast<const char*>(header.data()), std::streamsize(header.size()));
    std::vector<std::uint8_t> bytes;
    std::uint32_t seed = 1;
    const size_t chunk = 1 << 16;   // frames per write
    for (size_t at = 0; at < pcm.size() / 2; at += chunk) {
        const size_t n = std::min(chunk, pcm.size() / 2 - at);
        bytes.clear();
        dsp::encodeSamples(pcm.data() + at * 2, n, dsp::WavFormat::Pcm16, 1.f, false, seed, bytes);
        f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    }
    if (!f) return fail(error, "cannot write " + path);
    return true;
}

namespace {
std::uint32_t le32(const unsigned char* p) { return p[0] | p[1] << 8 | p[2] << 16 | std::uint32_t(p[3]) << 24; }
std::uint16_t le16(const unsigned char* p) { return std::uint16_t(p[0] | p[1] << 8); }
}  // namespace

bool readWavFile(const std::string& path, std::vector<float>& pcm, std::string* error) {
    std::ifstream f(toPath(path), std::ios::binary);
    if (!f) return fail(error, "cannot open " + path);
    std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (b.size() < 12 || std::memcmp(b.data(), "RIFF", 4) != 0 || std::memcmp(b.data() + 8, "WAVE", 4) != 0)
        return fail(error, "not a WAV file");
    int fmtTag = 0, channels = 0, rate = 0, bits = 0;
    const unsigned char* data = nullptr;
    size_t dataLen = 0;
    for (size_t at = 12; at + 8 <= b.size();) {
        const size_t len = le32(&b[at + 4]);
        const unsigned char* body = &b[at + 8];
        const size_t avail = std::min(len, b.size() - (at + 8));
        if (std::memcmp(&b[at], "fmt ", 4) == 0 && avail >= 16) {
            fmtTag = le16(body);
            channels = le16(body + 2);
            rate = int(le32(body + 4));
            bits = le16(body + 14);
            if (fmtTag == 0xFFFE && avail >= 26) fmtTag = le16(body + 24);   // WAVE_FORMAT_EXTENSIBLE: sub-format
        } else if (std::memcmp(&b[at], "data", 4) == 0) {
            data = body;
            dataLen = avail;
        }
        at += 8 + len + (len & 1);
    }
    if (!data || channels < 1 || rate < 1000 || (fmtTag != 1 && fmtTag != 3)) return fail(error, "unsupported WAV format");
    const int bytesPer = bits / 8;
    if (!(bytesPer == 1 || bytesPer == 2 || bytesPer == 3 || bytesPer == 4) || (fmtTag == 3 && bytesPer != 4)) return fail(error, "unsupported WAV format");
    const size_t frames = dataLen / (size_t(bytesPer) * size_t(channels));
    auto sample = [&](size_t frame, int ch) -> float {
        const unsigned char* p = data + (frame * size_t(channels) + size_t(ch)) * size_t(bytesPer);
        if (fmtTag == 3) { float v; std::memcpy(&v, p, 4); return v; }
        switch (bytesPer) {
        case 1: return (int(p[0]) - 128) / 128.f;
        case 2: return std::int16_t(le16(p)) / 32768.f;
        case 3: { int v = p[0] | p[1] << 8 | p[2] << 16; if (v & 0x800000) v -= 0x1000000; return float(v / 8388608.0); }
        default: return float(std::int32_t(le32(p)) / 2147483648.0);
        }
    };
    std::vector<float> st(frames * 2);
    for (size_t i = 0; i < frames; ++i) {
        st[2 * i] = sample(i, 0);
        st[2 * i + 1] = sample(i, channels > 1 ? 1 : 0);   // mono is duplicated
    }
    pcm.clear();
    Resampler rs;
    rs.process(st, rate, pcm);
    return true;
}

}  // namespace core
