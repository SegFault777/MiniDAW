// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Qt-free foundation of the MiniDAW core: constants, text formatting and small string helpers.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <type_traits>

constexpr int kSampleRate = 44100;
constexpr int kPeakStride = 256;   // frames summarised by one waveform peak
constexpr int kDrumRows = 4;       // step sequencer rows: kick, snare, hi-hat, clap
constexpr int kSteps = 16;

namespace core {

using i64 = std::int64_t;

/// Translated text with Qt-style numbered placeholders: Text("%1 copy").arg(name).
class Text {
public:
    Text(const char* s) : m_s(s) {}
    Text(std::string s) : m_s(std::move(s)) {}

    Text arg(const std::string& v) const { return replaceLowest(v); }
    Text arg(const char* v) const { return replaceLowest(v); }
    template <class T, class = std::enable_if_t<std::is_arithmetic_v<T> && !std::is_same_v<T, bool>>>
    Text arg(T v) const {
        if constexpr (std::is_floating_point_v<T>) return arg(double(v), 0, 'g', -1);
        else return replaceLowest(std::to_string(v));
    }
    /// Like QString::arg(double, fieldWidth, format, precision).
    Text arg(double v, int width, char format, int precision) const {
        char fmt[16], buf[64];
        if (precision < 0) std::snprintf(fmt, sizeof fmt, "%%%d%c", width, format);
        else std::snprintf(fmt, sizeof fmt, "%%%d.%d%c", width, precision, format);
        std::snprintf(buf, sizeof buf, fmt, v);
        return replaceLowest(buf);
    }
    const std::string& str() const { return m_s; }
    operator const std::string&() const { return m_s; }

private:
    Text replaceLowest(const std::string& value) const {
        int lowest = 100;   // the lowest %N that is present gets replaced, like QString::arg
        for (size_t i = 0; i + 1 < m_s.size(); ++i)
            if (m_s[i] == '%' && m_s[i + 1] >= '1' && m_s[i + 1] <= '9') lowest = std::min(lowest, m_s[i + 1] - '0');
        if (lowest == 100) return *this;
        const std::string key = "%" + std::to_string(lowest);
        std::string out;
        for (size_t i = 0; i < m_s.size();) {
            if (m_s.compare(i, key.size(), key) == 0 && !(i + key.size() < m_s.size() && std::isdigit(static_cast<unsigned char>(m_s[i + key.size()])))) {
                out += value;
                i += key.size();
            } else {
                out += m_s[i++];
            }
        }
        return Text(out);
    }
    std::string m_s;
};

inline bool startsWith(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }
inline bool endsWith(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0; }
std::string trimmed(const std::string& s);

/// Sets *error (when not null) and returns false, for "return fail(error, text)".
inline bool fail(std::string* error, const std::string& text) {
    if (error) *error = text;
    return false;
}

}  // namespace core
