// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "LogicImport.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <system_error>

#include "FileTypes.h"
#include "Bplist.h"
#include "Files.h"
#include "LogicArrangement.h"
#include "LogicProjectData.h"
#include "Session.h"

namespace core {

namespace {

std::string lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

// all regular files below 'dir' (empty when it does not exist)
std::vector<std::string> filesBelow(const std::string& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    if (!fs::is_directory(toPath(dir), ec)) return out;
    for (fs::recursive_directory_iterator it(toPath(dir), fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_regular_file(e2)) out.push_back(toUtf8(it->path()));
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool readable(const std::string& p) {
    const auto& ex = audioExtensions();
    return std::find(ex.begin(), ex.end(), fileExtension(p)) != ex.end();
}

bool audioLike(const std::string& p) {
    const std::string e = fileExtension(p);
    return readable(p) || e == ".caf";
}

}  // namespace

bool isLogicPath(const std::string& path) { return fileExtension(path) == ".logicx"; }

LogicPackage scanLogicPackage(const std::string& folder) {
    LogicPackage pk;
    std::error_code ec;
    if (!fs::is_directory(toPath(folder), ec)) return pk;
    std::string trimmed = folder;
    while (trimmed.size() > 1 && (trimmed.back() == '/' || trimmed.back() == '\\')) trimmed.pop_back();
    pk.name = baseName(trimmed);
    // a package has Alternatives/ or Media/ (or ProjectData somewhere); the extension alone is enough for a folder named .logicx
    const bool named = isLogicPath(trimmed);
    bool alt = false;
    for (const auto& f : filesBelow(trimmed + "/Alternatives")) if (baseName(f) == "ProjectData") { alt = true; break; }
    pk.hasProjectData = alt;
    const bool media = fs::is_directory(toPath(trimmed + "/Media"), ec);
    pk.found = named || alt || media;
    if (!pk.found) return pk;
    for (const auto& f : filesBelow(trimmed + "/Media")) {
        if (!audioLike(f)) continue;
        const std::string rel = lower(relativePath(f, trimmed));
        const bool bounce = rel.find("bounces/") != std::string::npos;
        if (!readable(f)) { ++pk.skippedUnreadable; continue; }
        (bounce ? pk.bounces : pk.audioFiles).push_back(f);
    }
    for (const auto& f : filesBelow(trimmed + "/Bounces")) if (readable(f)) pk.bounces.push_back(f);
    return pk;
}

void readLogicMetadata(const std::string& folder, LogicProject& proj) {
    auto load = [&](const std::string& path, PlistValue& v) {
        std::string raw;
        return !path.empty() && readFile(path, raw) && parseBplist(reinterpret_cast<const std::uint8_t*>(raw.data()), raw.size(), v);
    };
    PlistValue info;
    if (load(folder + "/Resources/ProjectInformation.plist", info)) {
        if (const auto* v = info.find("LastSavedFrom")) if (proj.creator.empty()) proj.creator = v->text();
    }
    const std::string pd = findProjectDataFile(folder);
    if (pd.empty()) return;
    PlistValue meta;
    if (!load(pd.substr(0, pd.size() - std::string("ProjectData").size()) + "MetaData.plist", meta)) return;
    if (const auto* v = meta.find("BeatsPerMinute")) {
        const double bpm = v->number();
        if (bpm >= 20 && bpm <= 999 && proj.tempo.empty()) proj.tempo = {{0.0, bpm, false}};
    }
    if (const auto* v = meta.find("SampleRate")) if (proj.sampleRate == 0) proj.sampleRate = int(v->number());
    if (const auto* v = meta.find("NumberOfTracks")) if (proj.trackCountHint == 0) proj.trackCountHint = int(v->number());
}

LogicApplyStats applyLogicProject(Session& session, const LogicProject& project, const LogicPackage& pk) {
    LogicApplyStats st;
    // files by their full name (a .wav and an .aif may share a stem)
    auto findFile = [&](const std::string& name) -> std::string {
        std::string want = lower(name);
        const std::size_t slash = want.find_last_of("/\\");
        if (slash != std::string::npos) want = want.substr(slash + 1);
        for (const auto* list : {&pk.audioFiles, &pk.bounces})
            for (const auto& f : *list) {
                std::string have = lower(f);
                const std::size_t s2 = have.find_last_of("/\\");
                if (s2 != std::string::npos) have = have.substr(s2 + 1);
                if (have == want) return f;
            }
        return {};
    };
    if (!project.tempo.empty()) { session.setTempoPoints(project.tempo, true); st.tempo = true; }
    if (!project.meters.empty()) session.setMeterPoints(project.meters);
    for (const auto& m : project.markers) session.addMarker(m.first, m.second);
    const double rateScale = project.sampleRate > 0 ? double(kSampleRate) / project.sampleRate : 1.0;
    std::map<std::string, std::vector<Session::AudioPlacement>> byFile;   // decode every file once
    for (const LogicTrack& lt : project.tracks) {
        TrackType type;
        switch (lt.kind) {
        case LogicTrackKind::Audio: type = TrackType::Audio; break;
        case LogicTrackKind::Software: type = TrackType::Instrument; break;
        case LogicTrackKind::Drummer: type = TrackType::Drummer; break;
        default: continue;
        }
        const int t = session.addTrack(type, lt.name);
        if (t < 0) continue;
        ++st.tracks;
        if (type == TrackType::Audio) {
            for (const auto& ar : lt.audio) {
                const std::string f = findFile(ar.file);
                if (f.empty()) { ++st.missingAudio; continue; }
                Session::AudioPlacement p;
                p.track = t;
                p.startBeat = ar.startBeat;
                p.srcOffset = static_cast<long long>(std::llround(double(ar.trimInSamples) * rateScale));
                p.frames = static_cast<long long>(std::llround(double(ar.lengthSamples) * rateScale));
                byFile[f].push_back(p);
                ++st.audioRegions;
            }
        } else {
            for (const auto& mr : lt.midi) {
                const int r = session.addMidiRegion(t, mr.startBeat, mr.lengthBeats);
                if (r < 0) continue;
                session.setRegionNotes(t, r, mr.notes);
                ++st.midiRegions;
            }
        }
    }
    for (const auto& kv : byFile) session.importAudioPlacements(kv.first, kv.second);
    return st;
}

int importLogicPackage(Session& session, const std::string& folder, std::string* summary, int maxFiles) {
    auto say = [&](const std::string& s) { if (summary) *summary = s; };
    const LogicPackage pk = scanLogicPackage(folder);
    if (!pk.found) {
        say("This is not a Logic Pro project folder. A .logicx is a folder: on Windows copy it whole (or unzip it) and open that folder.");
        return 0;
    }
    // stage 2: ProjectData -> project model (only when a decoder understands the file)
    std::string parseNote = "ProjectData was not found in the package.";
    const std::string pdFile = findProjectDataFile(folder);
    if (!pdFile.empty()) {
        std::string raw;
        if (readFile(pdFile, raw)) {
            const ProjectDataResult pr = parseProjectData(reinterpret_cast<const std::uint8_t*>(raw.data()), raw.size());
            parseNote = pr.note;
            LogicProject proj = pr.project;
            readLogicMetadata(folder, proj);
            if (proj.name.empty()) proj.name = pk.name;
            {   // track names from the arrange list (rows are Logic's track numbers)
                const auto names = readLogicTrackNames(reinterpret_cast<const std::uint8_t*>(raw.data()), raw.size(), proj.trackCountHint);
                for (LogicTrack& t : proj.tracks)
                    if (t.number >= 1 && std::size_t(t.number) <= names.size() && !names[std::size_t(t.number) - 1].empty()) t.name = names[std::size_t(t.number) - 1];
            }
            if (pr.decoded && !proj.tracks.empty()) {
                const LogicApplyStats st = applyLogicProject(session, proj, pk);
                std::string s = "Logic Pro project \"" + pk.name + "\": " + std::to_string(st.tracks) + " track(s), " + std::to_string(st.audioRegions) +
                                " audio and " + std::to_string(st.midiRegions) + " MIDI region(s)" + (st.tempo ? ", tempo map" : "") + ".";
                if (st.missingAudio > 0) s += " " + std::to_string(st.missingAudio) + " audio file(s) were not found in the package.";
                say(s);
                return st.audioRegions + st.midiRegions;
            }
        }
    }
    // tempo from the plist even when the arrangement cannot be read
    std::string tempoText;
    {
        LogicProject meta;
        readLogicMetadata(folder, meta);
        if (!meta.tempo.empty()) {
            LogicProject t; t.tempo = meta.tempo;
            applyLogicProject(session, t, pk);
            char b[48]; std::snprintf(b, sizeof b, "%g", meta.tempo[0].bpm);
            tempoText = std::string("Tempo set to ") + b + " BPM from the project. ";
        }
    }
    const bool useBounces = pk.audioFiles.empty();
    const std::vector<std::string>& src = useBounces ? pk.bounces : pk.audioFiles;
    if (src.empty()) {
        say("The Logic Pro project \"" + pk.name + "\" has no audio files MiniDAW can read" +
            (pk.skippedUnreadable > 0 ? " (" + std::to_string(pk.skippedUnreadable) + " in formats such as .caf)." : "."));
        return 0;
    }
    const int n = std::min<int>(int(src.size()), std::max(1, maxFiles));
    for (int i = 0; i < n; ++i) session.importAudio(src[size_t(i)], -1, 0.0);
    std::string s = "Logic Pro project \"" + pk.name + "\": " + tempoText + "importing " + std::to_string(n) + (useBounces ? " bounce" : " audio file") + (n == 1 ? "" : "s") +
                    ", one track each, from bar 1. ";
    if (int(src.size()) > n) s += std::to_string(int(src.size()) - n) + " more were left out. ";
    if (pk.skippedUnreadable > 0) s += std::to_string(pk.skippedUnreadable) + " file(s) in unsupported formats were skipped. ";
    s += "The arrangement (regions, plug-ins, automation) is stored in Logic's own format and is not read (" + parseNote + ")";
    say(s);
    return n;
}

}  // namespace core
