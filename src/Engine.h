// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The Qt side of the engine: audio devices, file decoding, plug-in hosts and signals. All music logic lives in
// the Qt-free core::Session; this class only adapts it to the Qt user interface (QString, QColor, signals).
#include <QByteArray>
#include <QColor>
#include <QObject>
#include <QString>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>
#include "core/Session.h"
#include "platform/Audio.h"

class QTimer;

// The model types are shared with the core.
using core::AudioData;
using core::AutoParam;
using core::AutoPoint;
using core::autoRange;
using core::DrumPattern;
using core::ExportOptions;
using core::Marker;
using core::Note;
using core::Region;
using core::Track;
using core::TrackType;
using core::ArpParams;
using core::QuantizeParams;
using core::ScaleParams;
using core::HumanizeParams;
using core::ChordParams;

class Engine : public QObject {
    Q_OBJECT
public:
    explicit Engine(QObject* parent = nullptr);
    ~Engine() override;

    core::Session& session() { return m_s; }

    // ---- Model (UI thread) ----
    std::vector<Track> snapshot() const { return m_s.snapshot(); }
    int trackCount() const { return m_s.trackCount(); }
    DrumPattern drumPattern(int track) const { return m_s.drumPattern(track); }
    double regionBeats(const Region& r) const { return m_s.regionBeats(r); }
    double projectEndBeats() const { return m_s.projectEndBeats(); }

    int addTrack(TrackType type, const QString& name = {}) { return m_s.addTrack(type, name.toStdString()); }
    void removeTrack(int i) { m_s.removeTrack(i); }
    void renameTrack(int i, const QString& name) { m_s.renameTrack(i, name.toStdString()); }
    void setVolume(int i, float v) { m_s.setVolume(i, v); }
    void setPan(int i, float p) { m_s.setPan(i, p); }
    void setSendA(int i, float v) { m_s.setSendA(i, v); }
    void setSendB(int i, float v) { m_s.setSendB(i, v); }
    void setMute(int i, bool on) { m_s.setMute(i, on); }
    void setSolo(int i, bool on) { m_s.setSolo(i, on); }
    void setArmed(int i, bool on) { m_s.setArmed(i, on); }
    void setMasterVolume(float v) { m_s.setMasterVolume(v); }
    float masterVolume() const { return m_s.masterVolume(); }

    void addInsert(int track, const QString& effectId) { m_s.addInsert(track, effectId.toStdString()); }
    void removeInsert(int track, int slot) { m_s.removeInsert(track, slot); }
    void setInsertBypass(int track, int slot, bool bypass) { m_s.setInsertBypass(track, slot, bypass); }
    void setInstrument(int track, const QString& instrumentId) { m_s.setInstrument(track, instrumentId.toStdString()); }
    void setInstrumentPreset(int track, int preset) { m_s.setInstrumentPreset(track, preset); }
    std::shared_ptr<dsp::Effect> auxEffect(int bus) const { return m_s.auxEffect(bus); }
    std::shared_ptr<dsp::Effect> limiter() const { return m_s.limiter(); }

    void importAudio(const QString& path, int track, double startBeat) { m_s.importAudio(path.toStdString(), track, startBeat); }
    bool importMidiFile(const QString& path, QString* error);
    bool exportMidiFile(const QString& path, QString* error);
    int addMidiRegion(int track, double startBeat, double lengthBeats) { return m_s.addMidiRegion(track, startBeat, lengthBeats); }
    int addDrummerRegion(int track, double startBeat, int bars) { return m_s.addDrummerRegion(track, startBeat, bars); }
    void setDrummerParams(int track, int region, const dsp::DrummerParams& p) { m_s.setDrummerParams(track, region, p); }
    void moveRegion(int t, int r, double startBeat) { m_s.moveRegion(t, r, startBeat); }
    void resizeRegion(int t, int r, double lengthBeats) { m_s.resizeRegion(t, r, lengthBeats); }
    void removeRegion(int t, int r) { m_s.removeRegion(t, r); }
    void splitRegion(int t, int r, double atBeat) { m_s.splitRegion(t, r, atBeat); }
    int duplicateRegion(int t, int r) { return m_s.duplicateRegion(t, r); }
    bool copyRegion(int t, int r) { return m_s.copyRegion(t, r); }
    bool hasClipboard() const { return m_s.hasClipboard(); }
    int pasteRegion(int track, double beat) { return m_s.pasteRegion(track, beat); }
    int duplicateTrack(int i) { return m_s.duplicateTrack(i); }
    void setRegionAudio(int t, int r, float gainDb, qint64 fadeInFrames, qint64 fadeOutFrames) { m_s.setRegionAudio(t, r, gainDb, fadeInFrames, fadeOutFrames); }
    void setRegionMute(int t, int r, bool mute) { m_s.setRegionMute(t, r, mute); }
    void moveTrack(int from, int to) { m_s.moveTrack(from, to); }
    void setTrackColor(int i, const QColor& color) {
        if (color.isValid()) m_s.setTrackColor(i, core::Color{std::uint8_t(color.red()), std::uint8_t(color.green()), std::uint8_t(color.blue())});
    }
    void reverseRegion(int t, int r) { m_s.reverseRegion(t, r); }
    void normalizeRegion(int t, int r, float targetDb = -1.f) { m_s.normalizeRegion(t, r, targetDb); }
    void setRegionNotes(int t, int r, const std::vector<Note>& notes) { m_s.setRegionNotes(t, r, notes); }
    void setRegionFlex(int t, int r, bool follow, double sourceBpm, double semitones) { m_s.setRegionFlex(t, r, follow, sourceBpm, semitones); }
    void setStep(int track, int row, int step, bool on) { m_s.setStep(track, row, step, on); }
    void setDrumPattern(int track, const DrumPattern& p) { m_s.setDrumPattern(track, p); }

