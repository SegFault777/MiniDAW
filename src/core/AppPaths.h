// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Where the program keeps its settings, autosaves and user plug-ins. Normal installs use %LOCALAPPDATA%\MiniDAW; a portable
// copy (a file called MiniDAW.portable next to the program) keeps everything in a "data" folder beside the executable, so the
// whole program can live on a USB stick and leave nothing behind on the computer.
#include <functional>
#include <string>

namespace core {

struct AppPaths {
    std::string exeDir;
    std::string dataDir;
    bool portable = false;
};

constexpr const char* kPortableMarker = "MiniDAW.portable";

/// 'exists' tells whether a file exists (so the decision can be tested without touching the disk).
AppPaths resolveAppPaths(const std::string& exeDir, const std::string& localAppData, const std::function<bool(const std::string&)>& exists,
                         char separator = '\\');

}  // namespace core
