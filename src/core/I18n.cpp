// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "I18n.h"

#include <algorithm>
#include "Base.h"
#include "Files.h"

namespace core {

namespace {
std::string unescape(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const char c = s[i + 1];
            if (c == 'n') { out += '\n'; ++i; continue; }
            if (c == 't') { out += '\t'; ++i; continue; }
            if (c == '\\') { out += '\\'; ++i; continue; }
        }
        out += s[i];
    }
    return out;
}
}  // namespace

bool TranslationTable::parse(const std::string& text) {
    m_map.clear();
    m_name.clear();
    size_t at = 0;
    bool first = true;
    while (at <= text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (first) {
            first = false;
            if (startsWith(line, "\xEF\xBB\xBF")) line.erase(0, 3);   // UTF-8 byte order mark
        }
        if (line.empty()) continue;
        if (line[0] == '#') {
            if (startsWith(line, "#name\t")) m_name = line.substr(6);
            continue;
        }
        const size_t tab = line.find('\t');
        if (tab == std::string::npos || tab == 0) continue;
        const std::string value = unescape(line.substr(tab + 1));
        if (!value.empty()) m_map[unescape(line.substr(0, tab))] = value;
    }
    return !m_map.empty();
}

bool TranslationTable::load(const std::string& path) {
    std::string text;
    return readFile(path, text) && parse(text);
}

const std::string* TranslationTable::find(const std::string& source) const {
    const auto it = m_map.find(source);
    return it == m_map.end() ? nullptr : &it->second;
}

const std::string& TranslationTable::translate(const std::string& source) const {
    const std::string* t = find(source);
    return t ? *t : source;
}

std::vector<LanguageInfo> findLanguages(const std::string& folder) {
    std::vector<LanguageInfo> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(toPath(folder), ec)) {
        if (!e.is_regular_file() || e.path().extension() != ".tsv") continue;
        LanguageInfo l;
        l.path = toUtf8(e.path());
        l.code = toUtf8(e.path().stem());
        l.name = l.code;
        TranslationTable t;
        std::string text;
        if (readFile(l.path, text)) {
            t.parse(text);
            if (!t.languageName().empty()) l.name = t.languageName();
        }
        out.push_back(std::move(l));
    }
    std::sort(out.begin(), out.end(), [](const LanguageInfo& a, const LanguageInfo& b) { return a.code < b.code; });
    return out;
}

std::string pickLanguage(const std::vector<LanguageInfo>& languages, const std::string& locale) {
    for (const auto& l : languages)
        if (l.code == locale) return l.code;
    const std::string lang = locale.substr(0, locale.find('_'));
    for (const auto& l : languages)
        if (l.code == lang) return l.code;
    if (startsWith(locale, "zh"))
        for (const auto& l : languages)
            if (startsWith(l.code, "zh")) return l.code;
    return {};
}

}  // namespace core
