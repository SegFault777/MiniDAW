// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Engine.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QAudioSource>
#include <QIODevice>
#include <QMediaDevices>
#include <cmath>
#include <random>
#include "AudioUtil.h"

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr qint64 kClickLen = 1400;  // metronome click length in frames

/// Pull-mode device: the audio sink reads from it and the engine mixes on demand.
class EngineDevice : public QIODevice {
public:
    EngineDevice(Engine* e, QObject* parent) : QIODevice(parent), m_e(e) { open(QIODevice::ReadOnly); }
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return (1 << 20) + QIODevice::bytesAvailable(); }
protected:
    qint64 readData(char* d, qint64 maxLen) override {
        const int frames = int(maxLen / 4);
        if (frames <= 0) return 0;
        m_e->fillInt16(reinterpret_cast<qint16*>(d), frames);
        return qint64(frames) * 4;
    }
    qint64 writeData(const char*, qint64) override { return -1; }
private:
    Engine* m_e;
};

/// Volume automation value (linear interpolation between breakpoints).
float autoGain(const Track& t, double beat) {
    const auto& p = t.volAuto;
    if (p.empty()) return t.volume;
    if (beat <= p.front().beat) return p.front().value;
    if (beat >= p.back().beat) return p.back().value;
    for (size_t i = 1; i < p.size(); ++i) {
        if (beat <= p[i].beat) {
            const float f = float((beat - p[i - 1].beat) / std::max(p[i].beat - p[i - 1].beat, 1e-9));
            return p[i - 1].value + (p[i].value - p[i - 1].value) * f;
        }
    }
    return p.back().value;
}
}  // namespace

// ------------------------------------------------------- audio output / input objects
// Both live on a dedicated audio thread so the UI can never starve them.
class AudioOutput : public QObject {
    Q_OBJECT
public:
    explicit AudioOutput(Engine* e) : m_e(e) {}
public slots:
    void start() {
        stop();
        QAudioFormat f;
        f.setSampleRate(kSampleRate);
        f.setChannelCount(2);
        f.setSampleFormat(QAudioFormat::Int16);
        const QAudioDevice dev = QMediaDevices::defaultAudioOutput();
        if (dev.isNull() || !dev.isFormatSupported(f)) {
            emit failed(QStringLiteral("No usable audio output device was found."));
            return;
        }
        m_dev = new EngineDevice(m_e, this);
        m_sink = new QAudioSink(dev, f, this);
        m_sink->setBufferSize(2048 * 4);  // about 46 ms
        m_sink->start(m_dev);
    }
    void stop() {
        if (m_sink) { m_sink->stop(); delete m_sink; m_sink = nullptr; }
        delete m_dev;
        m_dev = nullptr;
    }
signals:
    void failed(const QString& reason);
private:
    Engine* m_e;
    QAudioSink* m_sink = nullptr;
    QIODevice* m_dev = nullptr;
};

class AudioInput : public QObject {
    Q_OBJECT
public:
    explicit AudioInput(Engine* e) : m_e(e) {}
public slots:
    void start() {
        stop();
        const QAudioDevice dev = QMediaDevices::defaultAudioInput();
        if (dev.isNull()) {
            emit failed(QStringLiteral("No audio input device was found."));
            return;
        }
        m_fmt = dev.preferredFormat();
        m_rs = Resampler();
        m_src = new QAudioSource(dev, m_fmt, this);
        m_dev = m_src->start();
        if (!m_dev) {
            emit failed(QStringLiteral("Could not start recording."));
            return;
        }
        connect(m_dev, &QIODevice::readyRead, this, [this] {
            const QByteArray b = m_dev->readAll();
            const int bpf = m_fmt.bytesPerFrame();
            if (bpf <= 0) return;
            std::vector<float> st, res;
            if (!toStereoFloat(b.constData(), int(b.size() / bpf), m_fmt, st)) return;
            m_rs.process(st, m_fmt.sampleRate(), res);
            m_e->pushRecorded(res);
        });
    }
    void stop() {
        if (m_src) { m_src->stop(); delete m_src; m_src = nullptr; }
        m_dev = nullptr;
    }
signals:
    void failed(const QString& reason);
private:
    Engine* m_e;
    QAudioSource* m_src = nullptr;
    QIODevice* m_dev = nullptr;
    QAudioFormat m_fmt;
    Resampler m_rs;
};

