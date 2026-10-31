// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Translation tables: plain text files with one "English<TAB>Translation" pair per line. Lines that start with
// '#' are comments; the first line "#name<TAB>Display name" names the language. \n, \t and \\ may be used in
// the text.
#include <string>
#include <unordered_map>
#include <vector>

namespace core {

class TranslationTable {
public:
    /// Parses table text. Returns false when it contains no translation.
    bool parse(const std::string& text);
    bool load(const std::string& path);
    /// The translation of 'source', or nullptr when there is none.
    const std::string* find(const std::string& source) const;
    /// The translation, or 'source' itself.
    const std::string& translate(const std::string& source) const;
    const std::string& languageName() const { return m_name; }
    size_t size() const { return m_map.size(); }

private:
    std::unordered_map<std::string, std::string> m_map;
    std::string m_name;
};

struct LanguageInfo {
    std::string code;   // file name without extension, for example "ko" or "zh_CN"
    std::string name;   // native name from the "#name" line
    std::string path;
};

/// The *.tsv files of a folder, sorted by file name.
std::vector<LanguageInfo> findLanguages(const std::string& folder);

/// Picks the language for a system locale such as "ko_KR": exact match, then the language part, then any
/// Chinese locale falls back to a Chinese table. Returns "" when nothing matches.
std::string pickLanguage(const std::vector<LanguageInfo>& languages, const std::string& locale);

}  // namespace core
