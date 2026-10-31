// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace core {

const Json::Array& Json::emptyArray() { static const Array a; return a; }
const Json::Object& Json::emptyObject() { static const Object o; return o; }

const Json* Json::find(const std::string& key) const {
    if (m_type != Type::Object) return nullptr;
    for (const Member& m : m_obj)
        if (m.first == key) return &m.second;
    return nullptr;
}

const Json& Json::operator[](const std::string& key) const {
    static const Json null;
    const Json* j = find(key);
    return j ? *j : null;
}

Json& Json::operator[](const std::string& key) {
    if (m_type == Type::Null) m_type = Type::Object;
    for (Member& m : m_obj)
        if (m.first == key) return m.second;
    m_obj.emplace_back(key, Json());
    return m_obj.back().second;
}

void Json::push(Json v) {
    if (m_type == Type::Null) m_type = Type::Array;
    m_arr.push_back(std::move(v));
}

// ------------------------------------------------------------------ writing
namespace {
void escape(const std::string& s, std::string& out) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (c < 0x20) {
                char b[8];
                std::snprintf(b, sizeof b, "\\u%04x", c);
                out += b;
            } else {
                out += char(c);   // UTF-8 passes through unchanged
            }
        }
    }
    out += '"';
}

void number(double v, std::string& out) {
    if (!std::isfinite(v)) { out += "null"; return; }
    char b[40];
    if (v == std::floor(v) && std::fabs(v) < 1e15) std::snprintf(b, sizeof b, "%.0f", v);
    else std::snprintf(b, sizeof b, "%.17g", v);   // round-trips every double
    out += b;
}
}  // namespace

void Json::write(std::string& out, int indent, int level) const {
    auto nl = [&](int lv) {
        if (indent <= 0) return;
        out += '\n';
        out.append(size_t(lv * indent), ' ');
    };
    switch (m_type) {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += m_bool ? "true" : "false"; break;
    case Type::Number: number(m_num, out); break;
    case Type::String: escape(m_str, out); break;
    case Type::Array:
        if (m_arr.empty()) { out += "[]"; break; }
        out += '[';
        for (size_t i = 0; i < m_arr.size(); ++i) {
            if (i) out += ',';
            nl(level + 1);
            m_arr[i].write(out, indent, level + 1);
        }
        nl(level);
        out += ']';
        break;
    case Type::Object:
        if (m_obj.empty()) { out += "{}"; break; }
        out += '{';
        for (size_t i = 0; i < m_obj.size(); ++i) {
            if (i) out += ',';
            nl(level + 1);
            escape(m_obj[i].first, out);
            out += indent > 0 ? ": " : ":";
            m_obj[i].second.write(out, indent, level + 1);
        }
        nl(level);
        out += '}';
        break;
    }
}

std::string Json::dump(int indent) const {
    std::string out;
    write(out, indent, 0);
    if (indent > 0) out += '\n';
    return out;
}

// ------------------------------------------------------------------ parsing
namespace {
struct Parser {
    const std::string& s;
    size_t i = 0;
    std::string err;
    int depth = 0;

    bool fail(const char* what) {
        if (err.empty()) err = std::string(what) + " at offset " + std::to_string(i);
        return false;
    }
    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i; }
    bool lit(const char* w) {
        size_t n = std::char_traits<char>::length(w);
        if (s.compare(i, n, w) != 0) return false;
        i += n;
        return true;
    }
    static void utf8(unsigned cp, std::string& out) {
        if (cp < 0x80) out += char(cp);
        else if (cp < 0x800) { out += char(0xC0 | cp >> 6); out += char(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += char(0xE0 | cp >> 12); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
        else { out += char(0xF0 | cp >> 18); out += char(0x80 | ((cp >> 12) & 0x3F)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
    }
    bool hex4(unsigned& v) {
        if (i + 4 > s.size()) return fail("bad \\u escape");
        v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= unsigned(c - '0');
            else if (c >= 'a' && c <= 'f') v |= unsigned(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= unsigned(c - 'A' + 10);
            else return fail("bad \\u escape");
        }
        return true;
    }
    bool str(std::string& out) {
        ++i;   // opening quote
        while (i < s.size()) {
            const char c = s[i++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) break;
            const char e = s[i++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u': {
                unsigned cp = 0;
                if (!hex4(cp)) return false;
                if (cp >= 0xD800 && cp < 0xDC00 && s.compare(i, 2, "\\u") == 0) {   // surrogate pair
                    i += 2;
                    unsigned lo = 0;
                    if (!hex4(lo)) return false;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                utf8(cp, out);
                break;
            }
            default: return fail("bad escape");
            }
        }
        return fail("unterminated string");
    }
    bool value(Json& out) {
        ws();
        if (i >= s.size()) return fail("unexpected end");
        if (++depth > 200) return fail("nesting too deep");
        struct Guard { int& d; ~Guard() { --d; } } g{depth};
        const char c = s[i];
        if (c == '{') {
            ++i;
            out = Json::object();
            ws();
            if (i < s.size() && s[i] == '}') { ++i; return true; }
            for (;;) {
                ws();
                if (i >= s.size() || s[i] != '"') return fail("expected a key");
                std::string k;
                if (!str(k)) return false;
                ws();
                if (i >= s.size() || s[i] != ':') return fail("expected ':'");
                ++i;
                Json v;
                if (!value(v)) return false;
                out[k] = std::move(v);
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; return true; }
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            ++i;
            out = Json::array();
            ws();
            if (i < s.size() && s[i] == ']') { ++i; return true; }
            for (;;) {
                Json v;
                if (!value(v)) return false;
                out.push(std::move(v));
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; return true; }
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') {
            std::string v;
            if (!str(v)) return false;
            out = Json(std::move(v));
            return true;
        }
        if (lit("true")) { out = Json(true); return true; }
        if (lit("false")) { out = Json(false); return true; }
        if (lit("null")) { out = Json(); return true; }
        if (c == '-' || (c >= '0' && c <= '9')) {
            char* end = nullptr;
            const double v = std::strtod(s.c_str() + i, &end);
            if (end == s.c_str() + i) return fail("bad number");
            i = size_t(end - s.c_str());
            out = Json(v);
            return true;
        }
        return fail("unexpected character");
    }
};
}  // namespace

bool Json::parse(const std::string& text, Json& out, std::string* error) {
    Parser p{text, 0, {}, 0};
    Json v;
    if (!p.value(v)) {
        if (error) *error = p.err;
        return false;
    }
    p.ws();
    if (p.i != text.size()) {
        if (error) *error = "trailing characters at offset " + std::to_string(p.i);
        return false;
    }
    out = std::move(v);
    return true;
}

}  // namespace core