    // ---- Logic Pro style editing (v0.7) ----
    int bounceTrackInPlace(int track) { return m_s.bounceTrackInPlace(track); }
    bool arpeggiateRegion(int t, int r, const ArpParams& p) { return m_s.arpeggiateRegion(t, r, p); }
    bool quantizeRegion(int t, int r, const QuantizeParams& p) { return m_s.quantizeRegion(t, r, p); }
    bool scaleQuantizeRegion(int t, int r, const ScaleParams& p) { return m_s.scaleQuantizeRegion(t, r, p); }
    bool humanizeRegion(int t, int r, const HumanizeParams& p) { return m_s.humanizeRegion(t, r, p); }
    bool chordTriggerRegion(int t, int r, const ChordParams& p) { return m_s.chordTriggerRegion(t, r, p); }
    // ---- more Logic Pro style editing (v0.8) ----
    bool legatoRegion(int t, int r) { return m_s.legatoRegion(t, r); }
    bool transposeRegion(int t, int r, int semitones) { return m_s.transposeRegion(t, r, semitones); }
    bool velocityRampRegion(int t, int r, int from, int to) { return m_s.velocityRampRegion(t, r, from, to); }
    int repeatRegion(int t, int r, int copies) { return m_s.repeatRegion(t, r, copies); }
    bool joinRegions(int t, int r) { return m_s.joinRegions(t, r); }
    double detectRegionTempo(int t, int r) const { return m_s.detectRegionTempo(t, r); }
    int convertPatternToMidi(int track, int bars) { return m_s.convertPatternToMidi(track, bars); }
    // ---- more Logic Pro style editing (v0.9) ----
    bool fixOverlapsRegion(int t, int r) { return m_s.fixOverlapsRegion(t, r); }
    bool removeDuplicatesRegion(int t, int r) { return m_s.removeDuplicatesRegion(t, r); }
    bool fixedLengthRegion(int t, int r, double beats) { return m_s.fixedLengthRegion(t, r, beats); }
    bool retrogradeRegion(int t, int r) { return m_s.retrogradeRegion(t, r); }
    bool invertRegion(int t, int r) { return m_s.invertRegion(t, r); }
    bool scaleVelocityRegion(int t, int r, int percent, int offset) { return m_s.scaleVelocityRegion(t, r, percent, offset); }
    int stripSilenceRegion(int t, int r, double db, double minMs, double padMs) { return m_s.stripSilenceRegion(t, r, db, minMs, padMs); }
    double regionLoudness(int t, int r) const { return m_s.regionLoudness(t, r); }

    // ---- Markers (kept sorted by position) ----
    std::vector<Marker> markers() const { return m_s.markers(); }
    int addMarker(double beat, const QString& name = {}) { return m_s.addMarker(beat, name.toStdString()); }
    void removeMarker(int i) { m_s.removeMarker(i); }
    void renameMarker(int i, const QString& name) { m_s.renameMarker(i, name.toStdString()); }
    int moveMarker(int i, double beat) { return m_s.moveMarker(i, beat); }

    void setVolAuto(int track, const std::vector<AutoPoint>& pts) { m_s.setVolAuto(track, pts); }
    void setAuto(int track, AutoParam param, const std::vector<AutoPoint>& pts) { m_s.setAuto(track, param, pts); }

