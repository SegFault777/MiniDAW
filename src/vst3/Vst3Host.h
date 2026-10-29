// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// VST3 plug-in hosting. The real implementation (Vst3Host.cpp) needs the Steinberg VST3 SDK and is only
// compiled when build.sh finds it (VST3_SDK_DIR). Otherwise the inline stubs below are used and the rest
// of the application does not notice any difference.
//
// VST is a trademark of Steinberg Media Technologies GmbH.
#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <memory>
#include <vector>
#include "dsp/Effects.h"
#include "dsp/Instrument.h"

namespace vst3 {

struct PluginInfo {
    QString name, vendor, path;
    QString id;                 ///< "vst3:<module path>|<class id>", stored in projects
    bool instrument = false;
};

#ifdef MINIDAW_VST3
bool available();
QStringList defaultFolders();
std::vector<PluginInfo> scan(const QStringList& folders);
std::shared_ptr<dsp::Effect> createEffect(const QString& id, QString* error = nullptr);
std::shared_ptr<dsp::Instrument> createInstrument(const QString& id, QString* error = nullptr);
/// Creates the plug-in's own editor window, or returns nullptr when 'plugin' is not a VST3 plug-in.
QWidget* createEditor(const std::shared_ptr<dsp::ParamSet>& plugin, QWidget* parent);
QByteArray saveState(const dsp::ParamSet& plugin);
void loadState(dsp::ParamSet& plugin, const QByteArray& state);
#else
inline bool available() { return false; }
inline QStringList defaultFolders() { return {}; }
inline std::vector<PluginInfo> scan(const QStringList&) { return {}; }
inline std::shared_ptr<dsp::Effect> createEffect(const QString&, QString* error = nullptr) {
    if (error) *error = QStringLiteral("This build was made without VST3 support.");
    return nullptr;
}
inline std::shared_ptr<dsp::Instrument> createInstrument(const QString&, QString* error = nullptr) {
    if (error) *error = QStringLiteral("This build was made without VST3 support.");
    return nullptr;
}
inline QWidget* createEditor(const std::shared_ptr<dsp::ParamSet>&, QWidget*) { return nullptr; }
inline QByteArray saveState(const dsp::ParamSet&) { return {}; }
inline void loadState(dsp::ParamSet&, const QByteArray&) {}
#endif

}  // namespace vst3
