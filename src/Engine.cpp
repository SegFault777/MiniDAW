// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Engine.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QMetaObject>
#include <QTimer>
#include <algorithm>
#include <chrono>
#include <thread>
#include "NativePlugins.h"
#include "platform/Decode.h"
#include "vst3/Vst3Host.h"

// The Qt side of the engine: hooks for the core, the system sound streams of the platform layer, decoding on
// worker threads and Qt signals. All music logic lives in core::Session.

Engine::Engine(QObject* parent) : QObject(parent) {
    core::Session::Hooks h;
    h.changed = [this](bool structural) {
        if (structural) emit tracksChanged();
        else emit regionsChanged();
    };
    h.tempoChanged = [this] { emit tempoChanged(); };
    h.loopChanged = [this] { emit loopChanged(); };
    h.message = [this](const std::string& s) { emit message(QString::fromStdString(s)); };
    h.translate = [](const char* key) { return QCoreApplication::translate("Engine", key).toStdString(); };
    h.makeEffect = [](const std::string& id) -> std::shared_ptr<dsp::Effect> {
        const QString q = QString::fromStdString(id);
        return q.startsWith("vst3:") ? vst3::createEffect(q) : nativeplug::create(q);
    };
    h.makeInstrument = [](const std::string& id) { return vst3::createInstrument(QString::fromStdString(id)); };
    h.saveState = [](dsp::ParamSet& p) { return vst3::saveState(p).toBase64().toStdString(); };
    h.loadState = [](dsp::ParamSet& p, const std::string& s) { vst3::loadState(p, QByteArray::fromBase64(QByteArray::fromStdString(s))); };
    h.decode = [this](const std::string& path, core::Session::DecodeDone done) {
        decodeAsync(QString::fromStdString(path), [done](std::shared_ptr<AudioData> d, QString err) { done(std::move(d), err.toStdString()); });
    };
    h.startInput = [this] { startInput(); };
    h.stopInput = [this] { m_in.reset(); };
    m_s.setHooks(std::move(h));

    // The output stream runs permanently so instruments can be played while the transport is stopped.
    restartOutput();
    m_poll = new QTimer(this);
    m_poll->setInterval(3000);
    connect(m_poll, &QTimer::timeout, this, &Engine::pollDevices);
    m_poll->start();
}

Engine::~Engine() {
    if (m_poll) m_poll->stop();
    m_s.stopRecording();
    m_in.reset();
    m_out.reset();   // stops the audio thread before the session goes away
    for (int i = 0; i < 400 && m_decoding > 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));   // decode workers
}

// ------------------------------------------------------------------ audio devices
void Engine::restartOutput() {
    m_out.reset();   // the old stream is stopped and joined first
    auto render = [this](std::int16_t* out, int frames) { m_s.fillInt16(out, frames); };
    std::string err;
    m_out = platform::openOutput(m_outId.toStdString(), m_bufferFrames, render, &err);
    if (!m_out && !m_outId.isEmpty()) m_out = platform::openOutput("", m_bufferFrames, render, &err);   // chosen device is gone: use the default
    QString defaultId;
    for (const platform::AudioDevice& d : platform::audioOutputs())
        if (d.isDefault) defaultId = QString::fromStdString(d.id);
    m_outDefaultAtOpen = defaultId;
    if (!m_out) {
        m_audioStatus = tr("Audio error: %1").arg(QString::fromStdString(err));
        emit audioStatusChanged(m_audioStatus);
        emit message(m_audioStatus);
        return;
    }
    m_audioStatus = QString::fromStdString(m_out->status());
    emit audioStatusChanged(m_audioStatus);
}

void Engine::pollDevices() {
    bool restart = !m_out || m_out->failed();   // lost device or an earlier failure: try again
    if (!restart && m_outId.isEmpty()) {         // following the system default: it may have changed
        QString defaultId;
        for (const platform::AudioDevice& d : platform::audioOutputs())
            if (d.isDefault) defaultId = QString::fromStdString(d.id);
        restart = defaultId != m_outDefaultAtOpen;
    }
    if (restart) restartOutput();
}

void Engine::setAudioOutput(const QByteArray& id, int bufferFrames) {
    m_outId = id;
    m_bufferFrames = std::clamp(bufferFrames, 128, 16384);
    restartOutput();
}

void Engine::setAudioInput(const QByteArray& id, int offsetMs) {
    m_inId = id;   // used by the next recording
    m_s.setRecordingOffsetMs(offsetMs);
}

void Engine::startInput() {
    m_in.reset();
    std::string err;
    auto capture = [this](const float* stereo, int frames) { m_s.pushRecorded(std::vector<float>(stereo, stereo + size_t(frames) * 2)); };
    m_in = platform::openInput(m_inId.toStdString(), capture, &err);
    if (!m_in && !m_inId.isEmpty()) m_in = platform::openInput("", capture, &err);   // chosen device unplugged: use the default
    if (!m_in) emit message(tr("Could not start recording.") + (err.empty() ? QString() : " " + QString::fromStdString(err)));
}

// ------------------------------------------------------------------ file decoding
// Decodes a file on a worker thread to 44.1 kHz stereo float. 'done' runs exactly once on the UI thread.
void Engine::decodeAsync(const QString& path, std::function<void(std::shared_ptr<AudioData>, QString)> done) {
    ++m_decoding;
    std::thread([this, path, done = std::move(done)]() mutable {
        auto data = std::make_shared<AudioData>();
        data->name = QFileInfo(path).completeBaseName().toStdString();
        data->path = path.toStdString();
        std::string err;
        const bool ok = platform::decodeFile(data->path, data->pcm, &err) && data->frames() > 0;
        if (ok) core::computePeaks(*data);
        QMetaObject::invokeMethod(this, [this, ok, data, err, done = std::move(done)]() mutable {
            if (ok) done(data, {});
            else if (err.empty()) done(nullptr, tr("Could not decode %1").arg(QString::fromStdString(data->name)));
            else done(nullptr, tr("Import failed: %1").arg(QString::fromStdString(err)));
        }, Qt::QueuedConnection);
        --m_decoding;
    }).detach();
}

// ------------------------------------------------------------------ calls with an error text
namespace {
template <class F>
bool withError(QString* error, F call) {
    std::string e;
    const bool ok = call(&e);
    if (!ok && error) *error = QString::fromStdString(e);
    return ok;
}
}  // namespace

bool Engine::importMidiFile(const QString& path, QString* error) {
    return withError(error, [&](std::string* e) { return m_s.importMidiFile(path.toStdString(), e); });
}
bool Engine::exportMidiFile(const QString& path, QString* error) {
    return withError(error, [&](std::string* e) { return m_s.exportMidiFile(path.toStdString(), e); });
}
bool Engine::saveProject(const QString& path, QString* error) {
    return withError(error, [&](std::string* e) { return m_s.saveProject(path.toStdString(), e); });
}
bool Engine::loadProject(const QString& path, QString* error) {
    return withError(error, [&](std::string* e) { return m_s.loadProject(path.toStdString(), e); });
}
bool Engine::autosave(QString* error) {
    return withError(error, [&](std::string* e) { return m_s.autosave(e); });
}
bool Engine::exportWav(const QString& path, QString* error) {
    return withError(error, [&](std::string* e) { return m_s.exportWav(path.toStdString(), e); });
}
bool Engine::exportAudio(const QString& path, const ExportOptions& opt, QString* error) {
    return withError(error, [&](std::string* e) { return m_s.exportAudio(path.toStdString(), opt, e); });
}
