// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PluginHost.h"

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <set>
#include "../dsp/NativeEffect.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace platform {
namespace {

struct Entry {
    NativePluginInfo info;
    const MdEffectDesc* desc;
};

std::mutex gMutex;
std::vector<Entry> gEntries;
std::set<std::string> gLoaded;   // library files that were tried already (libraries stay loaded: descriptors must stay valid)

void* openLibrary(const std::string& path) {
#ifdef _WIN32
    const int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring w(size_t(std::max(n, 1)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, w.data(), n);
    return reinterpret_cast<void*>(LoadLibraryW(w.c_str()));
#else
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void* symbol(void* lib, const char* name) {
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(lib), name));
#else
    return dlsym(lib, name);
#endif
}

void closeLibrary(void* lib) {
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(lib));
#else
    dlclose(lib);
#endif
}

bool isLibrary(const std::filesystem::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return ext == ".dll" || ext == ".so" || ext == ".dylib";
}

}  // namespace

void scanNativePlugins(const std::vector<std::string>& folders) {
    namespace fs = std::filesystem;
    std::lock_guard<std::mutex> lock(gMutex);
    for (const std::string& folder : folders) {
        std::error_code ec;
        std::vector<std::string> files;
        for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
            if (it->is_regular_file(ec) && isLibrary(it->path())) files.push_back(it->path().string());
        std::sort(files.begin(), files.end());
        for (const std::string& path : files) {
            if (!gLoaded.insert(path).second) continue;
            void* lib = openLibrary(path);
            if (!lib) continue;
            using CountFn = int (*)();
            using GetFn = const MdEffectDesc* (*)(int);
            const auto count = reinterpret_cast<CountFn>(symbol(lib, "mdplugin_count"));
            const auto get = reinterpret_cast<GetFn>(symbol(lib, "mdplugin_get"));
            if (!count || !get) { closeLibrary(lib); continue; }
            bool used = false;
            for (int i = 0, n = std::min(count(), 64); i < n; ++i) {
                const MdEffectDesc* desc = get(i);
                if (!dsp::validDescriptor(desc)) continue;
                const std::string id = std::string("native:") + desc->id;
                const bool dup = std::any_of(gEntries.begin(), gEntries.end(), [&](const Entry& e) { return e.info.id == id; });
                if (dup) continue;
                gEntries.push_back({{id, desc->name, desc->vendor ? desc->vendor : "", path}, desc});
                used = true;
            }
            if (!used) closeLibrary(lib);
        }
    }
}

std::vector<NativePluginInfo> nativePlugins() {
    std::lock_guard<std::mutex> lock(gMutex);
    std::vector<NativePluginInfo> v;
    for (const Entry& e : gEntries) v.push_back(e.info);
    return v;
}

std::shared_ptr<dsp::Effect> createNativePlugin(const std::string& id) {
    std::lock_guard<std::mutex> lock(gMutex);
    for (const Entry& e : gEntries)
        if (e.info.id == id) return dsp::NativeEffect::create(e.desc);
    return nullptr;
}

}  // namespace platform
