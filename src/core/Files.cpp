// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Files.h"

#include <fstream>
#include <iterator>
#include "Base.h"

namespace core {

bool readFile(const std::string& path, std::string& out, std::string* error) {
    std::ifstream f(toPath(path), std::ios::binary);
    if (!f) return fail(error, "cannot open " + path);
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool writeFile(const std::string& path, const void* data, size_t size, std::string* error) {
    std::ofstream f(toPath(path), std::ios::binary | std::ios::trunc);
    if (!f) return fail(error, "cannot write " + path);
    f.write(static_cast<const char*>(data), std::streamsize(size));
    f.flush();
    if (!f) return fail(error, "cannot write " + path);
    return true;
}

std::string absolutePath(const std::string& path) {
    std::error_code ec;
    fs::path p = fs::absolute(toPath(path), ec);
    return toUtf8(p.lexically_normal());
}

std::string relativePath(const std::string& path, const std::string& dir) {
    std::error_code ec;
    const fs::path rel = fs::relative(toPath(absolutePath(path)), toPath(absolutePath(dir)), ec);
    if (ec || rel.empty()) return absolutePath(path);
    return toUtf8(rel);
}

}  // namespace core
