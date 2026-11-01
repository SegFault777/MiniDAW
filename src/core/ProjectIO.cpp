// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Session: importing, project files, autosave and bounce.
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <regex>
#include "../dsp/MidiFile.h"
#include "../dsp/Stretch.h"
#include "Files.h"
#include "Internal.h"
#include "Json.h"
#include "Session.h"

namespace core {

namespace {
const int kProjectVersion = 3;

std::string typeToString(TrackType t) {
    switch (t) {
    case TrackType::Audio: return "audio";
    case TrackType::Instrument: return "instrument";
    case TrackType::Drums: return "drums";
    case TrackType::Drummer: return "drummer";
    }
    return "audio";
}

TrackType typeFromString(const std::string& s) {
    return s == "instrument" ? TrackType::Instrument : s == "drums" ? TrackType::Drums : s == "drummer" ? TrackType::Drummer : TrackType::Audio;
}

std::string colorToHex(Color c) {
    char b[8];
    std::snprintf(b, sizeof b, "#%02x%02x%02x", c.r, c.g, c.b);
    return b;
}

Color colorFromHex(const std::string& s, Color def) {
    unsigned v;
    if (s.size() == 7 && s[0] == '#' && std::sscanf(s.c_str() + 1, "%6x", &v) == 1) return Color{std::uint8_t(v >> 16), std::uint8_t(v >> 8), std::uint8_t(v)};
    return def;
}

Json paramsToJson(const dsp::ParamSet& set) {
    Json o = Json::object();
    for (const auto& p : set.params) o[p->name] = double(p->get());
    return o;
}

void applyParams(dsp::ParamSet& set, const Json& o) {
    for (const auto& p : set.params)
        if (o.contains(p->name)) p->set(float(o[p->name].toDouble(p->def)));
}

/// Tempo written in a loop's file name ("Funk Groove 100 bpm.wav"), or 0.
double bpmFromName(const std::string& name) {
    static const std::regex re("(\\d{2,3}(?:\\.\\d+)?)\\s*[-_ ]?\\s*bpm", std::regex::icase);
    std::smatch m;
    return std::regex_search(name, m, re) ? std::stod(m[1].str()) : 0.0;
}

const char* const kAutoKeys[4] = {"volAuto", "panAuto", "sendAAuto", "sendBAuto"};
}  // namespace

void Session::importAudio(const std::string& path, int track, double startBeat) {
    decodeAsync(path, [this, track, startBeat](std::shared_ptr<AudioData> data, std::string err) {
        if (!data) { message(err); return; }
        int t = track;
        {
            std::lock_guard<std::mutex> l(m_mutex);
            if (t < 0 || t >= int(m_tracks.size()) || m_tracks[size_t(t)].type != TrackType::Audio) t = -1;
        }
        if (t < 0) t = addTrack(TrackType::Audio, data->name);
        else checkpoint();
        Region r;
        r.name = data->name;
        r.start = std::max(0.0, startBeat);
        r.source = r.audio = data;
        r.frames = r.srcFrames = data->frames();
        const double fileBpm = bpmFromName(data->name);   // loops that carry a tempo follow the project tempo
        if (fileBpm > 0) {
            r.sourceBpm = fileBpm;
            r.follow = true;
            const double bpm = tempoMap()->bpmAt(r.start);
            // Stretch outside the lock; this can take a moment for long files.
            const double ratio = fileBpm / bpm;
            auto d = std::make_shared<AudioData>();
            d->name = data->name;
            d->path = data->path;
            d->pcm = dsp::flex(data->pcm.data(), size_t(data->frames()), ratio, 0.0);
            computePeaks(*d);
            r.audio = d;
            r.offset = 0;
            r.frames = d->frames();
            r.flexBpm = bpm;
        }
        {
            std::lock_guard<std::mutex> l(m_mutex);
            if (t >= int(m_tracks.size())) return;
            m_tracks[size_t(t)].regions.push_back(r);
        }
        changed(false);
    });
}

// ------------------------------------------------------------------ MIDI files
bool Session::importMidiFile(const std::string& path, std::string* error) {
    std::string raw;
    if (!readFile(path, raw, error)) return false;
    dsp::MidiSong song;
    std::string err;
    if (!dsp::readMidiFile(std::vector<std::uint8_t>(raw.begin(), raw.end()), song, err)) return fail(error, err);
    pushUndo();
    bool empty = true;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        for (const Track& t : m_tracks) empty = empty && t.regions.empty();
    }
    if (empty) {   // adopt the tempo and time signature of the file into an empty project
        auto tm = std::make_shared<dsp::TempoMap>(*tempoMap());
        tm->setTempo({{0, song.bpm, false}});
        tm->setMeter({{1, song.num, song.den}});
        setTempoMap(tm, false);
    }
    const auto tm = tempoMap();
    {
        std::lock_guard<std::mutex> l(m_mutex);
        int n = 1;
        for (const dsp::MidiTrack& mt : song.tracks) {
            bool drums = true;
            double maxEnd = 0;
            for (const dsp::MidiNote& note : mt.notes) {
                drums = drums && note.channel == 9;
                maxEnd = std::max(maxEnd, note.startBeat + note.lengthBeats);
            }
            const std::string name = mt.name.empty() ? tr("Imported %1").arg(n).str() : mt.name;
            Track t = makeTrack(drums ? TrackType::Drummer : TrackType::Instrument, name);
            Region r;
            r.midi = true;
            r.name = name;
            r.start = 0;
            r.lengthBeats = std::max(tm->barLength(1), tm->nextBarLine(maxEnd));
            for (const dsp::MidiNote& note : mt.notes) r.notes.push_back({note.pitch, note.startBeat, note.lengthBeats, note.velocity});
            t.regions.push_back(std::move(r));
            m_tracks.push_back(std::move(t));
            ++n;
        }
    }
    changed(true);
    return true;
}

