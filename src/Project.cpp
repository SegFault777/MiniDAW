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
#include <QUrl>
#include <cmath>
#include "AudioUtil.h"
#include "Engine.h"

namespace {
const int kProjectVersion = 1;

QString typeToString(TrackType t) {
    return t == TrackType::Audio ? "audio" : t == TrackType::Instrument ? "instrument" : "drums";
}

TrackType typeFromString(const QString& s) {
    return s == "instrument" ? TrackType::Instrument : s == "drums" ? TrackType::Drums : TrackType::Audio;
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
        {
            QMutexLocker l(&m_mutex);
            Region r;
            r.name = data->name;
            r.start = std::max(0.0, startBeat);
            r.audio = data;
            r.frames = data->frames();
            m_tracks[size_t(t)].regions.push_back(r);
        }
        changed(false);
    });
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
        if (t.synth) {
            o["synthPreset"] = t.synthPreset;
            o["synth"] = paramsToJson(*t.synth);
        }
        QJsonArray inserts;
        for (const auto& fx : t.inserts) {
            QJsonObject f;
            f["id"] = fx->id();
            f["bypass"] = fx->bypass.load();
            f["params"] = paramsToJson(*fx);
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
            } else {
                QString p = r.audio ? r.audio->path : r.pendingPath;
                if (r.audio && p.isEmpty()) {   // a fresh recording: store it next to the project file
                    dir.mkpath(audioDir);
                    QString file;
                    do { file = dir.filePath(QString("%1/take_%2.wav").arg(audioDir).arg(take++)); } while (QFile::exists(file));
                    QString werr;
                    if (!writeWavFile(file, r.audio->pcm, &werr)) {
                        if (error) *error = werr;
                        return false;
                    }
                    r.audio->path = file;
                    p = file;
                }
                ro["path"] = dir.relativeFilePath(p);
                ro["offset"] = double(r.offset);
                ro["frames"] = double(r.frames);
            }
            regs.append(ro);
        }
        o["regions"] = regs;
        tracks.append(o);
    }

    QJsonObject root;
    root["format"] = "MiniDAW";
    root["version"] = kProjectVersion;
    root["bpm"] = bpm();
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
    QStringList toDecode;
    {
        QMutexLocker l(&m_mutex);
        m_tracks.clear();
        m_undo.clear();
        m_redo.clear();
        m_nextId = 1;
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
            if (t.synth) {
                t.synthPreset = o["synthPreset"].toInt();
                applyParams(*t.synth, o["synth"].toObject());
            }
            for (const QJsonValue& iv : o["inserts"].toArray()) {
                const QJsonObject io = iv.toObject();
                auto fx = dsp::createEffect(io["id"].toString().toStdString());
                if (!fx) continue;
                fx->bypass = io["bypass"].toBool();
                applyParams(*fx, io["params"].toObject());
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
                } else {
                    r.pendingPath = QDir::cleanPath(dir.absoluteFilePath(ro["path"].toString()));
                    r.offset = qint64(ro["offset"].toDouble());
                    r.frames = qint64(ro["frames"].toDouble());
                    if (!toDecode.contains(r.pendingPath)) toDecode << r.pendingPath;
                }
                t.regions.push_back(r);
            }
            m_tracks.push_back(std::move(t));
        }
        m_pos = 0;
    }
    m_bpm = root["bpm"].toDouble(120.0);
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
    emit loopChanged();

    // Decode the referenced audio files in the background and attach them to their regions.
    for (const QString& p : toDecode) {
        decodeAsync(p, [this, p](std::shared_ptr<AudioData> data, QString err) {
            if (!data) { emit message(tr("Missing audio file: %1").arg(p)); return; }
            {
                QMutexLocker l(&m_mutex);
                for (Track& t : m_tracks)
                    for (Region& r : t.regions)
                        if (!r.midi && !r.audio && r.pendingPath == p) {
                            r.audio = data;
                            if (r.frames <= 0 || r.offset + r.frames > data->frames()) r.frames = data->frames() - r.offset;
                            r.pendingPath.clear();
                        }
            }
            emit regionsChanged();
        });
    }
    return true;
}

// ------------------------------------------------------------------ bounce
bool Engine::exportWav(const QString& path, QString* error) {
    QMutexLocker lk(&m_mutex);
    const double fpb = framesPerBeat();
    double endBeat = 0;
    bool anyDrums = false;
    for (const Track& t : m_tracks) {
        for (const Region& r : t.regions) endBeat = std::max(endBeat, r.start + regionBeats(r));
        if (t.type == TrackType::Drums)
            for (const auto& row : t.steps)
                for (bool b : row) anyDrums |= b;
    }
    if (anyDrums) endBeat = std::max(endBeat, double(4 * kBeatsPerBar));   // at least 4 bars of drums
    if (endBeat <= 0) {
        if (error) *error = tr("There is nothing to bounce.");
        return false;
    }
    const qint64 end = qint64(std::llround(endBeat * fpb)) + qint64(kSampleRate) * 2;  // 2 s tail for reverb / echo

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    writeWavHeader(f, quint32(end * 4));

    constexpr int kBlock = 4096;
    const size_t need = size_t(kBlock) * 2;
    if (m_scratch.size() < need) {
        m_scratch.resize(need);
        m_trackBuf.resize(need);
        for (auto& a : m_auxBuf) a.resize(need);
    }
    flushDsp();
    m_expected = -1;
    std::vector<qint16> pcm(need);
    for (qint64 p = 0; p < end; p += kBlock) {
        const int n = int(std::min<qint64>(kBlock, end - p));
        renderBlock(p, n, m_scratch.data(), true);
        masterProcess(m_scratch.data(), n);
        for (int i = 0; i < n * 2; ++i) pcm[size_t(i)] = qint16(std::clamp(m_scratch[size_t(i)], -1.f, 1.f) * 32767.f);
        f.write(reinterpret_cast<const char*>(pcm.data()), qint64(n) * 4);
    }
    flushDsp();
    m_expected = -1;
    return true;
}
