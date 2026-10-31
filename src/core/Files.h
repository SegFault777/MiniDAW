// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// UTF-8 aware file helpers (std::filesystem takes the ANSI code page for char strings on Windows).
#include <filesystem>
#include <string>

namespace core {

namespace fs = std::filesystem;

inline fs::path toPath(const std::string& utf8) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

inline std::string toUtf8(const fs::path& p) {
    const std::u8string s = p.generic_u8string();   // '/' separators on every platform
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

bool readFile(const std::string& path, std::string& out, std::string* error = nullptr);
bool writeFile(const std::string& path, const void* data, size_t size, std::string* error = nullptr);
inline bool writeFile(const std::string& path, const std::string& text, std::string* error = nullptr) {
    return writeFile(path, text.data(), text.size(), error);
}

/// Absolute, normalised UTF-8 path.
std::string absolutePath(const std::string& path);
/// 'path' relative to 'dir' (absolute when that is not possible).
std::string relativePath(const std::string& path, const std::string& dir);
inline bool fileExists(const std::string& path) { std::error_code ec; return fs::exists(toPath(path), ec); }
inline void makeDirs(const std::string& path) { std::error_code ec; fs::create_directories(toPath(path), ec); }
inline void removeFile(const std::string& path) { std::error_code ec; fs::remove(toPath(path), ec); }
inline void removeTree(const std::string& path) { std::error_code ec; fs::remove_all(toPath(path), ec); }
inline std::string parentDir(const std::string& path) { return toUtf8(fs::absolute(toPath(path)).parent_path()); }
inline std::string baseName(const std::string& path) { return toUtf8(toPath(path).stem()); }

}  // namespace core