// ------------------------------------------------------------------ construction
Engine::Engine(QObject* parent) : QObject(parent) {
    synthDrums();
    auto reverb = dsp::createEffect("reverb");
    reverb->find("Mix")->set(1.f);
    reverb->find("Size")->set(0.8f);
    auto echo = dsp::createEffect("echo");
    echo->find("Mix")->set(1.f);
    m_aux = {reverb, echo};
    m_limiter = dsp::createEffect("limiter");
    newProject();

    m_out = new AudioOutput(this);
    m_in = new AudioInput(this);
    m_out->moveToThread(&m_thread);
    m_in->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_out, &QObject::deleteLater);
    connect(&m_thread, &QThread::finished, m_in, &QObject::deleteLater);
    connect(m_out, &AudioOutput::failed, this, &Engine::message);
    connect(m_in, &AudioInput::failed, this, &Engine::message);
    m_thread.start();
    // The output stream runs permanently so instruments can be played while the transport is stopped.
    QMetaObject::invokeMethod(m_out, &AudioOutput::start, Qt::QueuedConnection);
    auto* devices = new QMediaDevices(this);
    connect(devices, &QMediaDevices::audioOutputsChanged, this,
            [this] { QMetaObject::invokeMethod(m_out, &AudioOutput::start, Qt::QueuedConnection); });
}

