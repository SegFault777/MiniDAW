// Test helper: writes a tiny AIFF (or AIFF-C 'sowt') file of 16-bit stereo frames (+0.25 left, -0.25 right).
#pragma once
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

// Builds a tiny AIFF (or AIFF-C 'sowt') file in memory: 'frames' stereo frames of a ramp, 16-bit.
inline std::string writeTestAiff(const std::string& path, bool aifc, bool little, int rate, int frames) {
    std::vector<unsigned char> out;
    auto put32 = [&](std::uint32_t v) { out.push_back(v >> 24); out.push_back(v >> 16); out.push_back(v >> 8); out.push_back(v); };
    auto put16 = [&](unsigned v) { out.push_back(v >> 8); out.push_back(v); };
    auto tag = [&](const char* t) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<unsigned char>(t[i])); };
    auto ext80 = [&](double r) {   // IEEE extended for a positive integer sample rate
        int e = 0; double m = r; while (m >= 2.0) { m /= 2.0; ++e; }
        const std::uint64_t mant = static_cast<std::uint64_t>(m * 9223372036854775808.0);   // 2^63
        put16(unsigned(16383 + e)); put32(std::uint32_t(mant >> 32)); put32(std::uint32_t(mant));
    };
    tag("FORM"); put32(0); tag(aifc ? "AIFC" : "AIFF");
    tag("COMM"); put32(aifc ? 24 : 18); put16(2); put32(std::uint32_t(frames)); put16(16); ext80(rate);
    if (aifc) { tag(little ? "sowt" : "NONE"); out.push_back(0); out.push_back(0); }   // empty pascal string
    tag("SSND"); put32(std::uint32_t(8 + frames * 4)); put32(0); put32(0);
    for (int i = 0; i < frames; ++i)
        for (int ch = 0; ch < 2; ++ch) {
            const int v = ch == 0 ? 8192 : -8192;   // +0.25 left, -0.25 right
            if (little) { out.push_back(static_cast<unsigned char>(v)); out.push_back(static_cast<unsigned char>(v >> 8)); }
            else { out.push_back(static_cast<unsigned char>(v >> 8)); out.push_back(static_cast<unsigned char>(v)); }
        }
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
    return path;
}