    // ---- Tempo and time signature ----
    std::shared_ptr<const dsp::TempoMap> tempoMap() const { return m_s.tempoMap(); }
    void setTempoPoints(const std::vector<dsp::TempoPoint>& pts, bool commit = true) { m_s.setTempoPoints(pts, commit); }
    void setMeterPoints(const std::vector<dsp::MeterPoint>& pts) { m_s.setMeterPoints(pts); }
    void commitTempo() { m_s.commitTempo(); }
    double bpm() const { return m_s.bpm(); }
    void setBpm(double bpm) { m_s.setBpm(bpm); }
    double beatToFrame(double beat) const { return m_s.beatToFrame(beat); }
    double frameToBeat(double frame) const { return m_s.frameToBeat(frame); }

    // ---- Undo / redo ----
    void checkpoint() { m_s.checkpoint(); }
    void undo() { m_s.undo(); }
    void redo() { m_s.redo(); }
    bool canUndo() const { return m_s.canUndo(); }
    bool canRedo() const { return m_s.canRedo(); }

    // ---- Transport ----
    void play() { m_s.play(); }
    void pause() { m_s.pause(); }
    void stop() { m_s.stop(); }
    bool isPlaying() const { return m_s.isPlaying(); }
    bool isRecording() const { return m_s.isRecording(); }
    bool startRecording() { return m_s.startRecording(); }
    void stopRecording() { m_s.stopRecording(); }
    void seekBeats(double beat) { m_s.seekBeats(beat); }
    double positionBeats() const { return m_s.positionBeats(); }
    void setMetronome(bool on) { m_s.setMetronome(on); }
    bool metronome() const { return m_s.metronome(); }
    void setCountIn(bool on) { m_s.setCountIn(on); }
    bool countIn() const { return m_s.countIn(); }
    void setLoop(bool on, double a = -1, double b = -1) { m_s.setLoop(on, a, b); }
    bool loopOn() const { return m_s.loopOn(); }
    double loopA() const { return m_s.loopA(); }
    double loopB() const { return m_s.loopB(); }
    void masterPeaks(float& l, float& r) const { m_s.masterPeaks(l, r); }

    // ---- Audio device ----
    void setAudioOutput(const QByteArray& deviceId, int bufferFrames);
    /// Recording input device (empty = system default) and latency compensation in ms:
    /// positive values move recorded audio earlier, negative values later.
    void setAudioInput(const QByteArray& deviceId, int offsetMs);
    QString audioStatus() const { return m_audioStatus; }
    void playTestTone() { m_s.playTestTone(); }
    void audition(const QString& path) { m_s.audition(path.toStdString()); }   // plays a file once, independent of the transport
    void stopAudition() { m_s.stopAudition(); }

    // ---- Live playing (musical typing, MIDI keyboard, piano roll preview) ----
    void noteOn(int track, int pitch, int velocity) { m_s.noteOn(track, pitch, velocity); }
    void noteOff(int track, int pitch) { m_s.noteOff(track, pitch); }

    // ---- Project ----
    QString projectPath() const { return QString::fromStdString(m_s.projectPath()); }
    bool isDirty() const { return m_s.isDirty(); }
    void markDirty() { m_s.markDirty(); }
    void newProject() { m_s.newProject(); }
    bool saveProject(const QString& path, QString* error);
    bool loadProject(const QString& path, QString* error);
    // ---- Autosave and crash recovery ----
    void setAutosaveDir(const QString& dir) { m_s.setAutosaveDir(dir.toStdString()); }
    QString autosavePath() const { return QString::fromStdString(m_s.autosavePath()); }
    bool hasAutosave() const { return m_s.hasAutosave(); }
    bool autosave(QString* error = nullptr);
    void clearAutosave() { m_s.clearAutosave(); }
    void markRecovered() { m_s.markRecovered(); }

    bool exportWav(const QString& path, QString* error);
    bool exportAudio(const QString& path, const ExportOptions& opt, QString* error);

    // ---- Called by the audio threads ----
    void fillInt16(qint16* out, int frames) { m_s.fillInt16(out, frames); }
    void pushRecorded(const std::vector<float>& stereo) { m_s.pushRecorded(stereo); }

signals:
    void tracksChanged();    // structural change: tracks, effects, names
    void regionsChanged();   // regions, notes, patterns or automation changed
    void tempoChanged();
    void loopChanged();
    void audioStatusChanged(const QString& status);
    void message(const QString& text);

private:
    void decodeAsync(const QString& path, std::function<void(std::shared_ptr<AudioData>, QString)> done);
    void restartOutput();
    void startInput();
    void pollDevices();

    core::Session m_s;
    QString m_audioStatus;
    std::unique_ptr<platform::AudioStream> m_out, m_in;   // the system sound streams (no Qt Multimedia)
    QByteArray m_outId, m_inId;
    int m_bufferFrames = 2048;
    QString m_outDefaultAtOpen;                            // default device when the output was opened
    QTimer* m_poll = nullptr;                              // notices unplugged / changed devices
    std::atomic<int> m_decoding{0};                        // worker threads that still have to post their result
};
