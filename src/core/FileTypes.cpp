// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "FileTypes.h"

#include <algorithm>
#include <cctype>

namespace core {

std::string fileExtension(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash) || dot + 1 >= path.size()) return {};
    std::string e = path.substr(dot);
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return e;
}

const std::vector<std::string>& audioExtensions() {
    static const std::vector<std::string> v = {".wav", ".wave", ".mp3", ".mp2", ".m4a", ".mp4", ".m4b", ".3gp", ".aac", ".flac", ".wma",
                                               ".ogg", ".oga", ".opus", ".aif", ".aiff", ".aifc"};
    return v;
}

const std::vector<std::string>& midiExtensions() {
    static const std::vector<std::string> v = {".mid", ".midi", ".kar"};
    return v;
}

FileKind classifyFile(const std::string& path) {
    const std::string e = fileExtension(path);
    if (e.empty()) return FileKind::Unknown;
    if (e == ".mdaw") return FileKind::Project;
    if (e == ".logicx") return FileKind::Logic;
    if (std::find(audioExtensions().begin(), audioExtensions().end(), e) != audioExtensions().end()) return FileKind::Audio;
    if (std::find(midiExtensions().begin(), midiExtensions().end(), e) != midiExtensions().end()) return FileKind::Midi;
    return FileKind::Unknown;
}

namespace {
std::string patterns(const std::vector<std::string>& exts) {
    std::string s;
    for (const auto& e : exts) { if (!s.empty()) s += ';'; s += "*" + e; }
    return s;
}
}  // namespace

std::string audioFilter() { return "Audio files|" + patterns(audioExtensions()); }
std::string midiFilter() { return "MIDI files|" + patterns(midiExtensions()); }

std::string openFilter() {
    std::vector<std::string> all = {".mdaw"};
    all.insert(all.end(), audioExtensions().begin(), audioExtensions().end());
    all.insert(all.end(), midiExtensions().begin(), midiExtensions().end());
    return "All supported files|" + patterns(all) + "|MiniDAW projects|*.mdaw|" + audioFilter() + "|" + midiFilter();
}

std::string supportedExtensionsText() {
    std::string s = "mdaw";
    for (const auto* list : {&audioExtensions(), &midiExtensions()})
        for (const auto& e : *list) s += ", " + e.substr(1);
    return s;
}

}  // namespace core
