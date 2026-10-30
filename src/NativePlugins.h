// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
#include <QStringList>
#include <memory>
#include <vector>
#include "dsp/Effects.h"

/// MiniDAW plug-ins: shared libraries that follow dsp/mdplugin.h, found in the "plugins" folders.
namespace nativeplug {

struct Info {
    QString id;      ///< "native:<plug-in id>", as saved in projects
    QString name;
    QString vendor;
    QString path;    ///< the library file
};

/// <folder of MiniDAW.exe>/plugins and <app data>/plugins.
QStringList defaultFolders();

/// Loads every plug-in library of the default folders (and 'extra'); libraries already loaded are skipped.
void rescan(const QStringList& extra = {});

/// Plug-ins found so far (scans on first use).
const std::vector<Info>& list();

/// Creates an effect from a "native:..." id, or nullptr when that plug-in is not installed.
std::shared_ptr<dsp::Effect> create(const QString& id);

}  // namespace nativeplug
