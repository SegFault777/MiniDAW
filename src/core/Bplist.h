// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Minimal reader for Apple binary property lists ("bplist00"), as used in a Logic Pro package (MetaData.plist ...).
// Read only, bounds-checked, no dates / sets / UIDs beyond what is needed (they come back as empty values).
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace core {

struct PlistValue {
    enum class Type { None, Bool, Int, Real, String, Data, Array, Dict } type = Type::None;
    bool b = false;
    std::int64_t i = 0;
    double r = 0;
    std::string s;                                       ///< String (UTF-8) or Data (raw bytes)
    std::vector<PlistValue> array;
    std::vector<std::pair<std::string, PlistValue>> dict;   ///< in file order

    const PlistValue* find(const std::string& key) const;
    double number(double fallback = 0) const { return type == Type::Real ? r : type == Type::Int ? double(i) : fallback; }
    std::string text(const std::string& fallback = {}) const { return type == Type::String ? s : fallback; }
};

/// Parses a binary plist. Returns false (and sets 'error') for anything else.
bool parseBplist(const std::uint8_t* data, std::size_t size, PlistValue& out, std::string* error = nullptr);

}  // namespace core