bool Session::exportMidiFile(const std::string& path, std::string* error) {
    const auto tm = tempoMap();
    dsp::MidiSong song;
    song.bpm = tm->bpmAt(0);
    song.num = tm->meterAtBeat(0).num;
    song.den = tm->meterAtBeat(0).den;
    for (const Track& t : snapshot()) {
        if (t.type != TrackType::Instrument && t.type != TrackType::Drummer) continue;
        dsp::MidiTrack mt;
        mt.name = t.name;
        for (const Region& r : t.regions) {
            if (!r.midi) continue;
            for (const Note& n : r.notes) {
                if (n.start >= r.lengthBeats) continue;
                mt.notes.push_back({t.type == TrackType::Drummer ? 9 : 0, n.pitch, n.velocity, r.start + n.start,
                                    std::min(n.length, r.lengthBeats - n.start)});
            }
        }
        if (!mt.notes.empty()) song.tracks.push_back(std::move(mt));
    }
    if (song.tracks.empty()) return fail(error, tr("There are no MIDI notes to export."));
    const auto bytes = dsp::writeMidiFile(song);
    return writeFile(path, bytes.data(), bytes.size(), error);
}

// ------------------------------------------------------------------ save
bool Session::saveProject(const std::string& path, std::string* error) {
    if (!writeProject(path, false, error)) return false;
    m_path = path;
    m_dirty = false;
    clearAutosave();   // the autosave is obsolete once the project is saved
    return true;
}

// ------------------------------------------------------------------ autosave and crash recovery
std::string Session::autosavePath() const { return m_autosaveDir.empty() ? std::string() : m_autosaveDir + "/autosave.mdaw"; }

bool Session::hasAutosave() const { return !m_autosaveDir.empty() && fileExists(autosavePath()); }

bool Session::autosave(std::string* error) {
    if (m_autosaveDir.empty()) return false;
    makeDirs(m_autosaveDir);
    return writeProject(autosavePath(), true, error);
}

