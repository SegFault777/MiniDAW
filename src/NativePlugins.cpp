// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativePlugins.h"

#include <QCoreApplication>
#include <QDir>
#include <QLibrary>
#include <QSet>
#include <algorithm>
#include <QStandardPaths>
#include "dsp/NativeEffect.h"

namespace nativeplug {
namespace {
struct Entry {
    Info info;
    const MdEffectDesc* desc;
};
std::vector<Entry>& entries() { static std::vector<Entry> e; return e; }
std::vector<Info>& infos() { static std::vector<Info> v; return v; }
std::vector<std::unique_ptr<QLibrary>>& libraries() { static std::vector<std::unique_ptr<QLibrary>> l; return l; }   // never unloaded: descriptors must stay valid
QSet<QString>& loadedFiles() { static QSet<QString> s; return s; }
bool g_scanned = false;
}  // namespace

QStringList defaultFolders() {
    return {QCoreApplication::applicationDirPath() + "/plugins",
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/plugins"};
}

void rescan(const QStringList& extra) {
    g_scanned = true;
    for (const QString& folder : defaultFolders() + extra) {
        const QDir d(folder);
        for (const QString& file : d.entryList({"*.dll", "*.so", "*.dylib"}, QDir::Files, QDir::Name)) {
            const QString path = d.absoluteFilePath(file);
            if (loadedFiles().contains(path)) continue;
            loadedFiles().insert(path);
            auto lib = std::make_unique<QLibrary>(path);
            if (!lib->load()) continue;
            using CountFn = int (*)();
            using GetFn = const MdEffectDesc* (*)(int);
            const auto count = reinterpret_cast<CountFn>(lib->resolve("mdplugin_count"));
            const auto get = reinterpret_cast<GetFn>(lib->resolve("mdplugin_get"));
            if (!count || !get) {
                lib->unload();
                continue;
            }
            bool used = false;
            for (int i = 0, n = std::min(count(), 64); i < n; ++i) {
                const MdEffectDesc* desc = get(i);
                if (!dsp::validDescriptor(desc)) continue;
                const QString id = QString("native:") + QString::fromUtf8(desc->id);
                bool dup = false;
                for (const Entry& e : entries()) dup |= e.info.id == id;
                if (dup) continue;
                Info info{id, QString::fromUtf8(desc->name), desc->vendor ? QString::fromUtf8(desc->vendor) : QString(), path};
                entries().push_back({info, desc});
                infos().push_back(info);
                used = true;
            }
            if (used) libraries().push_back(std::move(lib));
            else lib->unload();
        }
    }
}

const std::vector<Info>& list() {
    if (!g_scanned) rescan();
    return infos();
}

std::shared_ptr<dsp::Effect> create(const QString& id) {
    if (!g_scanned) rescan();
    for (const Entry& e : entries())
        if (e.info.id == id) return dsp::NativeEffect::create(e.desc);
    return nullptr;
}

}  // namespace nativeplug
