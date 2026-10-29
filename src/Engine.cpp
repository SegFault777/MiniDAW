// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Engine.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QAudioSource>
#include <QFile>
#include <QIODevice>
#include <QMediaDevices>
#include <QTimer>
#include <cmath>
#include "AudioUtil.h"
#include "dsp/AudioOps.h"
#include "dsp/MidiFile.h"
#include "dsp/Stretch.h"
#include "vst3/Vst3Host.h"

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr qint64 kClickLen = 1400;   // metronome click length in frames
constexpr int kToneFrames = kSampleRate * 6 / 10;
const int kRowPiece[kDrumRows] = {dsp::Kick, dsp::Snare, dsp::HatClosed, dsp::Clap};

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

std::shared_ptr<dsp::Effect> makeEffect(const QString& id) {
    if (id.startsWith("vst3:")) return vst3::createEffect(id);
    return dsp::createEffect(id.toStdString());
}

std::shared_ptr<dsp::Instrument> makeInstrument(const QString& id) {
    if (id == "synth") { auto s = std::make_shared<dsp::Synth>(); s->setPreset(0); return s; }
    if (id == "drumkit") return std::make_shared<dsp::DrumKit>();
    if (id.startsWith("vst3:")) return vst3::createInstrument(id);
    return nullptr;
}

/// Copies every parameter value that both sets share (by name).
void copyParams(const dsp::ParamSet& from, dsp::ParamSet& to) {
    for (const auto& p : from.params)
        if (dsp::Param* q = to.find(p->name)) q->set(p->get());
}

/// Rebuilds the playback buffer of an audio region from its source (time-stretch and transpose).
void applyFlex(Region& r, double projectBpm) {
    if (!r.source) return;
    if (!r.flexed()) {
        r.audio = r.source;
        r.offset = r.srcOffset;
        r.frames = r.srcFrames;
        r.flexBpm = 0;
        return;
    }
    const double ratio = (r.follow && r.sourceBpm > 0 && projectBpm > 0) ? r.sourceBpm / projectBpm : 1.0;
    auto d = std::make_shared<AudioData>();
    d->name = r.source->name;
    d->path = r.source->path;
    d->pcm = dsp::flex(r.source->pcm.data() + r.srcOffset * 2, size_t(r.srcFrames), ratio, r.semitones);
    computePeaks(*d);
    r.audio = d;
    r.offset = 0;
    r.frames = d->frames();
    r.flexBpm = projectBpm;
}

/// Pull-mode device. The engine renders 44.1 kHz stereo 16-bit; any other device format is converted here.
class EngineDevice : public QIODevice {
public:
    EngineDevice(Engine* e, const QAudioFormat& fmt, QObject* parent) : QIODevice(parent), m_e(e), m_fmt(fmt) {
        m_native = fmt.sampleRate() == kSampleRate && fmt.channelCount() == 2 && fmt.sampleFormat() == QAudioFormat::Int16;
        m_ratio = double(kSampleRate) / std::max(1, fmt.sampleRate());
        open(QIODevice::ReadOnly);
    }
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return (1 << 20) + QIODevice::bytesAvailable(); }
protected:
    qint64 readData(char* d, qint64 maxLen) override {
        const int bpf = std::max(1, m_fmt.bytesPerFrame());
        const int frames = int(maxLen / bpf);
        if (frames <= 0) return 0;
        if (m_native) {
            m_e->fillInt16(reinterpret_cast<qint16*>(d), frames);
            return qint64(frames) * bpf;
        }
        const double need = m_phase + frames * m_ratio + 4;   // linear resampling with state across calls
        while (double(m_src.size() / 2) < need) {
            qint16 tmp[1024];
            m_e->fillInt16(tmp, 512);
            for (qint16 s : tmp) m_src.push_back(s / 32768.f);
        }
        const int ch = std::max(1, m_fmt.channelCount());
        for (int i = 0; i < frames; ++i) {
            const double x = m_phase + i * m_ratio;
            const size_t i0 = size_t(x);
            const float fr = float(x - double(i0));
            const float l = m_src[2 * i0] * (1 - fr) + m_src[2 * i0 + 2] * fr;
            const float r = m_src[2 * i0 + 1] * (1 - fr) + m_src[2 * i0 + 3] * fr;
            writeFrame(d + size_t(i) * size_t(bpf), l, r, ch);
        }
        m_phase += frames * m_ratio;
        const size_t drop = size_t(m_phase);
        m_src.erase(m_src.begin(), m_src.begin() + std::ptrdiff_t(drop * 2));
        m_phase -= double(drop);
        return qint64(frames) * bpf;
    }
    qint64 writeData(const char*, qint64) override { return -1; }
private:
    void writeFrame(char* p, float l, float r, int ch) const {
        for (int c = 0; c < ch; ++c) {
            const float v = ch == 1 ? 0.5f * (l + r) : c == 0 ? l : c == 1 ? r : 0.f;
            switch (m_fmt.sampleFormat()) {
            case QAudioFormat::UInt8: reinterpret_cast<quint8*>(p)[c] = quint8(std::clamp(128.f + v * 127.f, 0.f, 255.f)); break;
            case QAudioFormat::Int16: reinterpret_cast<qint16*>(p)[c] = qint16(std::clamp(v, -1.f, 1.f) * 32767.f); break;
            case QAudioFormat::Int32: reinterpret_cast<qint32*>(p)[c] = qint32(double(std::clamp(v, -1.f, 1.f)) * 2147483647.0); break;
            case QAudioFormat::Float: reinterpret_cast<float*>(p)[c] = v; break;
            default: break;
            }
        }
    }
    Engine* m_e;
    QAudioFormat m_fmt;
    bool m_native = false;
    double m_ratio = 1, m_phase = 0;
    std::vector<float> m_src;
};
}  // namespace

