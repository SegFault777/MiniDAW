// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>
#include <cmath>
#include "AudioUtil.h"
#include "Engine.h"
#include "dsp/MidiFile.h"
#include "dsp/Stretch.h"
#include "vst3/Vst3Host.h"

namespace {
const int kProjectVersion = 3;

QString typeToString(TrackType t) {
    switch (t) {
    case TrackType::Audio: return "audio";
    case TrackType::Instrument: return "instrument";
    case TrackType::Drums: return "drums";
    case TrackType::Drummer: return "drummer";
    }
    return "audio";
}

TrackType typeFromString(const QString& s) {
    return s == "instrument" ? TrackType::Instrument : s == "drums" ? TrackType::Drums : s == "drummer" ? TrackType::Drummer : TrackType::Audio;
}

QJsonObject paramsToJson(const dsp::ParamSet& set) {
    QJsonObject o;
    for (const auto& p : set.params) o[QString::fromStdString(p->name)] = double(p->get());
    return o;
}

void applyParams(dsp::ParamSet& set, const QJsonObject& o) {
    for (const auto& p : set.params) {
        const auto it = o.find(QString::fromStdString(p->name));
        if (it != o.end()) p->set(float(it->toDouble(p->def)));
    }
}

/// Tempo written in a loop's file name ("Funk Groove 100 bpm.wav"), or 0.
double bpmFromName(const QString& name) {
    static const QRegularExpression re("(\\d{2,3}(?:\\.\\d+)?)\\s*[-_ ]?\\s*bpm", QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(name);
    return m.hasMatch() ? m.captured(1).toDouble() : 0.0;
}

std::shared_ptr<dsp::Effect> makeEffect(const QString& id) {
    return id.startsWith("vst3:") ? vst3::createEffect(id) : dsp::createEffect(id.toStdString());
}

std::shared_ptr<dsp::Instrument> makeInstrument(const QString& id) {
    if (id == "synth") return std::make_shared<dsp::Synth>();
    if (id == "drumkit") return std::make_shared<dsp::DrumKit>();
    if (id.startsWith("vst3:")) return vst3::createInstrument(id);
    return nullptr;
}
}  // namespace

// Decodes a file asynchronously to 44.1 kHz stereo float. 'done' runs exactly once on the UI thread.
void Engine::decodeAsync(const QString& path, std::function<void(std::shared_ptr<AudioData>, QString)> done) {
    auto* dec = new QAudioDecoder(this);
    auto data = std::make_shared<AudioData>();
    data->name = QFileInfo(path).completeBaseName();
    data->path = path;
    auto rs = std::make_shared<Resampler>();
    auto fired = std::make_shared<bool>(false);

    QAudioFormat fmt;
    fmt.setSampleRate(kSampleRate);
    fmt.setChannelCount(2);
    fmt.setSampleFormat(QAudioFormat::Int16);
    dec->setAudioFormat(fmt);
    dec->setSource(QUrl::fromLocalFile(path));

    connect(dec, &QAudioDecoder::bufferReady, this, [dec, data, rs] {
        while (dec->bufferAvailable()) {
            const QAudioBuffer b = dec->read();
            if (!b.isValid() || b.frameCount() <= 0) continue;
            std::vector<float> st;
            if (toStereoFloat(b.constData<char>(), b.frameCount(), b.format(), st))
                rs->process(st, b.format().sampleRate(), data->pcm);
        }
    });
    connect(dec, &QAudioDecoder::finished, this, [=, this] {
        dec->deleteLater();
        if (*fired) return;
        *fired = true;
        if (data->frames() == 0) { done(nullptr, tr("Could not decode %1").arg(data->name)); return; }
        computePeaks(*data);
        done(data, {});
    });
    connect(dec, QOverload<QAudioDecoder::Error>::of(&QAudioDecoder::error), this, [=, this](QAudioDecoder::Error) {
        dec->deleteLater();
        if (*fired) return;
        *fired = true;
        done(nullptr, tr("Import failed: %1").arg(dec->errorString()));
    });
    dec->start();
}

void Engine::importAudio(const QString& path, int track, double startBeat) {
    decodeAsync(path, [this, track, startBeat](std::shared_ptr<AudioData> data, QString err) {
        if (!data) { emit message(err); return; }
        int t = track;
        {
            QMutexLocker l(&m_mutex);
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
            QMutexLocker l(&m_mutex);
            m_tracks[size_t(t)].regions.push_back(r);
        }
        changed(false);
    });
}

// ------------------------------------------------------------------ MIDI files
bool Engine::importMidiFile(const QString& path, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    const QByteArray raw = f.readAll();
    dsp::MidiSong song;
    std::string err;
    if (!dsp::readMidiFile(std::vector<std::uint8_t>(raw.begin(), raw.end()), song, err)) {
        if (error) *error = QString::fromStdString(err);
        return false;
    }
    pushUndo();
    bool empty = true;
    {
        QMutexLocker l(&m_mutex);
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
        QMutexLocker l(&m_mutex);
        int n = 1;
        for (const dsp::MidiTrack& mt : song.tracks) {
            bool drums = true;
            double maxEnd = 0;
            for (const dsp::MidiNote& note : mt.notes) {
                drums = drums && note.channel == 9;
                maxEnd = std::max(maxEnd, note.startBeat + note.lengthBeats);
            }
            const QString name = mt.name.empty() ? tr("Imported %1").arg(n) : QString::fromStdString(mt.name);
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

bool Engine::exportMidiFile(const QString& path, QString* error) {
    const auto tm = tempoMap();
    dsp::MidiSong song;
    song.bpm = tm->bpmAt(0);
    song.num = tm->meterAtBeat(0).num;
    song.den = tm->meterAtBeat(0).den;
    for (const Track& t : snapshot()) {
        if (t.type != TrackType::Instrument && t.type != TrackType::Drummer) continue;
        dsp::MidiTrack mt;
        mt.name = t.name.toStdString();
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
    if (song.tracks.empty()) {
        if (error) *error = tr("There are no MIDI notes to export.");
        return false;
    }
    const auto bytes = dsp::writeMidiFile(song);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(reinterpret_cast<const char*>(bytes.data()), qint64(bytes.size()));
    return true;
}

// ------------------------------------------------------------------ save
bool Engine::saveProject(const QString& path, QString* error) {
    const QFileInfo fi(path);
    const QDir dir = fi.absoluteDir();
    const QString audioDir = fi.completeBaseName() + "_audio";
    const std::vector<Track> tr = snapshot();
    int take = 1;

    QJsonArray tracks;
    for (const Track& t : tr) {
        QJsonObject o;
        o["name"] = t.name;
        o["color"] = t.color.name();
        o["type"] = typeToString(t.type);
        o["volume"] = double(t.volume);
        o["pan"] = double(t.pan);
        o["sendA"] = double(t.sendA);
        o["sendB"] = double(t.sendB);
        o["mute"] = t.mute;
        o["solo"] = t.solo;
        if (t.type == TrackType::Drums) {
            QJsonArray rows;
            for (const auto& row : t.steps) {
                QString s;
                for (bool b : row) s += b ? 'x' : '.';
                rows.append(s);
            }
            o["steps"] = rows;
        }
        if (t.instrument) {
            o["instrument"] = t.instrumentId;
            o["preset"] = t.preset;
            o["synth"] = paramsToJson(*t.instrument);
            if (t.instrumentId.startsWith("vst3:")) o["state"] = QString::fromLatin1(vst3::saveState(*t.instrument).toBase64());
        }
        QJsonArray inserts;
        for (const auto& fx : t.inserts) {
            QJsonObject f;
            f["id"] = fx->id();
            f["bypass"] = fx->bypass.load();
            f["params"] = paramsToJson(*fx);
            if (QString(fx->id()).startsWith("vst3:")) f["state"] = QString::fromLatin1(vst3::saveState(*fx).toBase64());
            inserts.append(f);
        }
        o["inserts"] = inserts;
        QJsonArray autom;
        for (const AutoPoint& p : t.volAuto) autom.append(QJsonArray{p.beat, double(p.value)});
        o["volAuto"] = autom;

        QJsonArray regs;
        for (const Region& r : t.regions) {
            QJsonObject ro;
            ro["name"] = r.name;
            ro["start"] = r.start;
            ro["midi"] = r.midi;
            if (r.midi) {
                ro["length"] = r.lengthBeats;
                QJsonArray notes;
                for (const Note& n : r.notes) notes.append(QJsonArray{n.pitch, n.start, n.length, n.velocity});
                ro["notes"] = notes;
                if (r.drummer) {
                    QJsonObject d;
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
                QString p = src ? src->path : r.pendingPath;
                if (src && p.isEmpty()) {   // a fresh recording: store it next to the project file
                    dir.mkpath(audioDir);
                    QString file;
                    do { file = dir.filePath(QString("%1/take_%2.wav").arg(audioDir).arg(take++)); } while (QFile::exists(file));
                    QString werr;
                    if (!writeWavFile(file, src->pcm, &werr)) {
                        if (error) *error = werr;
                        return false;
                    }
                    src->path = file;
                    p = file;
                }
                ro["path"] = dir.relativeFilePath(p);
                ro["srcOffset"] = double(r.srcOffset);
                ro["srcFrames"] = double(r.srcFrames);
                ro["follow"] = r.follow;
                ro["sourceBpm"] = r.sourceBpm;
                ro["semitones"] = r.semitones;
                if (r.gainDb != 0.f) ro["gainDb"] = double(r.gainDb);
                if (r.fadeIn > 0) ro["fadeIn"] = double(r.fadeIn);
                if (r.fadeOut > 0) ro["fadeOut"] = double(r.fadeOut);
            }
            regs.append(ro);
        }
        o["regions"] = regs;
        tracks.append(o);
    }

    const auto tm = tempoMap();
    QJsonArray tempo, meter;
    for (const auto& p : tm->tempoPoints()) tempo.append(QJsonObject{{"beat", p.beat}, {"bpm", p.bpm}, {"ramp", p.ramp}});
    for (const auto& m : tm->meterPoints()) meter.append(QJsonObject{{"bar", m.bar}, {"num", m.num}, {"den", m.den}});

    QJsonObject root;
    root["format"] = "MiniDAW";
    root["version"] = kProjectVersion;
    root["tempo"] = tempo;
    root["meter"] = meter;
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

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    m_path = path;
    m_dirty = false;
    return true;
}

// ------------------------------------------------------------------ load
bool Engine::loadProject(const QString& path, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (doc.isNull() || !doc.isObject() || doc.object()["format"].toString() != "MiniDAW") {
        if (error) *error = tr("Not a valid MiniDAW project file.");
        return false;
    }
    const QJsonObject root = doc.object();
    const QDir dir = QFileInfo(path).absoluteDir();

    stopRecording();
    m_playing = false;
    m_countRemain = 0;

    auto tm = std::make_shared<dsp::TempoMap>();
    std::vector<dsp::TempoPoint> tempo;
    for (const QJsonValue& v : root["tempo"].toArray()) {
        const QJsonObject o = v.toObject();
        tempo.push_back({o["beat"].toDouble(), o["bpm"].toDouble(120), o["ramp"].toBool()});
    }
    if (tempo.empty()) tempo.push_back({0, root["bpm"].toDouble(120), false});   // projects from v0.2
    std::vector<dsp::MeterPoint> meter;
    for (const QJsonValue& v : root["meter"].toArray()) {
        const QJsonObject o = v.toObject();
        meter.push_back({o["bar"].toInt(1), o["num"].toInt(4), o["den"].toInt(4)});
    }
    tm->setTempo(tempo);
    tm->setMeter(meter);
    { QMutexLocker l(&m_tempoMutex); m_tempo = tm; }

    QStringList toDecode;
    {
        QMutexLocker l(&m_mutex);
        m_tracks.clear();
        m_undo.clear();
        m_redo.clear();
        m_nextId = 1;
        m_audition.reset();
        for (const QJsonValue& tv : root["tracks"].toArray()) {
            const QJsonObject o = tv.toObject();
            Track t = makeTrack(typeFromString(o["type"].toString()), o["name"].toString());
            t.color = QColor(o["color"].toString(t.color.name()));
            t.volume = float(o["volume"].toDouble(0.8));
            t.pan = float(o["pan"].toDouble(0));
            t.sendA = float(o["sendA"].toDouble(0));
            t.sendB = float(o["sendB"].toDouble(0));
            t.mute = o["mute"].toBool();
            t.solo = o["solo"].toBool();
            if (t.type == TrackType::Drums) {
                const QJsonArray rows = o["steps"].toArray();
                for (int r = 0; r < kDrumRows && r < rows.size(); ++r) {
                    const QString s = rows[r].toString();
                    for (int k = 0; k < kSteps && k < s.size(); ++k) t.steps[size_t(r)][size_t(k)] = (s[k] == 'x');
                }
            }
            if (t.instrument) {
                const QString id = o["instrument"].toString(t.instrumentId);
                if (id != t.instrumentId) {
                    if (auto inst = makeInstrument(id)) { t.instrument = inst; t.instrumentId = id; }
                    else emit message(tr("Instrument plug-in not available: %1").arg(id));
                }
                t.preset = o["preset"].toInt();
                applyParams(*t.instrument, o["synth"].toObject());
                if (t.instrumentId.startsWith("vst3:") && o.contains("state"))
                    vst3::loadState(*t.instrument, QByteArray::fromBase64(o["state"].toString().toLatin1()));
            }
            for (const QJsonValue& iv : o["inserts"].toArray()) {
                const QJsonObject io = iv.toObject();
                auto fx = makeEffect(io["id"].toString());
                if (!fx) { emit message(tr("Effect plug-in not available: %1").arg(io["id"].toString())); continue; }
                fx->bypass = io["bypass"].toBool();
                applyParams(*fx, io["params"].toObject());
                if (io.contains("state")) vst3::loadState(*fx, QByteArray::fromBase64(io["state"].toString().toLatin1()));
                t.inserts.push_back(fx);
            }
            for (const QJsonValue& av : o["volAuto"].toArray()) {
                const QJsonArray a = av.toArray();
                if (a.size() == 2) t.volAuto.push_back({a[0].toDouble(), float(a[1].toDouble())});
            }
            for (const QJsonValue& rv : o["regions"].toArray()) {
                const QJsonObject ro = rv.toObject();
                Region r;
                r.name = ro["name"].toString();
                r.start = ro["start"].toDouble();
                r.midi = ro["midi"].toBool();
                if (r.midi) {
                    r.lengthBeats = ro["length"].toDouble(4);
                    for (const QJsonValue& nv : ro["notes"].toArray()) {
                        const QJsonArray a = nv.toArray();
                        if (a.size() == 4) r.notes.push_back(Note{a[0].toInt(), a[1].toDouble(), a[2].toDouble(), a[3].toInt()});
                    }
                    if (ro.contains("drummer")) {
                        const QJsonObject d = ro["drummer"].toObject();
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
                    r.pendingPath = QDir::cleanPath(dir.absoluteFilePath(ro["path"].toString()));
                    r.srcOffset = qint64(ro["srcOffset"].toDouble(ro["offset"].toDouble()));   // "offset" in v0.2 projects
                    r.srcFrames = qint64(ro["srcFrames"].toDouble(ro["frames"].toDouble()));
                    r.follow = ro["follow"].toBool();
                    r.sourceBpm = ro["sourceBpm"].toDouble();
                    r.semitones = ro["semitones"].toDouble();
                    r.gainDb = float(ro["gainDb"].toDouble(0));
                    r.fadeIn = qint64(ro["fadeIn"].toDouble(0));
                    r.fadeOut = qint64(ro["fadeOut"].toDouble(0));
                    if (!toDecode.contains(r.pendingPath)) toDecode << r.pendingPath;
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
    applyParams(*m_aux[0], root["auxA"].toObject());
    applyParams(*m_aux[1], root["auxB"].toObject());
    applyParams(*m_limiter, root["limiter"].toObject());
    m_path = path;
    m_flush |= 2;
    m_dirty = false;
    emit tracksChanged();
    emit tempoChanged();
    emit loopChanged();

    // Decode the referenced audio files in the background and attach them to their regions.
    for (const QString& p : toDecode) {
        decodeAsync(p, [this, p](std::shared_ptr<AudioData> data, QString) {
            if (!data) { emit message(tr("Missing audio file: %1").arg(p)); return; }
            const auto tmap = tempoMap();
            struct Job { int trackId; size_t ri; Region copy; };
            std::vector<Job> jobs;
            {
                QMutexLocker l(&m_mutex);
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
                QMutexLocker l(&m_mutex);
                for (const Job& j : jobs)
                    for (Track& t : m_tracks)
                        if (t.id == j.trackId && j.ri < t.regions.size() && t.regions[j.ri].source == data) {
                            Region& dst = t.regions[j.ri];
                            dst.audio = j.copy.audio; dst.offset = j.copy.offset; dst.frames = j.copy.frames; dst.flexBpm = j.copy.flexBpm;
                        }
            }
            emit regionsChanged();
        });
    }
    return true;
}

// ------------------------------------------------------------------ bounce
bool Engine::exportWav(const QString& path, QString* error) {
    return exportAudio(path, ExportOptions{}, error);
}

bool Engine::exportAudio(const QString& path, const ExportOptions& opt, QString* error) {
    const auto tempo = tempoMap();
    QMutexLocker lk(&m_mutex);
    m_tm = tempo.get();
    double startBeat = 0, endBeat = 0;
    if (opt.cycleOnly) {
        startBeat = m_loopA.load();
        endBeat = m_loopB.load();
        if (!(endBeat > startBeat)) {
            if (error) *error = tr("Set a cycle range first (right-drag on the ruler).");
            return false;
        }
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
    if (endBeat <= startBeat) {
        if (error) *error = tr("There is nothing to bounce.");
        return false;
    }
    const qint64 first = qint64(std::llround(tempo->beatToFrame(startBeat)));
    const qint64 end = qint64(std::llround(tempo->beatToFrame(endBeat))) + (opt.tail ? qint64(kSampleRate) * 2 : 0);   // tail for reverb / echo
    const qint64 total = end - first;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }

    constexpr int kBlock = 4096;
    const size_t need = size_t(kBlock) * 2;
    if (m_scratch.size() < need) {
        m_scratch.resize(need);
        m_trackBuf.resize(need);
        for (auto& a : m_auxBuf) a.resize(need);
    }
    // Renders [first, end) through the whole signal path and hands every block to 'sink'.
    auto pass = [&](auto&& sink) {
        flushDsp();
        m_expected = -1;
        for (qint64 p = first; p < end; p += kBlock) {
            const int n = int(std::min<qint64>(kBlock, end - p));
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

    const auto header = dsp::wavHeader(opt.format, kSampleRate, quint64(total));
    bool ok = f.write(reinterpret_cast<const char*>(header.data()), qint64(header.size())) == qint64(header.size());
    std::vector<std::uint8_t> bytes;
    std::uint32_t seed = 12345u;
    if (ok) pass([&](const float* b, int n) {
        bytes.clear();
        dsp::encodeSamples(b, size_t(n), opt.format, gain, opt.dither, seed, bytes);
        ok = f.write(reinterpret_cast<const char*>(bytes.data()), qint64(bytes.size())) == qint64(bytes.size());
        return ok;
    });
    if (!ok) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}