void Session::clearAutosave() {
    if (m_autosaveDir.empty()) return;
    removeFile(autosavePath());
    removeTree(m_autosaveDir + "/audio");
    m_autoFiles.clear();
}

void Session::markRecovered() {
    m_path.clear();
    m_dirty = true;
}

/// Writes the project. An autosave never changes the project's path or dirty state; it keeps every
/// path absolute and stores new recordings once in '<autosave dir>/audio'.
bool Session::writeProject(const std::string& path, bool autosave, std::string* error) {
    const std::string dir = parentDir(path);
    const std::string audioDir = baseName(path) + "_audio";
    const std::vector<Track> tracksSnapshot = snapshot();
    int take = 1;

    Json tracks = Json::array();
    for (const Track& t : tracksSnapshot) {
        Json o = Json::object();
        o["name"] = t.name;
        o["color"] = colorToHex(t.color);
        o["type"] = typeToString(t.type);
        o["volume"] = double(t.volume);
        o["pan"] = double(t.pan);
        o["sendA"] = double(t.sendA);
        o["sendB"] = double(t.sendB);
        o["mute"] = t.mute;
        o["solo"] = t.solo;
        if (t.type == TrackType::Drums) {
            Json rows = Json::array();
            for (const auto& row : t.steps) {
                std::string s;
                for (bool b : row) s += b ? 'x' : '.';
                rows.push(s);
            }
            o["steps"] = rows;
        }
        if (t.instrument) {
            o["instrument"] = t.instrumentId;
            o["preset"] = t.preset;
            o["synth"] = paramsToJson(*t.instrument);
            if (startsWith(t.instrumentId, "vst3:") && m_hooks.saveState) o["state"] = m_hooks.saveState(*t.instrument);
        }
        Json inserts = Json::array();
        for (const auto& fx : t.inserts) {
            Json f = Json::object();
            f["id"] = std::string(fx->id());
            f["bypass"] = fx->bypass.load();
            f["params"] = paramsToJson(*fx);
            if (startsWith(fx->id(), "vst3:") && m_hooks.saveState) f["state"] = m_hooks.saveState(*fx);
            inserts.push(f);
        }
        o["inserts"] = inserts;
        for (int k = 0; k < 4; ++k) {
            const auto& pts = t.autoOf(AutoParam(k));
            if (k > 0 && pts.empty()) continue;   // older files only know volAuto
            Json autom = Json::array();
            for (const AutoPoint& p : pts) autom.push(Json(Json::Array{p.beat, double(p.value)}));
            o[kAutoKeys[k]] = autom;
        }

        Json regs = Json::array();
        for (const Region& r : t.regions) {
            Json ro = Json::object();
            ro["name"] = r.name;
            ro["start"] = r.start;
            ro["midi"] = r.midi;
            if (r.mute) ro["mute"] = true;
            if (r.midi) {
                ro["length"] = r.lengthBeats;
                Json notes = Json::array();
                for (const Note& n : r.notes) notes.push(Json(Json::Array{n.pitch, n.start, n.length, n.velocity}));
                ro["notes"] = notes;
                if (r.drummer) {
                    Json d = Json::object();
                    d["style"] = r.drum.style;
                    d["complexity"] = r.drum.complexity;
                    d["loudness"] = r.drum.loudness;
                    d["swing"] = r.drum.swing;
                    d["fills"] = r.drum.fills;
                    d["seed"] = r.drum.seed;
                    d["bars"] = r.drum.bars;
                    ro["drummer"] = d;
                }
            } else {
                const auto src = r.source ? r.source : r.audio;
                std::string p = src ? src->path : r.pendingPath;
                // Audio that only exists in the autosave folder counts as fresh when the project is really saved.
                const bool inAutosave = !autosave && src && !m_autosaveDir.empty() && startsWith(p, m_autosaveDir);
                if (autosave && src && p.empty()) {   // fresh recording: written once, reused by the next autosaves
                    const auto it = m_autoFiles.find(src.get());
                    if (it != m_autoFiles.end() && !it->second.first.expired()) {
                        p = it->second.second;
                    } else {
                        makeDirs(dir + "/audio");
                        const std::string file = dir + "/audio/rec_" + std::to_string(m_autoTake++) + ".wav";
                        std::string werr;
                        if (!writeWavFile(file, src->pcm, &werr)) return fail(error, werr);
                        m_autoFiles[src.get()] = {src, file};
                        p = file;
                    }
                }
                if (src && (p.empty() || inAutosave)) {   // a fresh recording: store it next to the project file
                    makeDirs(dir + "/" + audioDir);
                    std::string file;
                    do { file = dir + "/" + audioDir + "/take_" + std::to_string(take++) + ".wav"; } while (fileExists(file));
                    std::string werr;
                    if (!writeWavFile(file, src->pcm, &werr)) return fail(error, werr);
                    src->path = file;
                    p = file;
                }
                ro["path"] = autosave ? p : relativePath(p, dir);
                ro["srcOffset"] = double(r.srcOffset);
                ro["srcFrames"] = double(r.srcFrames);
                ro["follow"] = r.follow;
                ro["sourceBpm"] = r.sourceBpm;
                ro["semitones"] = r.semitones;
                if (r.gainDb != 0.f) ro["gainDb"] = double(r.gainDb);
                if (r.fadeIn > 0) ro["fadeIn"] = double(r.fadeIn);
                if (r.fadeOut > 0) ro["fadeOut"] = double(r.fadeOut);
            }
            regs.push(ro);
        }
        o["regions"] = regs;
        tracks.push(o);
    }

    const auto tm = tempoMap();
    Json tempo = Json::array(), meter = Json::array();
    for (const auto& p : tm->tempoPoints()) {
        Json j = Json::object();
        j["beat"] = p.beat;
        j["bpm"] = p.bpm;
        j["ramp"] = p.ramp;
        tempo.push(j);
    }
    for (const auto& m : tm->meterPoints()) {
        Json j = Json::object();
        j["bar"] = m.bar;
        j["num"] = m.num;
        j["den"] = m.den;
        meter.push(j);
    }

    Json root = Json::object();
    root["format"] = "MiniDAW";
    root["version"] = kProjectVersion;
    root["tempo"] = tempo;
    root["meter"] = meter;
    {
        Json marks = Json::array();
        for (const Marker& m : markers()) {
            Json j = Json::object();
            j["beat"] = m.beat;
            j["name"] = m.name;
            marks.push(j);
        }
        root["markers"] = marks;
    }
    root["loopOn"] = loopOn();
    root["loopA"] = loopA();
    root["loopB"] = loopB();
    root["metronome"] = metronome();
    root["countIn"] = countIn();
    root["master"] = double(m_master.load());
    root["auxA"] = paramsToJson(*m_aux[0]);
    root["auxB"] = paramsToJson(*m_aux[1]);
    root["limiter"] = paramsToJson(*m_limiter);
    root["tracks"] = tracks;

    return writeFile(path, root.dump(2), error);
}

