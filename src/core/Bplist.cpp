// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Bplist.h"

#include <cstring>

namespace core {

const PlistValue* PlistValue::find(const std::string& key) const {
    for (const auto& kv : dict) if (kv.first == key) return &kv.second;
    return nullptr;
}

namespace {

struct Reader {
    const std::uint8_t* d; std::size_t n;
    int offSize = 0, refSize = 0;
    std::uint64_t count = 0, offTable = 0;

    bool uintAt(std::size_t pos, int bytes, std::uint64_t& v) const {
        if (bytes < 1 || bytes > 8 || pos + std::size_t(bytes) > n) return false;
        v = 0;
        for (int i = 0; i < bytes; ++i) v = v << 8 | d[pos + std::size_t(i)];
        return true;
    }
    bool objOffset(std::uint64_t idx, std::size_t& off) const {
        if (idx >= count) return false;
        std::uint64_t v;
        if (!uintAt(std::size_t(offTable + idx * std::uint64_t(offSize)), offSize, v) || v >= n) return false;
        off = std::size_t(v);
        return true;
    }
    // length nibble: 0xF means an int object follows
    bool length(std::size_t& pos, std::uint8_t nib, std::uint64_t& len) const {
        if (nib != 0xF) { len = nib; return true; }
        if (pos >= n || (d[pos] & 0xF0) != 0x10) return false;
        const int bytes = 1 << (d[pos] & 0x0F);
        ++pos;
        if (bytes > 8 || !uintAt(pos, bytes, len)) return false;
        pos += std::size_t(bytes);
        return true;
    }
    static void utf8(std::string& out, std::uint32_t cp) {
        if (cp < 0x80) out += char(cp);
        else if (cp < 0x800) { out += char(0xC0 | cp >> 6); out += char(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += char(0xE0 | cp >> 12); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
        else { out += char(0xF0 | cp >> 18); out += char(0x80 | ((cp >> 12) & 0x3F)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
    }
    bool parse(std::uint64_t idx, PlistValue& out, int depth) const {
        std::size_t pos;
        if (depth > 40 || !objOffset(idx, pos) || pos >= n) return false;
        const std::uint8_t m = d[pos++];
        const std::uint8_t hi = m >> 4, lo = m & 0x0F;
        switch (hi) {
        case 0x0:
            if (lo == 0x8 || lo == 0x9) { out.type = PlistValue::Type::Bool; out.b = lo == 0x9; }
            return true;
        case 0x1: {
            const int bytes = 1 << lo;
            std::uint64_t v;
            if (bytes > 8 || !uintAt(pos, bytes, v)) return false;
            out.type = PlistValue::Type::Int; out.i = std::int64_t(v);
            return true;
        }
        case 0x2: {
            const int bytes = 1 << lo;
            std::uint64_t v;
            if ((bytes != 4 && bytes != 8) || !uintAt(pos, bytes, v)) return false;
            out.type = PlistValue::Type::Real;
            if (bytes == 4) { std::uint32_t u = std::uint32_t(v); float f; std::memcpy(&f, &u, 4); out.r = double(f); }
            else { double x; std::memcpy(&x, &v, 8); out.r = x; }
            return true;
        }
        case 0x4: case 0x5: case 0x6: {
            std::uint64_t len;
            if (!length(pos, lo, len)) return false;
            const std::uint64_t bytes = hi == 0x6 ? len * 2 : len;
            if (bytes > n || pos + std::size_t(bytes) > n) return false;
            if (hi == 0x6) {
                out.type = PlistValue::Type::String;
                for (std::uint64_t i = 0; i < len; ++i) {
                    std::uint32_t cp = std::uint32_t(d[pos + 2 * i]) << 8 | d[pos + 2 * i + 1];
                    if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < len) {
                        const std::uint32_t lo2 = std::uint32_t(d[pos + 2 * i + 2]) << 8 | d[pos + 2 * i + 3];
                        if (lo2 >= 0xDC00 && lo2 < 0xE000) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo2 - 0xDC00); ++i; }
                    }
                    utf8(out.s, cp);
                }
            } else {
                out.type = hi == 0x5 ? PlistValue::Type::String : PlistValue::Type::Data;
                out.s.assign(reinterpret_cast<const char*>(d + pos), std::size_t(len));
            }
            return true;
        }
        case 0xA: case 0xD: {
            std::uint64_t len;
            if (!length(pos, lo, len)) return false;
            const std::uint64_t refs = hi == 0xD ? len * 2 : len;
            if (refs > n || pos + std::size_t(refs * std::uint64_t(refSize)) > n) return false;
            auto ref = [&](std::uint64_t k) { std::uint64_t v = 0; uintAt(pos + std::size_t(k * std::uint64_t(refSize)), refSize, v); return v; };
            if (hi == 0xA) {
                out.type = PlistValue::Type::Array;
                out.array.resize(std::size_t(len));
                for (std::uint64_t i = 0; i < len; ++i) if (!parse(ref(i), out.array[std::size_t(i)], depth + 1)) return false;
            } else {
                out.type = PlistValue::Type::Dict;
                for (std::uint64_t i = 0; i < len; ++i) {
                    PlistValue k, v;
                    if (!parse(ref(i), k, depth + 1) || !parse(ref(len + i), v, depth + 1)) return false;
                    out.dict.emplace_back(k.s, std::move(v));
                }
            }
            return true;
        }
        default: return true;   // dates, UIDs, sets: empty value
        }
    }
};

}  // namespace

bool parseBplist(const std::uint8_t* data, std::size_t size, PlistValue& out, std::string* error) {
    auto fail = [&](const char* m) { if (error) *error = m; return false; };
    if (!data || size < 8 + 32 || std::memcmp(data, "bplist00", 8) != 0) return fail("Not a binary property list.");
    Reader r{data, size};
    const std::uint8_t* t = data + size - 32;
    r.offSize = t[6]; r.refSize = t[7];
    std::uint64_t top = 0;
    auto be = [&](const std::uint8_t* p) { std::uint64_t v = 0; for (int i = 0; i < 8; ++i) v = v << 8 | p[i]; return v; };
    r.count = be(t + 8); top = be(t + 16); r.offTable = be(t + 24);
    if (r.offSize < 1 || r.offSize > 8 || r.refSize < 1 || r.refSize > 8 || r.count == 0 || r.count > size ||
        r.offTable >= size || r.offTable + r.count * std::uint64_t(r.offSize) > size - 32 + 0) return fail("Damaged property list trailer.");
    out = PlistValue{};
    if (!r.parse(top, out, 0)) return fail("Damaged property list.");
    return true;
}

}  // namespace core
