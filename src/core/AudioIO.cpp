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
    pcm = rate == kSampleRate ? std::move(st) : resampleStereo(st, rate, kSampleRate);   // files at 48 / 96 kHz are converted with the good filter
    return true;
}

std::vector<float> resampleStereo(const std::vector<float>& in, int srcRate, int dstRate) {
    if (srcRate == dstRate || in.size() < 4 || srcRate < 1 || dstRate < 1) return in;
    const double ratio = double(dstRate) / double(srcRate);
    const size_t inFrames = in.size() / 2;
    const size_t outFrames = size_t(std::llround(double(inFrames) * ratio));
    const double scale = std::min(1.0, ratio);             // < 1 when the rate goes down: the filter is stretched (anti-aliasing)
    const double cutoff = scale * 0.985;                   // fraction of the lower Nyquist that is kept
    constexpr int kHalfTaps = 24;                          // taps on each side at the lower of the two rates
    const int half = int(std::ceil(kHalfTaps / scale));
    constexpr int kSteps = 256;                            // table resolution per input sample
    const double pi = 3.14159265358979323846;
    auto bessel0 = [](double x) { double s = 1, t = 1; for (int k = 1; k < 40; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; };
    const double beta = 8.6, b0 = bessel0(beta);           // Kaiser window: about 90 dB stop band
    // windowed sinc, tabulated once: h(d) for d in [-half, half] input samples
    std::vector<float> table(size_t(2 * half * kSteps) + 2);
    for (size_t j = 0; j < table.size(); ++j) {
        const double d = double(j) / kSteps - half;
        const double x = d * cutoff;
        const double sinc = std::fabs(x) < 1e-12 ? 1.0 : std::sin(pi * x) / (pi * x);
        const double r = d / half;
        table[j] = std::fabs(r) >= 1.0 ? 0.f : float(cutoff * sinc * bessel0(beta * std::sqrt(1.0 - r * r)) / b0);
    }
    std::vector<float> out(outFrames * 2);
    for (size_t o = 0; o < outFrames; ++o) {
        const double center = double(o) / ratio;
        const long long c = (long long)std::floor(center);
        double accL = 0, accR = 0, wsum = 0;
        for (long long i = c - half + 1; i <= c + half; ++i) {
            const double pos = (double(i) - center + half) * kSteps;   // index into the table
            if (pos < 0 || pos >= double(table.size() - 1)) continue;
            const size_t j = size_t(pos);
            const double frac = pos - double(j);
            const double k = double(table[j]) * (1.0 - frac) + double(table[j + 1]) * frac;
            wsum += k;
            if (i < 0 || size_t(i) >= inFrames) continue;
            accL += k * double(in[size_t(i) * 2]);
            accR += k * double(in[size_t(i) * 2 + 1]);
        }
        const double norm = wsum != 0 ? 1.0 / wsum : 1.0;   // keeps the level exact, also at the edges of the file
        out[o * 2] = float(accL * norm);
        out[o * 2 + 1] = float(accR * norm);
    }
    return out;
}

namespace {
unsigned be16(const unsigned char* p) { return unsigned(p[0] << 8 | p[1]); }
std::uint32_t be32(const unsigned char* p) { return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 | std::uint32_t(p[2]) << 8 | p[3]; }
// IEEE 754 80-bit extended precision (the sample rate of an AIFF file)
double extended80(const unsigned char* p) {
    const int sign = p[0] >> 7, exponent = int(be16(p) & 0x7FFF);
    const std::uint64_t mant = std::uint64_t(be32(p + 2)) << 32 | be32(p + 6);
    if (exponent == 0 && mant == 0) return 0.0;
    const double v = std::ldexp(double(mant), exponent - 16383 - 63);
    return sign ? -v : v;
}
}  // namespace

bool readAiffFile(const std::string& path, std::vector<float>& pcm, std::string* error) {
    std::ifstream f(toPath(path), std::ios::binary);
    if (!f) return fail(error, "cannot open " + path);
    std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (b.size() < 12 || std::memcmp(b.data(), "FORM", 4) != 0 || (std::memcmp(b.data() + 8, "AIFF", 4) != 0 && std::memcmp(b.data() + 8, "AIFC", 4) != 0))
        return fail(error, "not an AIFF file");
    const bool aifc = std::memcmp(b.data() + 8, "AIFC", 4) == 0;
    int channels = 0, bits = 0;
    double rate = 0;
    bool littleEndian = false, isFloat = false, ok = true;
    const unsigned char* data = nullptr;
    size_t dataLen = 0;
    for (size_t at = 12; at + 8 <= b.size();) {
        const size_t len = be32(&b[at + 4]);
        const unsigned char* body = &b[at + 8];
        const size_t avail = std::min(len, b.size() - (at + 8));
        if (std::memcmp(&b[at], "COMM", 4) == 0 && avail >= 18) {
            channels = int(be16(body));
            bits = int(be16(body + 6));
            rate = extended80(body + 8);
            if (aifc && avail >= 22) {
                const std::string comp(reinterpret_cast<const char*>(body + 18), 4);
                if (comp == "sowt") littleEndian = true;
                else if (comp == "fl32" || comp == "FL32") isFloat = true;
                else if (comp != "NONE" && comp != "none") ok = false;   // compressed AIFF-C (ima4, ulaw ...)
            }
        } else if (std::memcmp(&b[at], "SSND", 4) == 0 && avail >= 8) {
            const size_t offset = be32(body);
            if (8 + offset <= avail) { data = body + 8 + offset; dataLen = avail - 8 - offset; }
        }
        at += 8 + len + (len & 1);
    }
    const int bytesPer = (bits + 7) / 8;
    if (!ok || !data || channels < 1 || rate < 1000 || !(bytesPer == 1 || bytesPer == 2 || bytesPer == 3 || bytesPer == 4) || (isFloat && bytesPer != 4))
        return fail(error, "unsupported AIFF format");
    const size_t frames = dataLen / (size_t(bytesPer) * size_t(channels));
    auto sample = [&](size_t frame, int ch) -> float {
        const unsigned char* p = data + (frame * size_t(channels) + size_t(ch)) * size_t(bytesPer);
        if (isFloat) { std::uint32_t u = be32(p); float v; std::memcpy(&v, &u, 4); return v; }
        switch (bytesPer) {
        case 1: return float(std::int8_t(p[0])) / 128.f;                        // AIFF 8-bit is signed
        case 2: return float(std::int16_t(littleEndian ? p[0] | p[1] << 8 : p[0] << 8 | p[1])) / 32768.f;
        case 3: { int v = p[0] << 16 | p[1] << 8 | p[2]; if (v & 0x800000) v -= 0x1000000; return float(v / 8388608.0); }
        default: return float(std::int32_t(be32(p)) / 2147483648.0);
        }
    };
    std::vector<float> st(frames * 2);
    for (size_t i = 0; i < frames; ++i) {
        st[2 * i] = sample(i, 0);
        st[2 * i + 1] = sample(i, channels > 1 ? 1 : 0);
    }
    const int srcRate = int(std::lround(rate));
    pcm = srcRate == kSampleRate ? std::move(st) : resampleStereo(st, srcRate, kSampleRate);
    return true;
}

}  // namespace core
