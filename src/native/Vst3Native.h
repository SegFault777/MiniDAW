// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// VST3 hosting for the native (Qt-free) application. The real implementation (Vst3Native.cpp) needs the Steinberg
// VST3 SDK and is only compiled when build.sh finds it (VST3_SDK_DIR); otherwise the inline stubs below are used.
// VST is a trademark of Steinberg Media Technologies GmbH.
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "../dsp/Effects.h"
#include "../dsp/Instrument.h"

namespace vst3n {

struct PluginInfo {
    std::string name, vendor, path;
    std::string id;             ///< "vst3:<module path>|<class id>", stored in projects
    bool instrument = false;
};

#ifdef MINIDAW_VST3
bool available();
std::vector<std::string> defaultFolders();
std::vector<PluginInfo> scan(const std::vector<std::string>& folders);
std::shared_ptr<dsp::Effect> createEffect(const std::string& id, std::string* error = nullptr);
std::shared_ptr<dsp::Instrument> createInstrument(const std::string& id, std::string* error = nullptr);
/// Opens (or raises) the plug-in's own editor window. 'ownerWindow' is an HWND. False when it has no editor.
bool openEditor(const std::shared_ptr<dsp::ParamSet>& plugin, void* ownerWindow);
bool isVst3(const dsp::ParamSet& plugin);
/// Plug-in state as base64 text (stored in the project file).
std::string saveState(const dsp::ParamSet& plugin);
void loadState(dsp::ParamSet& plugin, const std::string& base64);
#else
inline bool available() { return false; }
inline std::vector<std::string> defaultFolders() { return {}; }
inline std::vector<PluginInfo> scan(const std::vector<std::string>&) { return {}; }
inline std::shared_ptr<dsp::Effect> createEffect(const std::string&, std::string* error = nullptr) {
    if (error) *error = "This build was made without VST3 support.";
    return nullptr;
}
inline std::shared_ptr<dsp::Instrument> createInstrument(const std::string&, std::string* error = nullptr) {
    if (error) *error = "This build was made without VST3 support.";
    return nullptr;
}
inline bool openEditor(const std::shared_ptr<dsp::ParamSet>&, void*) { return false; }
inline bool isVst3(const dsp::ParamSet&) { return false; }
inline std::string saveState(const dsp::ParamSet&) { return {}; }
inline void loadState(dsp::ParamSet&, const std::string&) {}
#endif

}  // namespace vst3n