// ------------------------------------------------------------------ load
bool Session::loadProject(const std::string& path, std::string* error) {
    std::string text;
    if (!readFile(path, text, error)) return false;
    Json root;
    if (!Json::parse(text, root) || !root.isObject() || root["format"].toString() != "MiniDAW")
        return fail(error, tr("Not a valid MiniDAW project file."));
    const std::string dir = parentDir(path);

    stopRecording();
    m_playing = false;
    m_countRemain = 0;

    auto tm = std::make_shared<dsp::TempoMap>();
    std::vector<dsp::TempoPoint> tempo;
    for (const Json& o : root["tempo"].items()) tempo.push_back({o["beat"].toDouble(), o["bpm"].toDouble(120), o["ramp"].toBool()});
    if (tempo.empty()) tempo.push_back({0, root["bpm"].toDouble(120), false});   // projects from v0.2
    std::vector<dsp::MeterPoint> meter;
    for (const Json& o : root["meter"].items()) meter.push_back({o["bar"].toInt(1), o["num"].toInt(4), o["den"].toInt(4)});
    tm->setTempo(tempo);
    tm->setMeter(meter);
    { std::lock_guard<std::mutex> l(m_tempoMutex); m_tempo = tm; }

    std::vector<std::string> toDecode;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        m_tracks.clear();
        m_undo.clear();
        m_redo.clear();
        m_nextId = 1;
        m_audition.reset();
        m_markers.clear();
        for (const Json& mo : root["markers"].items()) m_markers.push_back({mo["beat"].toDouble(), mo["name"].toString()});
        std::sort(m_markers.begin(), m_markers.end(), [](const Marker& a, const Marker& b) { return a.beat < b.beat; });
        for (const Json& o : root["tracks"].items()) {
            Track t = makeTrack(typeFromString(o["type"].toString()), o["name"].toString());
            t.color = colorFromHex(o["color"].toString(), t.color);
            t.volume = float(o["volume"].toDouble(0.8));
            t.pan = float(o["pan"].toDouble(0));
            t.sendA = float(o["sendA"].toDouble(0));
            t.sendB = float(o["sendB"].toDouble(0));
            t.mute = o["mute"].toBool();
            t.solo = o["solo"].toBool();
            if (t.type == TrackType::Drums) {
                const auto& rows = o["steps"].items();
                for (size_t r = 0; r < size_t(kDrumRows) && r < rows.size(); ++r) {
                    const std::string s = rows[r].toString();
                    for (size_t k = 0; k < size_t(kSteps) && k < s.size(); ++k) t.steps[r][k] = (s[k] == 'x');
                }
            }
            if (t.instrument) {
                const std::string id = o["instrument"].toString(t.instrumentId);
                if (id != t.instrumentId) {
                    if (auto inst = makeInstrument(id)) { t.instrument = inst; t.instrumentId = id; }
                    else message(tr("Instrument plug-in not available: %1").arg(id));
                }
                t.preset = o["preset"].toInt();
                applyParams(*t.instrument, o["synth"]);
                if (startsWith(t.instrumentId, "vst3:") && o.contains("state") && m_hooks.loadState)
                    m_hooks.loadState(*t.instrument, o["state"].toString());
            }
            for (const Json& io : o["inserts"].items()) {
                auto fx = makeEffect(io["id"].toString());
                if (!fx) { message(tr("Effect plug-in not available: %1").arg(io["id"].toString())); continue; }
                fx->bypass = io["bypass"].toBool();
                applyParams(*fx, io["params"]);
                if (io.contains("state") && m_hooks.loadState) m_hooks.loadState(*fx, io["state"].toString());
                t.inserts.push_back(fx);
            }
            for (int k = 0; k < 4; ++k)
                for (const Json& a : o[kAutoKeys[k]].items())
                    if (a.size() == 2) t.autoOf(AutoParam(k)).push_back({a.items()[0].toDouble(), float(a.items()[1].toDouble())});
            for (const Json& ro : o["regions"].items()) {
                Region r;
                r.name = ro["name"].toString();
                r.start = ro["start"].toDouble();
                r.midi = ro["midi"].toBool();
                r.mute = ro["mute"].toBool();
                if (r.midi) {
                    r.lengthBeats = ro["length"].toDouble(4);
                    for (const Json& nv : ro["notes"].items()) {
                        if (nv.size() != 4) continue;
                        const auto& a = nv.items();
                        r.notes.push_back(Note{a[0].toInt(), a[1].toDouble(), a[2].toDouble(), a[3].toInt()});
                    }
                    if (ro.contains("drummer")) {
                        const Json& d = ro["drummer"];
                        r.drummer = true;
                        r.drum.style = d["style"].toInt();
                        r.drum.complexity = d["complexity"].toDouble(0.5);
                        r.drum.loudness = d["loudness"].toDouble(0.6);
                        r.drum.swing = d["swing"].toDouble();
                        r.drum.fills = d["fills"].toBool(true);
                        r.drum.seed = d["seed"].toInt(1);
                        r.drum.bars = d["bars"].toInt(4);
                    }
                } else {
                    r.pendingPath = absolutePath(toUtf8(toPath(dir) / toPath(ro["path"].toString())));
                    r.srcOffset = i64(ro["srcOffset"].toDouble(ro["offset"].toDouble()));   // "offset" in v0.2 projects
                    r.srcFrames = i64(ro["srcFrames"].toDouble(ro["frames"].toDouble()));
                    r.follow = ro["follow"].toBool();
                    r.sourceBpm = ro["sourceBpm"].toDouble();
                    r.semitones = ro["semitones"].toDouble();
                    r.gainDb = float(ro["gainDb"].toDouble(0));
                    r.fadeIn = i64(ro["fadeIn"].toDouble(0));
                    r.fadeOut = i64(ro["fadeOut"].toDouble(0));
                    if (std::find(toDecode.begin(), toDecode.end(), r.pendingPath) == toDecode.end()) toDecode.push_back(r.pendingPath);
                }
                t.regions.push_back(r);
            }
            m_tracks.push_back(std::move(t));
        }
        m_pos = 0;
    }
    m_loopOn = root["loopOn"].toBool();
    m_loopA = root["loopA"].toDouble(0);
    m_loopB = root["loopB"].toDouble(16);
    m_metro = root["metronome"].toBool();
    m_countIn = root["countIn"].toBool();
    m_master = float(root["master"].toDouble(0.9));
    applyParams(*m_aux[0], root["auxA"]);
    applyParams(*m_aux[1], root["auxB"]);
    applyParams(*m_limiter, root["limiter"]);
    m_path = path;
    m_flush |= 2;
    m_dirty = false;
    if (m_hooks.changed) m_hooks.changed(true);
    if (m_hooks.tempoChanged) m_hooks.tempoChanged();
    if (m_hooks.loopChanged) m_hooks.loopChanged();

    // Decode the referenced audio files and attach them to their regions.
    for (const std::string& p : toDecode) {
        decodeAsync(p, [this, p](std::shared_ptr<AudioData> data, std::string) {
            if (!data) { message(tr("Missing audio file: %1").arg(p)); return; }
            const auto tmap = tempoMap();
            struct Job { int trackId; size_t ri; Region copy; };
            std::vector<Job> jobs;
            {
                std::lock_guard<std::mutex> l(m_mutex);
                for (Track& t : m_tracks)
                    for (size_t i = 0; i < t.regions.size(); ++i) {
                        Region& r = t.regions[i];
                        if (r.midi || r.source || r.pendingPath != p) continue;
                        r.source = r.audio = data;
                        if (r.srcFrames <= 0 || r.srcOffset + r.srcFrames > data->frames()) r.srcFrames = data->frames() - r.srcOffset;
                        r.offset = r.srcOffset;
                        r.frames = r.srcFrames;
                        r.pendingPath.clear();
                        if (r.flexed()) jobs.push_back({t.id, i, r});
                    }
            }
            for (Job& j : jobs) {   // stretch regions that follow the tempo, outside the lock
                const double bpm = tmap->bpmAt(j.copy.start);
                const double ratio = (j.copy.follow && j.copy.sourceBpm > 0) ? j.copy.sourceBpm / bpm : 1.0;
                auto d = std::make_shared<AudioData>();
                d->name = data->name;
                d->path = data->path;
                d->pcm = dsp::flex(data->pcm.data() + j.copy.srcOffset * 2, size_t(j.copy.srcFrames), ratio, j.copy.semitones);
                computePeaks(*d);
                j.copy.audio = d;
                j.copy.offset = 0;
                j.copy.frames = d->frames();
                j.copy.flexBpm = bpm;
            }
            {
                std::lock_guard<std::mutex> l(m_mutex);
                for (const Job& j : jobs)
                    for (Track& t : m_tracks)
                        if (t.id == j.trackId && j.ri < t.regions.size() && t.regions[j.ri].source == data) {
                            Region& dst = t.regions[j.ri];
                            dst.audio = j.copy.audio; dst.offset = j.copy.offset; dst.frames = j.copy.frames; dst.flexBpm = j.copy.flexBpm;
                        }
            }
            if (m_hooks.changed) m_hooks.changed(false);
        });
    }
    return true;
}