// ------------------------------------------------------- audio output / input objects
// Both live on a dedicated audio thread so the UI can never starve them.
class AudioOutput : public QObject {
    Q_OBJECT
public:
    explicit AudioOutput(Engine* e) : m_e(e) {}
public slots:
    void configure(const QByteArray& id, int bufferFrames) {
        m_id = id;
        m_buf = std::clamp(bufferFrames, 128, 16384);
        start();
    }
    void start() {
        teardown();
        ++m_gen;
        m_device = QAudioDevice();
        for (const QAudioDevice& d : QMediaDevices::audioOutputs())
            if (!m_id.isEmpty() && d.id() == m_id) m_device = d;
        if (m_device.isNull()) m_device = QMediaDevices::defaultAudioOutput();
        if (m_device.isNull()) {
            emit failed(tr("No audio output device was found."));
            return;
        }
        m_fmts.clear();
        QAudioFormat ours;
        ours.setSampleRate(kSampleRate);
        ours.setChannelCount(2);
        ours.setSampleFormat(QAudioFormat::Int16);
        if (m_device.isFormatSupported(ours)) m_fmts.push_back(ours);
        m_fmts.push_back(m_device.preferredFormat());   // fallback: whatever the device prefers
        m_cand = 0;
        begin();
    }
    void stop() {
        ++m_gen;
        teardown();
    }
signals:
    void failed(const QString& reason);
    void status(const QString& text);
private:
    void begin() {
        teardown();
        if (m_cand >= int(m_fmts.size())) {
            emit failed(tr("The audio device accepted no data. Choose another output device in Preferences."));
            return;
        }
        const QAudioFormat f = m_fmts[size_t(m_cand)];
        const int gen = ++m_gen;
        m_dev = new EngineDevice(m_e, f, this);
        m_sink = new QAudioSink(m_device, f, this);
        m_sink->setBufferSize(m_buf * std::max(1, f.bytesPerFrame()));
        connect(m_sink, &QAudioSink::stateChanged, this, [this, gen](QAudio::State s) {
            if (gen == m_gen && s == QAudio::StoppedState && m_sink && m_sink->error() != QAudio::NoError) fallback(gen);
        });
        m_sink->start(m_dev);
        if (m_sink->error() != QAudio::NoError) { fallback(gen); return; }
        static const char* names[] = {"unknown", "8-bit", "16-bit", "32-bit", "float"};
        emit status(QString("%1 — %2 Hz, %3, %4 ms")
                        .arg(m_device.description())
                        .arg(f.sampleRate())
                        .arg(names[std::clamp(int(f.sampleFormat()), 0, 4)])
                        .arg(m_buf * 1000 / std::max(1, f.sampleRate())));
        // Watchdog: if the device never consumes audio, try the next format.
        QTimer::singleShot(1500, this, [this, gen] {
            if (gen == m_gen && m_sink && m_sink->processedUSecs() == 0) fallback(gen);
        });
    }
    void fallback(int gen) {
        if (gen != m_gen) return;
        ++m_cand;
        QMetaObject::invokeMethod(this, [this] { begin(); }, Qt::QueuedConnection);
    }
    void teardown() {
        if (m_sink) { m_sink->stop(); delete m_sink; m_sink = nullptr; }
        delete m_dev;
        m_dev = nullptr;
    }
    Engine* m_e;
    QByteArray m_id;
    int m_buf = 2048, m_cand = 0, m_gen = 0;
    QAudioDevice m_device;
    std::vector<QAudioFormat> m_fmts;
    QAudioSink* m_sink = nullptr;
    QIODevice* m_dev = nullptr;
};