Engine::~Engine() {
    stopRecording();
    QMetaObject::invokeMethod(m_out, &AudioOutput::stop, Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(m_in, &AudioInput::stop, Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
}

void Engine::synthDrums() {
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> nd(-1.f, 1.f);
    const double sr = kSampleRate;
    {   // kick: pitch-swept sine
        auto& v = m_drum[0];
        v.resize(size_t(0.40 * sr));
        double ph = 0;
        for (size_t i = 0; i < v.size(); ++i) {
            const double t = i / sr;
            ph += 2 * kPi * (42 + 120 * std::exp(-t * 30)) / sr;
            v[i] = float(std::sin(ph) * std::exp(-t * 7.5) * 0.95);
        }
    }
    {   // snare: noise plus a short tone
        auto& v = m_drum[1];
        v.resize(size_t(0.25 * sr));
        for (size_t i = 0; i < v.size(); ++i) {
            const double t = i / sr;
            v[i] = float(nd(rng) * std::exp(-t * 20) * 0.6 + std::sin(2 * kPi * 185 * t) * std::exp(-t * 30) * 0.5);
        }
    }
    {   // hi-hat: differentiated (high-passed) noise
        auto& v = m_drum[2];
        v.resize(size_t(0.07 * sr));
        float prev = 0;
        for (size_t i = 0; i < v.size(); ++i) {
            const double t = i / sr;
            const float n = nd(rng);
            v[i] = (n - prev) * 0.5f * float(std::exp(-t * 70));
            prev = n;
        }
    }
    {   // clap: three quick noise bursts and a tail
        auto& v = m_drum[3];
        v.resize(size_t(0.22 * sr));
        for (size_t i = 0; i < v.size(); ++i) {
            const double t = i / sr;
            double env = std::exp(-t * 22);
            if (t < 0.035) env *= (std::fmod(t, 0.012) < 0.005 ? 1.0 : 0.3);
            v[i] = float(nd(rng) * env * 0.55);
        }
    }
    for (const auto& v : m_drum) m_drumMax = std::max<qint64>(m_drumMax, qint64(v.size()));
}

// ------------------------------------------------------------------ model
Track Engine::makeTrack(TrackType type, const QString& name) {
    Track t;
    t.id = m_nextId++;
    t.type = type;
    t.name = name;
    switch (type) {
    case TrackType::Audio: t.color = QColor(70, 150, 255); break;
    case TrackType::Instrument:
        t.color = QColor(110, 200, 110);
        t.synth = std::make_shared<dsp::Synth>();
        t.synth->setPreset(0);
        break;
    case TrackType::Drums: t.color = QColor(255, 170, 60); break;
    }
    return t;
}

void Engine::changed(bool structural) {
    m_dirty = true;
    if (structural) emit tracksChanged();
    else emit regionsChanged();
}

std::vector<Track> Engine::snapshot() const { QMutexLocker l(&m_mutex); return m_tracks; }
int Engine::trackCount() const { QMutexLocker l(&m_mutex); return int(m_tracks.size()); }

DrumPattern Engine::drumPattern(int track) const {
    QMutexLocker l(&m_mutex);
    if (track < 0 || track >= int(m_tracks.size())) return {};
    return m_tracks[size_t(track)].steps;
}

double Engine::regionBeats(const Region& r) const {
    return r.midi ? r.lengthBeats : double(r.frames) / framesPerBeat();
}

double Engine::projectEndBeats() const {
    QMutexLocker l(&m_mutex);
    double e = 0;
    for (const Track& t : m_tracks)
        for (const Region& r : t.regions) e = std::max(e, r.start + regionBeats(r));
    return e;
}

void Engine::newProject() {
    stopRecording();
    m_playing = false;
    m_countRemain = 0;
    {
        QMutexLocker l(&m_mutex);
        m_tracks.clear();
        m_undo.clear();
        m_redo.clear();
        m_nextId = 1;
        Track d = makeTrack(TrackType::Drums, "Drum Machine 1");
        for (int s : {0, 4, 8, 12}) d.steps[0][size_t(s)] = true;
        for (int s : {4, 12}) d.steps[1][size_t(s)] = true;
        for (int s : {2, 6, 10, 14}) d.steps[2][size_t(s)] = true;
        m_tracks.push_back(d);
        m_tracks.push_back(makeTrack(TrackType::Instrument, "Software Instrument 1"));
        m_pos = 0;
    }
    m_bpm = 120.0;
    m_loopOn = false;
    m_loopA = 0.0;
    m_loopB = 16.0;
    m_path.clear();
    m_flush |= 2;
    m_dirty = false;
    emit tracksChanged();
    emit loopChanged();
}

int Engine::addTrack(TrackType type, const QString& name) {
    checkpoint();
    int idx;
    {
        QMutexLocker l(&m_mutex);
        QString n = name;
        if (n.isEmpty()) {
            int count = 0;
            for (const Track& t : m_tracks) count += (t.type == type) ? 1 : 0;
            const char* base = type == TrackType::Audio ? "Audio" : type == TrackType::Instrument ? "Software Instrument" : "Drum Machine";
            n = QString("%1 %2").arg(base).arg(count + 1);
        }
        m_tracks.push_back(makeTrack(type, n));
        idx = int(m_tracks.size()) - 1;
    }
    changed(true);
    return idx;
}

void Engine::removeTrack(int i) {
    checkpoint();
    {
        QMutexLocker l(&m_mutex);
        if (i < 0 || i >= int(m_tracks.size())) return;
        m_tracks.erase(m_tracks.begin() + i);
    }
    changed(true);
}

void Engine::renameTrack(int i, const QString& name) {
    { QMutexLocker l(&m_mutex); if (i < 0 || i >= int(m_tracks.size())) return; m_tracks[size_t(i)].name = name; }
    changed(true);
}

#define TRACK_SET(...) { QMutexLocker l(&m_mutex); if (i < 0 || i >= int(m_tracks.size())) return; Track& t = m_tracks[size_t(i)]; __VA_ARGS__; } m_dirty = true;
void Engine::setVolume(int i, float v) { TRACK_SET(t.volume = std::clamp(v, 0.f, 1.5f)) }
void Engine::setPan(int i, float p) { TRACK_SET(t.pan = std::clamp(p, -1.f, 1.f)) }
void Engine::setSendA(int i, float v) { TRACK_SET(t.sendA = std::clamp(v, 0.f, 1.f)) }
void Engine::setSendB(int i, float v) { TRACK_SET(t.sendB = std::clamp(v, 0.f, 1.f)) }
void Engine::setMute(int i, bool on) { TRACK_SET(t.mute = on) }
void Engine::setSolo(int i, bool on) { TRACK_SET(t.solo = on) }
#undef TRACK_SET

void Engine::setArmed(int i, bool on) {
    {
        QMutexLocker l(&m_mutex);
        for (size_t k = 0; k < m_tracks.size(); ++k) m_tracks[k].armed = on && int(k) == i;  // one armed track at a time
    }
}

void Engine::addInsert(int track, const QString& effectId) {
    auto fx = dsp::createEffect(effectId.toStdString());
    if (!fx) return;
    checkpoint();
    {
        QMutexLocker l(&m_mutex);
        if (track < 0 || track >= int(m_tracks.size())) return;
        m_tracks[size_t(track)].inserts.push_back(fx);
    }
    changed(true);
}

void Engine::removeInsert(int track, int slot) {
    checkpoint();
    {
        QMutexLocker l(&m_mutex);
        if (track < 0 || track >= int(m_tracks.size())) return;
        auto& v = m_tracks[size_t(track)].inserts;
        if (slot < 0 || slot >= int(v.size())) return;
        v.erase(v.begin() + slot);
    }
    changed(true);
}

void Engine::setInsertBypass(int track, int slot, bool bypass) {
    QMutexLocker l(&m_mutex);
    if (track < 0 || track >= int(m_tracks.size())) return;
    auto& v = m_tracks[size_t(track)].inserts;
    if (slot >= 0 && slot < int(v.size())) v[size_t(slot)]->bypass = bypass;
    m_dirty = true;
}

void Engine::setSynthPreset(int track, int preset) {
    QMutexLocker l(&m_mutex);
    if (track < 0 || track >= int(m_tracks.size()) || !m_tracks[size_t(track)].synth) return;
    m_tracks[size_t(track)].synth->setPreset(preset);
    m_tracks[size_t(track)].synthPreset = preset;
    m_dirty = true;
}

int Engine::addMidiRegion(int track, double startBeat, double lengthBeats) {
    checkpoint();
    int idx = -1;
    {
        QMutexLocker l(&m_mutex);
        if (track < 0 || track >= int(m_tracks.size()) || m_tracks[size_t(track)].type != TrackType::Instrument) return -1;
        Region r;
        r.midi = true;
        r.name = "MIDI Region";
        r.start = std::max(0.0, startBeat);
        r.lengthBeats = std::max(0.25, lengthBeats);
        m_tracks[size_t(track)].regions.push_back(r);
        idx = int(m_tracks[size_t(track)].regions.size()) - 1;
    }
    changed(false);
    return idx;
}

#define REGION_OP(...) { QMutexLocker l(&m_mutex); \
    if (t < 0 || t >= int(m_tracks.size()) || r < 0 || r >= int(m_tracks[size_t(t)].regions.size())) return; \
    auto& regs = m_tracks[size_t(t)].regions; Region& rg = regs[size_t(r)]; __VA_ARGS__ }

void Engine::moveRegion(int t, int r, double startBeat) {
    REGION_OP(rg.start = std::max(0.0, startBeat);)
    changed(false);
}

void Engine::resizeRegion(int t, int r, double lengthBeats) {
    REGION_OP(
        if (rg.midi) rg.lengthBeats = std::max(0.25, lengthBeats);
        else if (rg.audio) {
            const qint64 maxFrames = rg.audio->frames() - rg.offset;
            rg.frames = std::clamp<qint64>(qint64(std::llround(lengthBeats * framesPerBeat())), 1, maxFrames);
        })
    changed(false);
}

void Engine::removeRegion(int t, int r) {
    checkpoint();
    REGION_OP(regs.erase(regs.begin() + r);)
    changed(false);
}

void Engine::splitRegion(int t, int r, double atBeat) {
    checkpoint();
    REGION_OP(
        const double len = regionBeats(rg), rel = atBeat - rg.start;
        if (rel <= 0.01 || rel >= len - 0.01) return;
        Region a = rg, b = rg;
        b.start = atBeat;
        if (a.midi) {
            a.lengthBeats = rel;
            b.lengthBeats = len - rel;
            b.notes.clear();
            a.notes.clear();
            for (const Note& n : rg.notes) {
                if (n.start >= rel) { Note m = n; m.start -= rel; b.notes.push_back(m); }
                else a.notes.push_back(n);
            }
        } else {
            const qint64 rf = qint64(std::llround(rel * framesPerBeat()));
            if (rf <= 0 || rf >= rg.frames) return;
            a.frames = rf;
            b.offset += rf;
            b.frames -= rf;
        }
        regs[size_t(r)] = a;
        regs.insert(regs.begin() + r + 1, b);)
    changed(false);
}

int Engine::duplicateRegion(int t, int r) {
    checkpoint();
    int idx = -1;
    {
        QMutexLocker l(&m_mutex);
        if (t < 0 || t >= int(m_tracks.size()) || r < 0 || r >= int(m_tracks[size_t(t)].regions.size())) return -1;
        auto& regs = m_tracks[size_t(t)].regions;
        Region c = regs[size_t(r)];
        c.start = c.start + regionBeats(c);   // place the copy directly after the original
        regs.push_back(c);
        idx = int(regs.size()) - 1;
    }
    changed(false);
    return idx;
}

void Engine::setRegionNotes(int t, int r, const std::vector<Note>& notes) {
    REGION_OP(if (!rg.midi) return; rg.notes = notes;)
    changed(false);
}
#undef REGION_OP

void Engine::setStep(int track, int row, int step, bool on) {
    if (row < 0 || row >= kDrumRows || step < 0 || step >= kSteps) return;
    QMutexLocker l(&m_mutex);
    if (track < 0 || track >= int(m_tracks.size())) return;
    m_tracks[size_t(track)].steps[size_t(row)][size_t(step)] = on;
    m_dirty = true;
}

void Engine::setDrumPattern(int track, const DrumPattern& p) {
    checkpoint();
    { QMutexLocker l(&m_mutex); if (track < 0 || track >= int(m_tracks.size())) return; m_tracks[size_t(track)].steps = p; }
    changed(false);
}

void Engine::setVolAuto(int track, const std::vector<AutoPoint>& pts) {
    {
        QMutexLocker l(&m_mutex);
        if (track < 0 || track >= int(m_tracks.size())) return;
        auto sorted = pts;
        std::sort(sorted.begin(), sorted.end(), [](const AutoPoint& a, const AutoPoint& b) { return a.beat < b.beat; });
        m_tracks[size_t(track)].volAuto = sorted;
    }
    changed(false);
}

// ------------------------------------------------------------------ undo / redo
void Engine::pushUndo() {
    QMutexLocker l(&m_mutex);
    m_undo.push_back(m_tracks);
    if (m_undo.size() > 60) m_undo.erase(m_undo.begin());
    m_redo.clear();
    m_dirty = true;
}

void Engine::checkpoint() { pushUndo(); }

void Engine::undo() {
    {
        QMutexLocker l(&m_mutex);
        if (m_undo.empty()) return;
        m_redo.push_back(m_tracks);
        m_tracks = std::move(m_undo.back());
        m_undo.pop_back();
    }
    changed(true);
}

void Engine::redo() {
    {
        QMutexLocker l(&m_mutex);
        if (m_redo.empty()) return;
        m_undo.push_back(m_tracks);
        m_tracks = std::move(m_redo.back());
        m_redo.pop_back();
    }
    changed(true);
}

// ------------------------------------------------------------------ transport
void Engine::startPlayback() {
    m_expected = -1;   // forces a discontinuity: synth voices are re-synchronised
    m_flush |= 2;
    m_playing = true;
}

void Engine::play() {
    if (!m_playing) startPlayback();
}

void Engine::pause() {
    if (!m_playing) return;
    stopRecording();
    m_playing = false;
    m_countRemain = 0;
    m_flush |= 1;
    m_idle = qint64(kSampleRate) * 4;  // let reverb / echo tails ring out
}

void Engine::stop() {
    if (m_playing) pause();
    else seekBeats(m_loopOn ? double(m_loopA) : 0.0);
}

void Engine::seekBeats(double beat) {
    QMutexLocker l(&m_mutex);   // keeps the audio thread from overwriting the new position
    m_pos = qint64(std::llround(std::max(0.0, beat) * framesPerBeat()));
}

void Engine::setBpm(double b) {
    b = std::clamp(b, 30.0, 300.0);
    const double old = m_bpm;
    if (old == b) return;
    QMutexLocker l(&m_mutex);
    m_bpm = b;
    m_pos = qint64(std::llround(double(m_pos.load()) * old / b));  // keep the musical position
    m_dirty = true;
}

void Engine::setLoop(bool on, double a, double b) {
    if (a >= 0 && b > a) { m_loopA = a; m_loopB = b; }
    m_loopOn = on;
    m_dirty = true;
    emit loopChanged();
}

void Engine::masterPeaks(float& l, float& r) const {
    QMutexLocker lk(&m_mutex);
    l = m_masterL;
    r = m_masterR;
}

// ------------------------------------------------------------------ live notes and recording
void Engine::noteOn(int track, int pitch, int velocity) {
    bool onRecTrack = false;
    {
        QMutexLocker l(&m_mutex);
        if (track < 0 || track >= int(m_tracks.size())) return;
        Track& t = m_tracks[size_t(track)];
        if (t.type != TrackType::Instrument || !t.synth) return;
        t.synth->noteOn(pitch, velocity);
        m_idle = qint64(kSampleRate) * 6;
        onRecTrack = (t.id == m_recTrackId.load());
    }
    if (onRecTrack && m_recording && m_playing && m_countRemain == 0 && !m_recAudio)
        m_recPending[pitch] = {positionBeats(), velocity};
}

void Engine::noteOff(int track, int pitch) {
    {
        QMutexLocker l(&m_mutex);
        if (track < 0 || track >= int(m_tracks.size())) return;
        Track& t = m_tracks[size_t(track)];
        if (t.synth) t.synth->noteOff(pitch);
    }
    auto it = m_recPending.find(pitch);
    if (it != m_recPending.end()) {
        const double end = positionBeats();
        m_recNotes.push_back(Note{pitch, it->second.start, std::max(0.05, end - it->second.start), it->second.vel});
        m_recPending.erase(it);
    }
}

bool Engine::startRecording() {
    if (m_recording) return true;
    int tid = -1;
    bool audio = false;
    {
        QMutexLocker l(&m_mutex);
        for (const Track& t : m_tracks)
            if (t.armed && t.type != TrackType::Drums) { tid = t.id; audio = (t.type == TrackType::Audio); break; }
    }
    if (tid < 0) {
        emit message(tr("Arm an audio or instrument track first (R button on the track header)."));
        return false;
    }
    m_recStartBeat = positionBeats();
    m_recPending.clear();
    m_recNotes.clear();
    { QMutexLocker r(&m_recMutex); m_recBuf.clear(); }
    m_recTrackId = tid;
    m_recAudio = audio;
    m_recording = true;
    if (!m_playing) {
        if (m_countIn) {
            m_countTotal = qint64(std::llround(kBeatsPerBar * framesPerBeat()));
            m_countRemain = m_countTotal.load();
        }
        startPlayback();
    }
    if (audio && m_countRemain == 0) QMetaObject::invokeMethod(m_in, &AudioInput::start, Qt::QueuedConnection);
    return true;
}

void Engine::stopRecording() {
    if (!m_recording.exchange(false)) return;
    const bool audio = m_recAudio;
    if (audio) QMetaObject::invokeMethod(m_in, &AudioInput::stop, Qt::BlockingQueuedConnection);
    const double endBeat = positionBeats();
    std::vector<float> take;
    { QMutexLocker r(&m_recMutex); take.swap(m_recBuf); }
    for (const auto& [pitch, p] : m_recPending)
        m_recNotes.push_back(Note{pitch, p.start, std::max(0.05, endBeat - p.start), p.vel});
    m_recPending.clear();
    std::vector<Note> notes;
    notes.swap(m_recNotes);
    if (audio ? take.size() < size_t(kSampleRate / 10) : notes.empty()) return;  // ignore accidental taps

    pushUndo();
    {
        QMutexLocker l(&m_mutex);
        int ti = -1;
        for (size_t i = 0; i < m_tracks.size(); ++i)
            if (m_tracks[i].id == m_recTrackId.load()) ti = int(i);
        if (ti < 0) return;
        Region r;
        r.start = m_recStartBeat;
        if (audio) {
            auto data = std::make_shared<AudioData>();
            data->name = m_tracks[size_t(ti)].name + " take";
            data->pcm = std::move(take);
            computePeaks(*data);
            r.name = data->name;
            r.frames = data->frames();
            r.audio = data;
        } else {
            r.midi = true;
            r.name = "Recorded MIDI";
            double maxEnd = m_recStartBeat;
            for (const Note& n : notes) maxEnd = std::max(maxEnd, n.start + n.length);
            r.lengthBeats = std::max(4.0, std::ceil((maxEnd - m_recStartBeat) / kBeatsPerBar) * kBeatsPerBar);
            for (Note n : notes) { n.start = std::max(0.0, n.start - m_recStartBeat); r.notes.push_back(n); }
        }
        m_tracks[size_t(ti)].regions.push_back(std::move(r));
    }
    changed(false);
}

void Engine::pushRecorded(const std::vector<float>& stereo) {
    QMutexLocker l(&m_recMutex);
    m_recBuf.insert(m_recBuf.end(), stereo.begin(), stereo.end());
}

// ------------------------------------------------------------------ rendering
void Engine::flushDsp() {
    for (Track& t : m_tracks) {
        for (auto& fx : t.inserts) fx->reset();
        if (t.synth) t.synth->reset();
    }
    for (auto& a : m_aux) a->reset();
    m_limiter->reset();
}

void Engine::addClicks(qint64 tf, int frames, float* out) {
    const double fpb = framesPerBeat();
    const qint64 first = std::max<qint64>(0, qint64((tf - kClickLen) / fpb) - 1);
    const qint64 last = qint64((tf + frames) / fpb);
    for (qint64 k = first; k <= last; ++k) {
        const qint64 st = qint64(std::llround(k * fpb));
        const qint64 a = std::max(tf, st), b = std::min<qint64>(tf + frames, st + kClickLen);
        const double freq = (k % kBeatsPerBar == 0) ? 1600.0 : 1000.0;
        for (qint64 f = a; f < b; ++f) {
            const double n = double(f - st);
            const float v = float(std::sin(2 * kPi * freq * n / kSampleRate) * std::exp(-n / 250.0) * 0.35);
            out[(f - tf) * 2] += v;
            out[(f - tf) * 2 + 1] += v;
        }
    }
}

// Mixes [pos, pos + frames) of the whole project into 'out'. m_mutex must be held.
// 'running' is false while the transport is stopped: only live notes and effect tails are rendered.
void Engine::renderBlock(qint64 pos, int frames, float* out, bool running) {
    const size_t n2 = size_t(frames) * 2;
    std::fill(out, out + n2, 0.f);
    for (auto& a : m_auxBuf) std::fill(a.begin(), a.begin() + std::ptrdiff_t(n2), 0.f);

    const double fpb = framesPerBeat();
    const double beat0 = double(pos) / fpb, beat1 = double(pos + frames) / fpb;
    const bool jump = running && m_expected.load() != pos;
    const bool anySolo = std::any_of(m_tracks.begin(), m_tracks.end(), [](const Track& t) { return t.solo; });
    float* tb = m_trackBuf.data();

    for (Track& t : m_tracks) {
        if (t.mute || (anySolo && !t.solo)) {
            t.peakL = t.peakR = 0;
            if (t.synth) t.synth->allNotesOff();  // never leave voices hanging on a silent track
            continue;
        }
        std::fill(tb, tb + n2, 0.f);

        if (t.type == TrackType::Audio && running) {
            for (const Region& rg : t.regions) {
                if (rg.midi || !rg.audio) continue;
                const qint64 st = qint64(std::llround(rg.start * fpb));
                const qint64 a = std::max(pos, st), b = std::min<qint64>(pos + frames, st + rg.frames);
                if (a >= b) continue;
                const float* s = rg.audio->pcm.data() + (rg.offset + a - st) * 2;
                for (qint64 f = a; f < b; ++f, s += 2) {
                    tb[(f - pos) * 2] += s[0];
                    tb[(f - pos) * 2 + 1] += s[1];
                }
            }
        } else if (t.type == TrackType::Drums && running) {
            const double stepLen = fpb / 4.0;
            const qint64 first = std::max<qint64>(0, qint64((pos - m_drumMax) / stepLen) - 1);
            const qint64 last = qint64((pos + frames) / stepLen);
            for (qint64 k = first; k <= last; ++k) {
                const size_t s = size_t(k % kSteps);
                const qint64 st = qint64(std::llround(k * stepLen));
                for (int r = 0; r < kDrumRows; ++r) {
                    if (!t.steps[size_t(r)][s]) continue;
                    const auto& snd = m_drum[size_t(r)];
                    const qint64 a = std::max(pos, st), b = std::min<qint64>(pos + frames, st + qint64(snd.size()));
                    for (qint64 f = a; f < b; ++f) {
                        const float v = snd[size_t(f - st)];
                        tb[(f - pos) * 2] += v;
                        tb[(f - pos) * 2 + 1] += v;
                    }
                }
            }
        } else if (t.type == TrackType::Instrument && t.synth) {
            if (!running) {
                t.synth->render(tb, frames);   // live notes only
            } else {
                m_events.clear();
                if (jump) t.synth->allNotesOff();
                for (const Region& rg : t.regions) {
                    if (!rg.midi) continue;
                    const double regEnd = rg.start + rg.lengthBeats;
                    for (const Note& n : rg.notes) {
                        if (n.start >= rg.lengthBeats) continue;
                        const qint64 s = qint64(std::llround((rg.start + n.start) * fpb));
                        qint64 e = qint64(std::llround(std::min(rg.start + n.start + n.length, regEnd) * fpb));
                        if (e <= s) e = s + 1;
                        if (s >= pos && s < pos + frames) m_events.push_back({int(s - pos), true, n.pitch, n.velocity});
                        if (e >= pos && e < pos + frames) m_events.push_back({int(e - pos), false, n.pitch, 0});
                        if (jump && s < pos && e > pos) m_events.push_back({0, true, n.pitch, n.velocity});  // note already sounding
                    }
                }
                std::stable_sort(m_events.begin(), m_events.end(), [](const Ev& a, const Ev& b) {
                    return a.off != b.off ? a.off < b.off : a.on < b.on;   // releases before attacks
                });
                int cur = 0;
                for (const Ev& ev : m_events) {
                    if (ev.off > cur) { t.synth->render(tb + cur * 2, ev.off - cur); cur = ev.off; }
                    if (ev.on) t.synth->noteOn(ev.pitch, ev.vel);
                    else t.synth->noteOff(ev.pitch);
                }
                if (cur < frames) t.synth->render(tb + cur * 2, frames - cur);
            }
        }

        for (auto& fx : t.inserts)
            if (!fx->bypass) fx->process(tb, frames);

        // Fader (with volume automation), pan law and aux sends.
        const bool automated = !t.volAuto.empty();
        const float g0 = automated ? autoGain(t, beat0) : t.volume;
        const float g1 = automated ? autoGain(t, beat1) : t.volume;
        const float ang = (t.pan + 1.f) * 0.25f * float(kPi);
        const float pl = std::cos(ang) * 1.41421f, pr = std::sin(ang) * 1.41421f;
        float peakL = 0, peakR = 0;
        for (int i = 0; i < frames; ++i) {
            const float g = g0 + (g1 - g0) * float(i) / float(frames);
            const float a = tb[2 * i] * g, b = tb[2 * i + 1] * g;
            if (t.sendA > 0.f) { m_auxBuf[0][size_t(2 * i)] += a * t.sendA; m_auxBuf[0][size_t(2 * i + 1)] += b * t.sendA; }
            if (t.sendB > 0.f) { m_auxBuf[1][size_t(2 * i)] += a * t.sendB; m_auxBuf[1][size_t(2 * i + 1)] += b * t.sendB; }
            const float l = a * pl, r = b * pr;
            out[2 * i] += l;
            out[2 * i + 1] += r;
            peakL = std::max(peakL, std::fabs(l));
            peakR = std::max(peakR, std::fabs(r));
        }
        t.peakL = peakL;
        t.peakR = peakR;
    }

    for (size_t bus = 0; bus < m_aux.size(); ++bus) {
        if (!m_aux[bus]->bypass) m_aux[bus]->process(m_auxBuf[bus].data(), frames);
        for (size_t i = 0; i < n2; ++i) out[i] += m_auxBuf[bus][i];
    }
    if (running && m_metro) addClicks(pos, frames, out);
    if (running) m_expected = pos + frames;
}

void Engine::masterProcess(float* buf, int frames) {
    const float g = m_master;
    for (int i = 0; i < frames * 2; ++i) buf[i] *= g;
    if (!m_limiter->bypass) m_limiter->process(buf, frames);
    float ml = 0, mr = 0;
    for (int i = 0; i < frames; ++i) {
        ml = std::max(ml, std::fabs(buf[2 * i]));
        mr = std::max(mr, std::fabs(buf[2 * i + 1]));
    }
    m_masterL = ml;
    m_masterR = mr;
}

void Engine::fillInt16(qint16* out, int frames) {
    QMutexLocker lk(&m_mutex);
    const size_t need = size_t(frames) * 2;
    if (m_scratch.size() < need) {   // grows only; no allocation once warmed up
        m_scratch.resize(need);
        m_trackBuf.resize(need);
        for (auto& a : m_auxBuf) a.resize(need);
    }
    const int fl = m_flush.exchange(0);
    if (fl & 2) flushDsp();
    else if (fl & 1)
        for (Track& t : m_tracks)
            if (t.synth) t.synth->allNotesOff();

    const bool running = m_playing;
    if (!running && m_idle <= 0) {   // idle: no work and no CPU use
        std::fill(out, out + need, qint16(0));
        m_masterL = m_masterR = 0;
        for (Track& t : m_tracks) t.peakL = t.peakR = 0;
        return;
    }

    const double fpb = framesPerBeat();
    const qint64 la = qint64(std::llround(m_loopA * fpb)), lb = qint64(std::llround(m_loopB * fpb));
    const bool looping = m_loopOn && lb > la;
    qint64 pos = m_pos;
    int done = 0;
    while (done < frames) {
        int n = frames - done;
        float* dst = m_scratch.data() + size_t(done) * 2;
        if (running && m_countRemain > 0) {   // count-in: clicks only, the playhead stays put
            n = int(std::min<qint64>(n, m_countRemain));
            std::fill(dst, dst + size_t(n) * 2, 0.f);
            addClicks(m_countTotal - m_countRemain, n, dst);
            m_countRemain -= n;
            if (m_countRemain == 0 && m_recording && m_recAudio)
                QMetaObject::invokeMethod(m_in, &AudioInput::start, Qt::QueuedConnection);
            done += n;
            continue;
        }
        if (running && looping) {
            if (pos >= lb) pos = la;
            n = int(std::min<qint64>(n, lb - pos));
        }
        renderBlock(pos, n, dst, running);
        if (running) pos += n;
        done += n;
    }
    if (running) m_pos = pos;
    else m_idle -= frames;

    masterProcess(m_scratch.data(), frames);
    for (size_t i = 0; i < need; ++i) out[i] = qint16(std::clamp(m_scratch[i], -1.f, 1.f) * 32767.f);
}

#include "Engine.moc"