// ------------------------------------------------------------------ bounce
bool Session::exportWav(const std::string& path, std::string* error) {
    return exportAudio(path, ExportOptions{}, error);
}

bool Session::exportAudio(const std::string& path, const ExportOptions& opt, std::string* error) {
    const auto tempo = tempoMap();
    std::lock_guard<std::mutex> lk(m_mutex);
    m_tm = tempo.get();
    double startBeat = 0, endBeat = 0;
    if (opt.cycleOnly) {
        startBeat = m_loopA.load();
        endBeat = m_loopB.load();
        if (!(endBeat > startBeat)) return fail(error, tr("Set a cycle range first (right-drag on the ruler)."));
    } else {
        bool anyDrums = false;
        for (const Track& t : m_tracks) {
            for (const Region& r : t.regions) endBeat = std::max(endBeat, r.start + regionBeats(r));
            if (t.type == TrackType::Drums)
                for (const auto& row : t.steps)
                    for (bool b : row) anyDrums |= b;
        }
        if (anyDrums) endBeat = std::max(endBeat, tempo->barStart(5));   // at least 4 bars of drums
    }
    if (endBeat <= startBeat) return fail(error, tr("There is nothing to bounce."));
    const i64 first = i64(std::llround(tempo->beatToFrame(startBeat)));
    const i64 end = i64(std::llround(tempo->beatToFrame(endBeat))) + (opt.tail ? i64(kSampleRate) * 2 : 0);   // tail for reverb / echo
    const i64 total = end - first;

    std::ofstream out(toPath(path), std::ios::binary | std::ios::trunc);   // streamed: long projects need no memory
    if (!out) return fail(error, "cannot write " + path);
    constexpr int kBlock = 4096;
    ensureScratch(size_t(kBlock) * 2);
    // Renders [first, end) through the whole signal path and hands every block to 'sink'.
    auto pass = [&](auto&& sink) {
        flushDsp();
        m_expected = -1;
        for (i64 p = first; p < end; p += kBlock) {
            const int n = int(std::min<i64>(kBlock, end - p));
            renderBlock(p, n, m_scratch.data(), true);
            masterProcess(m_scratch.data(), n);
            if (!sink(m_scratch.data(), n)) break;
        }
        flushDsp();
        m_expected = -1;
    };

    float gain = 1.f;
    if (opt.normalize) {   // first pass: find the peak of the finished mix
        float peak = 0;
        pass([&](const float* b, int n) { peak = std::max(peak, dsp::peakOf(b, size_t(n))); return true; });
        gain = dsp::normalizeGain(peak, opt.normalizeDb);
    }

    const auto header = dsp::wavHeader(opt.format, kSampleRate, std::uint64_t(total));
    out.write(reinterpret_cast<const char*>(header.data()), std::streamsize(header.size()));
    std::vector<std::uint8_t> bytes;
    std::uint32_t seed = 12345u;
    pass([&](const float* b, int n) {
        bytes.clear();
        dsp::encodeSamples(b, size_t(n), opt.format, gain, opt.dither, seed, bytes);
        out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        return bool(out);
    });
    out.flush();
    if (!out) return fail(error, "cannot write " + path);
    return true;
}

}  // namespace core