class AudioInput : public QObject {
    Q_OBJECT
public:
    explicit AudioInput(Engine* e) : m_e(e) {}
    void setDevice(const QByteArray& id) { m_id = id; }   // used by the next start()
public slots:
    void start() {
        stop();
        QAudioDevice dev;
        for (const QAudioDevice& d : QMediaDevices::audioInputs())
            if (!m_id.isEmpty() && d.id() == m_id) dev = d;
        if (dev.isNull()) dev = QMediaDevices::defaultAudioInput();   // chosen device unplugged: use the default
        if (dev.isNull()) {
            emit failed(tr("No audio input device was found."));
            return;
        }
        m_fmt = dev.preferredFormat();
        m_rs = Resampler();
        m_src = new QAudioSource(dev, m_fmt, this);
        m_dev = m_src->start();
        if (!m_dev) {
            emit failed(tr("Could not start recording."));
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
    QByteArray m_id;
    QAudioSource* m_src = nullptr;
    QIODevice* m_dev = nullptr;
    QAudioFormat m_fmt;
    Resampler m_rs;
};

// ------------------------------------------------------------------ construction
Engine::Engine(QObject* parent) : QObject(parent) {
    m_tempo = std::make_shared<const dsp::TempoMap>();
    for (int p = 0; p < dsp::kDrumPieceCount; ++p) m_drumMax = std::max<qint64>(m_drumMax, qint64(dsp::DrumKit::sound(p).size()));
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
    connect(m_out, &AudioOutput::failed, this, [this](const QString& s) {
        m_audioStatus = tr("Audio error: %1").arg(s);
        emit audioStatusChanged(m_audioStatus);
        emit message(m_audioStatus);
    });
    connect(m_out, &AudioOutput::status, this, [this](const QString& s) {
        m_audioStatus = s;
        emit audioStatusChanged(s);
    });
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

void Engine::setAudioOutput(const QByteArray& id, int bufferFrames) {
    QMetaObject::invokeMethod(m_out, [this, id, bufferFrames] { m_out->configure(id, bufferFrames); }, Qt::QueuedConnection);
}

void Engine::setAudioInput(const QByteArray& id, int offsetMs) {
    m_recOffsetMs = std::clamp(offsetMs, -500, 500);
    QMetaObject::invokeMethod(m_in, [in = m_in, id] { in->setDevice(id); }, Qt::QueuedConnection);
}

void Engine::playTestTone() {
    m_toneRemain = kToneFrames;
    m_idle = qint64(kSampleRate);
}

void Engine::audition(const QString& path) {
    decodeAsync(path, [this](std::shared_ptr<AudioData> data, QString err) {
        if (!data) { emit message(err); return; }
        QMutexLocker l(&m_mutex);
        m_audition = data;
        m_auditionPos = 0;
    });
}

void Engine::stopAudition() {
    QMutexLocker l(&m_mutex);
    m_audition.reset();
}

// ------------------------------------------------------------------ model
Track Engine::makeTrack(TrackType type, const QString& name) {
    Track t;
    t.id = m_nextId++;
    t.type = type;
    t.name = name;
    switch (type) {
    case TrackType::Audio: t.color = QColor(60, 130, 235); break;
    case TrackType::Instrument:
        t.color = QColor(100, 190, 100);
        t.instrumentId = "synth";
        t.instrument = makeInstrument("synth");
        break;
    case TrackType::Drums: t.color = QColor(240, 150, 50); break;
    case TrackType::Drummer:
        t.color = QColor(235, 200, 70);
        t.instrumentId = "drumkit";
        t.instrument = makeInstrument("drumkit");
        break;
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
    if (r.midi) return r.lengthBeats;
    const auto tm = tempoMap();
    return tm->frameToBeat(tm->beatToFrame(r.start) + double(r.frames)) - r.start;
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
        QMutexLocker l(&m_tempoMutex);
        m_tempo = std::make_shared<const dsp::TempoMap>();
    }
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
        m_audition.reset();
    }
    m_loopOn = false;
    m_loopA = 0.0;
    m_loopB = 16.0;
    m_path.clear();
    m_flush |= 2;
    m_dirty = false;
    emit tracksChanged();
    emit tempoChanged();
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
            const char* base = type == TrackType::Audio ? "Audio" : type == TrackType::Instrument ? "Software Instrument"
                             : type == TrackType::Drums ? "Drum Machine" : "Drummer";
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
    QMutexLocker l(&m_mutex);
    for (size_t k = 0; k < m_tracks.size(); ++k) m_tracks[k].armed = on && int(k) == i;   // one armed track at a time
}

void Engine::addInsert(int track, const QString& effectId) {
    auto fx = makeEffect(effectId);
    if (!fx) { emit message(tr("Could not create the effect.")); return; }
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

void Engine::setInstrument(int track, const QString& instrumentId) {
    auto inst = makeInstrument(instrumentId);
    if (!inst) { emit message(tr("Could not load the instrument.")); return; }
    checkpoint();
    {
        QMutexLocker l(&m_mutex);
        if (track < 0 || track >= int(m_tracks.size()) || !m_tracks[size_t(track)].instrument) return;
        m_tracks[size_t(track)].instrument = inst;
        m_tracks[size_t(track)].instrumentId = instrumentId;
        m_tracks[size_t(track)].preset = 0;
    }
    changed(true);
}

void Engine::setInstrumentPreset(int track, int preset) {
    QMutexLocker l(&m_mutex);
    if (track < 0 || track >= int(m_tracks.size()) || !m_tracks[size_t(track)].instrument) return;
    m_tracks[size_t(track)].instrument->setPreset(preset);
    m_tracks[size_t(track)].preset = preset;
    m_dirty = true;
}

void Engine::regenDrummer(Region& r, const dsp::TempoMap& tm) const {
    const dsp::MeterPoint mp = tm.meterAtBeat(r.start);
    r.notes.clear();
    for (const dsp::DrumHit& h : dsp::generateDrummer(r.drum, mp.num, mp.den)) r.notes.push_back({h.pitch, h.beat, h.length, h.velocity});
    r.lengthBeats = r.drum.bars * mp.num * 4.0 / mp.den;
}

int Engine::addMidiRegion(int track, double startBeat, double lengthBeats) {
    checkpoint();
    int idx = -1;
    {
        QMutexLocker l(&m_mutex);
        if (track < 0 || track >= int(m_tracks.size())) return -1;
        Track& t = m_tracks[size_t(track)];
        if (t.type != TrackType::Instrument && t.type != TrackType::Drummer) return -1;
        Region r;
        r.midi = true;
        r.name = "MIDI Region";
        r.start = std::max(0.0, startBeat);
        r.lengthBeats = std::max(0.25, lengthBeats);
        t.regions.push_back(r);
        idx = int(t.regions.size()) - 1;
    }
    changed(false);
    return idx;
}

int Engine::addDrummerRegion(int track, double startBeat, int bars) {
    checkpoint();
    const auto tm = tempoMap();
    int idx = -1;
    {
        QMutexLocker l(&m_mutex);
        if (track < 0 || track >= int(m_tracks.size()) || m_tracks[size_t(track)].type != TrackType::Drummer) return -1;
        Region r;
        r.midi = true;
        r.drummer = true;
        r.name = "Drummer";
        r.start = std::max(0.0, startBeat);
        r.drum.bars = std::clamp(bars, 1, 64);
        regenDrummer(r, *tm);
        m_tracks[size_t(track)].regions.push_back(r);
        idx = int(m_tracks[size_t(track)].regions.size()) - 1;
    }
    changed(false);
    return idx;
}

void Engine::setDrummerParams(int track, int region, const dsp::DrummerParams& p) {
    const auto tm = tempoMap();
    {
        QMutexLocker l(&m_mutex);
        if (track < 0 || track >= int(m_tracks.size()) || region < 0 || region >= int(m_tracks[size_t(track)].regions.size())) return;
        Region& r = m_tracks[size_t(track)].regions[size_t(region)];
        if (!r.drummer) return;
        r.drum = p;
        regenDrummer(r, *tm);
    }
    changed(false);
}

#define REGION_OP(...) { QMutexLocker l(&m_mutex); \
    if (t < 0 || t >= int(m_tracks.size()) || r < 0 || r >= int(m_tracks[size_t(t)].regions.size())) return; \
    auto& regs = m_tracks[size_t(t)].regions; Region& rg = regs[size_t(r)]; __VA_ARGS__ }

void Engine::moveRegion(int t, int r, double startBeat) {
    REGION_OP(rg.start = std::max(0.0, startBeat);)
    changed(false);
}

void Engine::resizeRegion(int t, int r, double lengthBeats) {
    const auto tm = tempoMap();
    REGION_OP(
        if (rg.midi) rg.lengthBeats = std::max(0.25, lengthBeats);
        else if (rg.audio && !rg.flexed()) {   // trimming the end of an audio region
            const double sf = tm->beatToFrame(rg.start);
            const qint64 maxFrames = rg.audio->frames() - rg.offset;
            rg.frames = std::clamp<qint64>(qint64(std::llround(tm->beatToFrame(rg.start + lengthBeats) - sf)), 1, maxFrames);
            rg.srcFrames = rg.frames;
        } else if (rg.audio) {   // stretched audio: shorten the played buffer, keep the source segment proportional
            const double sf = tm->beatToFrame(rg.start);
            const qint64 maxFrames = rg.audio->frames() - rg.offset;
            const qint64 nf = std::clamp<qint64>(qint64(std::llround(tm->beatToFrame(rg.start + lengthBeats) - sf)), 1, maxFrames);
            rg.srcFrames = std::max<qint64>(1, qint64(double(rg.srcFrames) * double(nf) / double(std::max<qint64>(1, rg.frames))));
            rg.frames = nf;
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
    const auto tm = tempoMap();
    Region a, b;
    {
        QMutexLocker l(&m_mutex);
        if (t < 0 || t >= int(m_tracks.size()) || r < 0 || r >= int(m_tracks[size_t(t)].regions.size())) return;
        a = m_tracks[size_t(t)].regions[size_t(r)];
    }
    const double len = regionBeats(a), rel = atBeat - a.start;
    if (rel <= 0.01 || rel >= len - 0.01) return;
    const Region orig = a;
    b = a;
    b.start = atBeat;
    if (orig.midi) {
        a.lengthBeats = rel;
        b.lengthBeats = len - rel;
        a.drummer = b.drummer = false;   // split regions are no longer regenerated
        a.notes.clear();
        b.notes.clear();
        for (const Note& n : orig.notes) {
            if (n.start >= rel) { Note m = n; m.start -= rel; b.notes.push_back(m); }
            else a.notes.push_back(n);
        }
    } else {
        const qint64 rf = qint64(std::llround(tm->beatToFrame(atBeat) - tm->beatToFrame(orig.start)));
        if (rf <= 0 || rf >= orig.frames) return;
        if (orig.flexed()) {   // split the source proportionally and stretch both halves again
            const qint64 sf = orig.srcOffset + qint64(std::llround(double(rf) / double(orig.frames) * double(orig.srcFrames)));
            a.srcFrames = sf - orig.srcOffset;
            b.srcOffset = sf;
            b.srcFrames = orig.srcOffset + orig.srcFrames - sf;
            applyFlex(a, tm->bpmAt(a.start));
            applyFlex(b, tm->bpmAt(b.start));
        } else {
            a.frames = rf;
            a.srcFrames = rf;
            b.offset += rf;
            b.frames -= rf;
            b.srcOffset += rf;
            b.srcFrames -= rf;
        }
        a.fadeOut = 0;   // fades stay on the outer edges of the two halves
        b.fadeIn = 0;
    }
    {
        QMutexLocker l(&m_mutex);
        if (t >= int(m_tracks.size()) || r >= int(m_tracks[size_t(t)].regions.size())) return;
        auto& regs = m_tracks[size_t(t)].regions;
        regs[size_t(r)] = a;
        regs.insert(regs.begin() + r + 1, b);
    }
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

bool Engine::copyRegion(int t, int r) {
    bool loading = false;
    {
        QMutexLocker l(&m_mutex);
        if (t < 0 || t >= int(m_tracks.size()) || r < 0 || r >= int(m_tracks[size_t(t)].regions.size())) return false;
        const Region& rg = m_tracks[size_t(t)].regions[size_t(r)];
        if (!rg.midi && !rg.audio) loading = true;
        else { m_clip = rg; m_hasClip = true; }
    }
    if (loading) emit message(tr("This audio is still loading."));
    return !loading;
}

int Engine::pasteRegion(int track, double beat) {
    Region c;
    TrackType type = TrackType::Audio;
    {
        QMutexLocker l(&m_mutex);
        if (!m_hasClip || track < 0 || track >= int(m_tracks.size())) return -1;
        c = m_clip;
        type = m_tracks[size_t(track)].type;
    }
    const bool fits = c.midi ? (c.drummer ? type == TrackType::Drummer : (type == TrackType::Instrument || type == TrackType::Drummer))
                             : type == TrackType::Audio;
    if (!fits) {
        emit message(c.midi ? tr("MIDI regions can be pasted on Software Instrument and Drummer tracks.")
                            : tr("Audio regions can be pasted on Audio tracks."));
        return -1;
    }
    checkpoint();
    c.start = std::max(0.0, beat);
    if (!c.midi && c.source && c.follow) applyFlex(c, tempoMap()->bpmAt(c.start));   // stretch for the tempo at the new place
    int idx = -1;
    {
        QMutexLocker l(&m_mutex);
        if (track >= int(m_tracks.size())) return -1;
        auto& regs = m_tracks[size_t(track)].regions;
        regs.push_back(std::move(c));
        idx = int(regs.size()) - 1;
    }
    changed(false);
    return idx;
}

int Engine::duplicateTrack(int i) {
    Track src;
    {
        QMutexLocker l(&m_mutex);
        if (i < 0 || i >= int(m_tracks.size())) return -1;
        src = m_tracks[size_t(i)];
    }
    checkpoint();
    Track t = src;   // regions share their audio data; everything else is copied below
    t.name = tr("%1 copy").arg(src.name);
    t.armed = false;
    t.peakL = t.peakR = 0;
    if (src.instrument) {   // every track needs its own instrument and effect instances
        t.instrument = makeInstrument(src.instrumentId);
        if (t.instrument) {
            copyParams(*src.instrument, *t.instrument);
            if (src.instrumentId.startsWith("vst3:")) vst3::loadState(*t.instrument, vst3::saveState(*src.instrument));
        } else {
            emit message(tr("Instrument plug-in not available: %1").arg(src.instrumentId));
        }
    }
    t.inserts.clear();
    for (const auto& fx : src.inserts) {
        auto c = makeEffect(QString::fromUtf8(fx->id()));
        if (!c) continue;
        copyParams(*fx, *c);
        c->bypass = fx->bypass.load();
        if (QString::fromUtf8(fx->id()).startsWith("vst3:")) vst3::loadState(*c, vst3::saveState(*fx));
        t.inserts.push_back(c);
    }
    int idx;
    {
        QMutexLocker l(&m_mutex);
        t.id = m_nextId++;
        idx = std::min(i + 1, int(m_tracks.size()));
        m_tracks.insert(m_tracks.begin() + idx, std::move(t));
    }
    changed(true);
    return idx;
}

void Engine::setRegionAudio(int t, int r, float gainDb, qint64 fadeInFrames, qint64 fadeOutFrames) {
    checkpoint();
    REGION_OP(
        if (rg.midi) return;
        rg.gainDb = std::clamp(gainDb, -60.f, 24.f);
        rg.fadeIn = std::max<qint64>(0, fadeInFrames);
        rg.fadeOut = std::max<qint64>(0, fadeOutFrames);)
    changed(false);
}

void Engine::reverseRegion(int t, int r) {
    Region c;
    {
        QMutexLocker l(&m_mutex);
        if (t < 0 || t >= int(m_tracks.size()) || r < 0 || r >= int(m_tracks[size_t(t)].regions.size())) return;
        c = m_tracks[size_t(t)].regions[size_t(r)];
    }
    if (c.midi || !c.source || c.srcFrames <= 0) return;
    checkpoint();
    auto d = std::make_shared<AudioData>();   // no path yet: it is written next to the project when saved
    d->name = c.source->name;
    d->pcm = dsp::reverseFrames(c.source->pcm.data() + c.srcOffset * 2, size_t(c.srcFrames));   // reverse the source so tempo / pitch settings still apply
    computePeaks(*d);
    c.source = c.audio = d;
    c.srcOffset = c.offset = 0;
    c.srcFrames = c.frames = d->frames();
    c.pendingPath.clear();
    static const QString kTag = QStringLiteral(" (reversed)");
    c.name = c.name.endsWith(kTag) ? c.name.left(c.name.size() - kTag.size()) : c.name + kTag;
    if (c.flexed()) applyFlex(c, tempoMap()->bpmAt(c.start));   // slow part runs outside the lock
    {
        QMutexLocker l(&m_mutex);
        if (t >= int(m_tracks.size()) || r >= int(m_tracks[size_t(t)].regions.size())) return;
        m_tracks[size_t(t)].regions[size_t(r)] = std::move(c);
    }
    changed(false);
}

void Engine::normalizeRegion(int t, int r, float targetDb) {
    checkpoint();
    REGION_OP(
        if (rg.midi || !rg.audio || rg.frames <= 0) return;
        const float pk = dsp::peakOf(rg.audio->pcm.data() + rg.offset * 2, size_t(rg.frames));
        rg.gainDb = std::clamp(dsp::linearToDb(dsp::normalizeGain(pk, targetDb)), -60.f, 24.f);)
    changed(false);
}

void Engine::setRegionNotes(int t, int r, const std::vector<Note>& notes) {
    REGION_OP(if (!rg.midi) return; rg.notes = notes; rg.drummer = false;)   // hand-edited notes detach from the generator
    changed(false);
}

void Engine::setRegionFlex(int t, int r, bool follow, double sourceBpm, double semitones) {
    checkpoint();
    const auto tm = tempoMap();
    Region copy;
    {
        QMutexLocker l(&m_mutex);
        if (t < 0 || t >= int(m_tracks.size()) || r < 0 || r >= int(m_tracks[size_t(t)].regions.size())) return;
        copy = m_tracks[size_t(t)].regions[size_t(r)];
    }
    if (copy.midi || !copy.source) return;
    copy.follow = follow;
    copy.sourceBpm = sourceBpm;
    copy.semitones = semitones;
    applyFlex(copy, tm->bpmAt(copy.start));   // the slow part runs outside the lock
    {
        QMutexLocker l(&m_mutex);
        if (t >= int(m_tracks.size()) || r >= int(m_tracks[size_t(t)].regions.size())) return;
        Region& dst = m_tracks[size_t(t)].regions[size_t(r)];
        dst.follow = copy.follow; dst.sourceBpm = copy.sourceBpm; dst.semitones = copy.semitones;
        dst.audio = copy.audio; dst.offset = copy.offset; dst.frames = copy.frames; dst.flexBpm = copy.flexBpm;
    }
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

// ------------------------------------------------------------------ tempo and meter
std::shared_ptr<const dsp::TempoMap> Engine::tempoMap() const {
    QMutexLocker l(&m_tempoMutex);
    return m_tempo;
}

void Engine::setTempoMap(std::shared_ptr<const dsp::TempoMap> map, bool commit) {
    const double beat = positionBeats();   // keep the playhead at the same musical position
    { QMutexLocker l(&m_tempoMutex); m_tempo = map; }
    { QMutexLocker l(&m_mutex); m_pos = qint64(std::llround(map->beatToFrame(beat))); }
    m_dirty = true;
    emit tempoChanged();
    if (commit) commitTempo();
}

void Engine::setTempoPoints(const std::vector<dsp::TempoPoint>& pts, bool commit) {
    auto tm = std::make_shared<dsp::TempoMap>(*tempoMap());
    tm->setTempo(pts);
    setTempoMap(tm, commit);
}

void Engine::setMeterPoints(const std::vector<dsp::MeterPoint>& pts) {
    checkpoint();
    auto tm = std::make_shared<dsp::TempoMap>(*tempoMap());
    tm->setMeter(pts);
    setTempoMap(tm, false);
    changed(false);
}

double Engine::bpm() const { return tempoMap()->bpmAt(positionBeats()); }

void Engine::setBpm(double b) {
    checkpoint();
    auto pts = tempoMap()->tempoPoints();
    const double beat = positionBeats();
    size_t idx = 0;
    for (size_t k = 0; k < pts.size(); ++k)
        if (pts[k].beat <= beat + 1e-6) idx = k;
    pts[idx].bpm = b;
    setTempoPoints(pts, true);
}

void Engine::commitTempo() {
    struct Job { int trackId; size_t ri; Region copy; double bpm; };
    std::vector<Job> jobs;
    const auto tm = tempoMap();
    {
        QMutexLocker l(&m_mutex);
        for (const Track& t : m_tracks)
            for (size_t i = 0; i < t.regions.size(); ++i) {
                const Region& r = t.regions[i];
                const double bpm = tm->bpmAt(r.start);
                if (!r.midi && r.source && r.follow && std::fabs(r.flexBpm - bpm) > 1e-6) jobs.push_back({t.id, i, r, bpm});
            }
    }
    for (Job& j : jobs) applyFlex(j.copy, j.bpm);   // stretching runs outside the lock
    {
        QMutexLocker l(&m_mutex);
        for (const Job& j : jobs)
            for (Track& t : m_tracks) {
                if (t.id != j.trackId || j.ri >= t.regions.size() || t.regions[j.ri].source != j.copy.source) continue;
                Region& dst = t.regions[j.ri];
                dst.audio = j.copy.audio; dst.offset = j.copy.offset; dst.frames = j.copy.frames; dst.flexBpm = j.copy.flexBpm;
            }
    }
    emit regionsChanged();
}

// ------------------------------------------------------------------ undo / redo
void Engine::pushUndo() {
    UndoState s{{}, tempoMap()};
    QMutexLocker l(&m_mutex);
    s.tracks = m_tracks;
    m_undo.push_back(std::move(s));
    if (m_undo.size() > 60) m_undo.erase(m_undo.begin());
    m_redo.clear();
    m_dirty = true;
}

void Engine::checkpoint() { pushUndo(); }

void Engine::restore(UndoState s) {
    {
        QMutexLocker l(&m_tempoMutex);
        m_tempo = s.tempo;
    }
    {
        QMutexLocker l(&m_mutex);
        m_tracks = std::move(s.tracks);
    }
    emit tempoChanged();
    changed(true);
}

void Engine::undo() {
    UndoState cur{{}, tempoMap()};
    UndoState prev;
    {
        QMutexLocker l(&m_mutex);
        if (m_undo.empty()) return;
        cur.tracks = m_tracks;
        prev = std::move(m_undo.back());
        m_undo.pop_back();
        m_redo.push_back(std::move(cur));
    }
    restore(std::move(prev));
}

void Engine::redo() {
    UndoState cur{{}, tempoMap()};
    UndoState next;
    {
        QMutexLocker l(&m_mutex);
        if (m_redo.empty()) return;
        cur.tracks = m_tracks;
        next = std::move(m_redo.back());
        m_redo.pop_back();
        m_undo.push_back(std::move(cur));
    }
    restore(std::move(next));
}

// ------------------------------------------------------------------ transport
void Engine::startPlayback() {
    m_expected = -1;   // forces a discontinuity: instrument voices are re-synchronised
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
    const double f = beatToFrame(std::max(0.0, beat));
    QMutexLocker l(&m_mutex);   // keeps the audio thread from overwriting the new position
    m_pos = qint64(std::llround(f));
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
        if (!t.instrument) return;
        t.instrument->noteOn(pitch, velocity);
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
        if (t.instrument) t.instrument->noteOff(pitch);
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
        if (m_countIn) {   // one bar of clicks in the meter at the playhead
            const auto tm = tempoMap();
            const double beat = m_recStartBeat;
            const dsp::MeterPoint mp = tm->meterAtBeat(beat);
            const qint64 click = qint64(std::llround(kSampleRate * 60.0 / tm->bpmAt(beat) * (4.0 / mp.den)));
            m_countClick = click;
            m_countPerBar = mp.num;
            m_countTotal = click * mp.num;
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
    if (audio) dsp::applyRecordingOffset(take, long(std::lround(double(m_recOffsetMs.load()) * kSampleRate / 1000.0)));
    for (const auto& [pitch, p] : m_recPending)
        m_recNotes.push_back(Note{pitch, p.start, std::max(0.05, endBeat - p.start), p.vel});
    m_recPending.clear();
    std::vector<Note> notes;
    notes.swap(m_recNotes);
    if (audio ? take.size() < size_t(kSampleRate / 10) : notes.empty()) return;   // ignore accidental taps

    pushUndo();
    const auto tm = tempoMap();
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
            r.source = r.audio = data;
            r.frames = r.srcFrames = data->frames();
        } else {
            r.midi = true;
            r.name = "Recorded MIDI";
            double maxEnd = m_recStartBeat;
            for (const Note& n : notes) maxEnd = std::max(maxEnd, n.start + n.length);
            double endBar = tm->nextBarLine(maxEnd);
            if (endBar <= m_recStartBeat + 1e-6) endBar = tm->barStart(tm->barAt(m_recStartBeat) + 1);
            r.lengthBeats = endBar - m_recStartBeat;
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
        if (t.instrument) t.instrument->reset();
    }
    for (auto& a : m_aux) a->reset();
    m_limiter->reset();
}

void Engine::addClicks(qint64 tf, int frames, float* out) {
    const dsp::TempoMap& tm = *m_tm;
    tm.forEachClick(tm.frameToBeat(double(std::max<qint64>(0, tf - kClickLen))), tm.frameToBeat(double(tf + frames)),
                    [&](double beat, bool accent) {
        const qint64 st = qint64(std::llround(tm.beatToFrame(beat)));
        const qint64 a = std::max(tf, st), b = std::min<qint64>(tf + frames, st + kClickLen);
        const double freq = accent ? 1600.0 : 1000.0;
        for (qint64 f = a; f < b; ++f) {
            const double n = double(f - st);
            const float v = float(std::sin(2 * kPi * freq * n / kSampleRate) * std::exp(-n / 250.0) * 0.35);
            out[(f - tf) * 2] += v;
            out[(f - tf) * 2 + 1] += v;
        }
    });
}

// Mixes [pos, pos + frames) of the whole project into 'out'. m_mutex must be held and m_tm set.
// 'running' is false while the transport is stopped: only live notes and effect tails are rendered.
void Engine::renderBlock(qint64 pos, int frames, float* out, bool running) {
    const dsp::TempoMap& tm = *m_tm;
    const size_t n2 = size_t(frames) * 2;
    std::fill(out, out + n2, 0.f);
    for (auto& a : m_auxBuf) std::fill(a.begin(), a.begin() + std::ptrdiff_t(n2), 0.f);

    const double beat0 = tm.frameToBeat(double(pos)), beat1 = tm.frameToBeat(double(pos + frames));
    auto bf = [&](double beat) { return qint64(std::llround(tm.beatToFrame(beat))); };
    const bool jump = running && m_expected.load() != pos;
    const bool anySolo = std::any_of(m_tracks.begin(), m_tracks.end(), [](const Track& t) { return t.solo; });
    float* tb = m_trackBuf.data();

    for (Track& t : m_tracks) {
        if (t.mute || (anySolo && !t.solo)) {
            t.peakL = t.peakR = 0;
            if (t.instrument) t.instrument->allNotesOff();   // never leave voices hanging on a silent track
            continue;
        }
        std::fill(tb, tb + n2, 0.f);

        if (t.type == TrackType::Audio && running) {
            for (const Region& rg : t.regions) {
                if (rg.midi || !rg.audio) continue;
                const qint64 st = bf(rg.start);
                const qint64 a = std::max(pos, st), b = std::min<qint64>(pos + frames, st + rg.frames);
                if (a >= b) continue;
                const float* s = rg.audio->pcm.data() + (rg.offset + a - st) * 2;
                const float lin = rg.gainDb == 0.f ? 1.f : dsp::dbToLinear(rg.gainDb);
                const bool fades = rg.fadeIn > 0 || rg.fadeOut > 0;
                for (qint64 f = a; f < b; ++f, s += 2) {
                    const float g = fades ? lin * dsp::fadeGain(f - st, rg.frames, rg.fadeIn, rg.fadeOut) : lin;
                    tb[(f - pos) * 2] += s[0] * g;
                    tb[(f - pos) * 2 + 1] += s[1] * g;
                }
            }
        } else if (t.type == TrackType::Drums && running) {
            const qint64 first = std::max<qint64>(0, qint64(std::floor(tm.frameToBeat(double(std::max<qint64>(0, pos - m_drumMax))) * 4.0)) - 1);
            const qint64 last = qint64(std::floor(beat1 * 4.0));   // 16th-note steps
            for (qint64 k = first; k <= last; ++k) {
                const size_t s = size_t(k % kSteps);
                const qint64 st = bf(double(k) * 0.25);
                for (int r = 0; r < kDrumRows; ++r) {
                    if (!t.steps[size_t(r)][s]) continue;
                    const auto& snd = dsp::DrumKit::sound(kRowPiece[r]);
                    const qint64 a = std::max(pos, st), b = std::min<qint64>(pos + frames, st + qint64(snd.size()));
                    for (qint64 f = a; f < b; ++f) {
                        const float v = snd[size_t(f - st)];
                        tb[(f - pos) * 2] += v;
                        tb[(f - pos) * 2 + 1] += v;
                    }
                }
            }
        } else if ((t.type == TrackType::Instrument || t.type == TrackType::Drummer) && t.instrument) {
            if (!running) {
                t.instrument->render(tb, frames);   // live notes only
            } else {
                m_events.clear();
                if (jump) t.instrument->allNotesOff();
                for (const Region& rg : t.regions) {
                    if (!rg.midi) continue;
                    const double regEnd = rg.start + rg.lengthBeats;
                    for (const Note& n : rg.notes) {
                        if (n.start >= rg.lengthBeats) continue;
                        const qint64 s = bf(rg.start + n.start);
                        qint64 e = bf(std::min(rg.start + n.start + n.length, regEnd));
                        if (e <= s) e = s + 1;
                        if (s >= pos && s < pos + frames) m_events.push_back({int(s - pos), true, n.pitch, n.velocity});
                        if (e >= pos && e < pos + frames) m_events.push_back({int(e - pos), false, n.pitch, 0});
                        if (jump && s < pos && e > pos) m_events.push_back({0, true, n.pitch, n.velocity});   // note already sounding
                    }
                }
                std::stable_sort(m_events.begin(), m_events.end(), [](const Ev& a, const Ev& b) {
                    return a.off != b.off ? a.off < b.off : a.on < b.on;   // releases before attacks
                });
                int cur = 0;
                for (const Ev& ev : m_events) {
                    if (ev.off > cur) { t.instrument->render(tb + cur * 2, ev.off - cur); cur = ev.off; }
                    if (ev.on) t.instrument->noteOn(ev.pitch, ev.vel);
                    else t.instrument->noteOff(ev.pitch);
                }
                if (cur < frames) t.instrument->render(tb + cur * 2, frames - cur);
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
    const auto tempo = tempoMap();   // keeps the map alive for this call
    QMutexLocker lk(&m_mutex);
    m_tm = tempo.get();
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
            if (t.instrument) t.instrument->allNotesOff();

    const bool running = m_playing;
    if (!running && m_idle <= 0 && m_toneRemain <= 0 && !m_audition) {   // idle: no work and no CPU use
        std::fill(out, out + need, qint16(0));
        m_masterL = m_masterR = 0;
        for (Track& t : m_tracks) t.peakL = t.peakR = 0;
        return;
    }

    const qint64 la = qint64(std::llround(tempo->beatToFrame(m_loopA))), lb = qint64(std::llround(tempo->beatToFrame(m_loopB)));
    const bool looping = m_loopOn && lb > la;
    qint64 pos = m_pos;
    int done = 0;
    while (done < frames) {
        int n = frames - done;
        float* dst = m_scratch.data() + size_t(done) * 2;
        if (running && m_countRemain > 0) {   // count-in: clicks only, the playhead stays put
            n = int(std::min<qint64>(n, m_countRemain));
            std::fill(dst, dst + size_t(n) * 2, 0.f);
            const qint64 click = std::max<qint64>(1, m_countClick), el = m_countTotal - m_countRemain;
            for (int f = 0; f < n; ++f) {
                const qint64 t = el + f, k = t / click, off = t - k * click;
                if (off >= kClickLen) continue;
                const float v = float(std::sin(2 * kPi * (k % m_countPerBar == 0 ? 1600.0 : 1000.0) * double(off) / kSampleRate) *
                                      std::exp(-double(off) / 250.0) * 0.35);
                dst[2 * f] += v;
                dst[2 * f + 1] += v;
            }
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

    if (m_toneRemain > 0) {   // device test tone
        for (int i = 0; i < frames && m_toneRemain > 0; ++i, --m_toneRemain) {
            const qint64 n = kToneFrames - m_toneRemain;
            const float env = float(std::min<qint64>(std::min<qint64>(n, m_toneRemain), 2000)) / 2000.f;
            const float v = 0.25f * env * float(std::sin(2 * kPi * 440.0 * double(n) / kSampleRate));
            m_scratch[size_t(2 * i)] += v;
            m_scratch[size_t(2 * i + 1)] += v;
        }
    }
    if (m_audition) {   // loop browser preview
        for (int i = 0; i < frames; ++i) {
            if (m_auditionPos >= m_audition->frames()) { m_audition.reset(); break; }
            m_scratch[size_t(2 * i)] += m_audition->pcm[size_t(2 * m_auditionPos)] * 0.8f;
            m_scratch[size_t(2 * i + 1)] += m_audition->pcm[size_t(2 * m_auditionPos + 1)] * 0.8f;
            ++m_auditionPos;
        }
    }
    for (size_t i = 0; i < need; ++i) out[i] = qint16(std::clamp(m_scratch[i], -1.f, 1.f) * 32767.f);
}

#include "Engine.moc"
