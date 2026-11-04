// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// MiniDAW plug-ins without Qt: shared libraries that follow plugin-sdk/mdplugin.h, found in "plugins" folders.
#include <memory>
#include <string>
#include <vector>
#include "../dsp/Effects.h"

namespace platform {

struct NativePluginInfo {
    std::string id;      ///< "native:<plug-in id>", as saved in projects
    std::string name, vendor, path;
};

/// Loads every plug-in library found in 'folders'; libraries that were loaded before are skipped.
void scanNativePlugins(const std::vector<std::string>& folders);

/// Plug-ins found so far.
std::vector<NativePluginInfo> nativePlugins();

/// Creates an effect from a "native:..." id, or nullptr when that plug-in is not installed.
std::shared_ptr<dsp::Effect> createNativePlugin(const std::string& id);

}  // namespace platform
