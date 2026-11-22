// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "AppPaths.h"

namespace core {

AppPaths resolveAppPaths(const std::string& exeDir, const std::string& localAppData, const std::function<bool(const std::string&)>& exists, char separator) {
    AppPaths p;
    p.exeDir = exeDir;
    if (exists && exists(exeDir + separator + kPortableMarker)) {
        p.portable = true;
        p.dataDir = exeDir + separator + "data";
    } else {
        p.dataDir = (localAppData.empty() ? exeDir : localAppData) + separator + "MiniDAW";
    }
    return p;
}

}  // namespace core
