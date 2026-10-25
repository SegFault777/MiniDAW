// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QColor>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QThread>
#include <algorithm>
#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <vector>
#include "dsp/Effects.h"
#include "dsp/Synth.h"

constexpr int kSampleRate = 44100;
constexpr int kPeakStride = 256;   // frames summarised by one waveform peak
constexpr int kDrumRows = 4;       // kick, snare, hi-hat, clap
constexpr int kSteps = 16;
constexpr int kBeatsPerBar = 4;
using DrumPattern = std::array<std::array<bool, kSteps>, kDrumRows>;

enum class TrackType { Audio, Instrument, Drums };

/// Decoded audio shared between regions (split / duplicated regions reference the same data).
struct AudioData {
    QString name;
    mutable QString path;        // source file; empty for fresh recordings until the project is saved
    std::vector<float> pcm;      // interleaved stereo, 44.1 kHz
    std::vector<float> peaks;    // max |sample| per kPeakStride frames
    qint64 frames() const { return qint64(pcm.size() / 2); }
};

/// One MIDI note. Times are in beats relative to the start of the owning region.
struct Note {
    int pitch = 60;
    double start = 0, length = 0.25;
    int velocity = 100;
};

/// A region on a track. Positions are in beats so MIDI follows tempo changes like in a musical DAW.
struct Region {
    QString name;
    bool midi = false;
    double start = 0;                       // timeline position in beats
    double lengthBeats = 4;                 // MIDI regions only
    std::shared_ptr<const AudioData> audio; // audio regions only
    QString pendingPath;                    // audio still being decoded after a project load
    qint64 offset = 0, frames = 0;          // audio frames used
    std::vector<Note> notes;
};

struct AutoPoint {
    double beat = 0;
    float value = 0;   // volume, 0..1.5
};

struct Track {
    int id = 0;
    QString name;
    QColor color;
    TrackType type = TrackType::Audio;
    float volume = 0.8f, pan = 0.f, sendA = 0.f, sendB = 0.f;
    bool mute = false, solo = false, armed = false;
    std::vector<Region> regions;
    DrumPattern steps{};
    std::vector<std::shared_ptr<dsp::Effect>> inserts;
    std::shared_ptr<dsp::Synth> synth;
    int synthPreset = 0;
    std::vector<AutoPoint> volAuto;
    float peakL = 0, peakR = 0;
};

class AudioOutput;
class AudioInput;

class Engine : public QObject {
    Q_OBJECT
public:
    explicit Engine(QObject* parent = nullptr);
    ~Engine() override;

    // ---- Model (UI thread) ----
    std::vector<Track> snapshot() const;
    int trackCount() const;
    DrumPattern drumPattern(int track) const;
    double regionBeats(const Region& r) const;
    double projectEndBeats() const;

    int addTrack(TrackType type, const QString& name = {});
    void removeTrack(int i);
    void renameTrack(int i, const QString& name);
    void setVolume(int i, float v);
    void setPan(int i, float p);
    void setSendA(int i, float v);
    void setSendB(int i, float v);
    void setMute(int i, bool on);
    void setSolo(int i, bool on);
    void setArmed(int i, bool on);
    void setMasterVolume(float v) { m_master = v; markDirty(); }

    void addInsert(int track, const QString& effectId);
    void removeInsert(int track, int slot);
    void setInsertBypass(int track, int slot, bool bypass);
    void setSynthPreset(int track, int preset);
    std::shared_ptr<dsp::Effect> auxEffect(int bus) const { return m_aux[size_t(bus)]; }
    std::shared_ptr<dsp::Effect> limiter() const { return m_limiter; }

    void importAudio(const QString& path, int track, double startBeat);
    int addMidiRegion(int track, double startBeat, double lengthBeats);
    void moveRegion(int t, int r, double startBeat);
    void resizeRegion(int t, int r, double lengthBeats);
    void removeRegion(int t, int r);
    void splitRegion(int t, int r, double atBeat);
    int duplicateRegion(int t, int r);
    void setRegionNotes(int t, int r, const std::vector<Note>& notes);
    void setStep(int track, int row, int step, bool on);
    void setDrumPattern(int track, const DrumPattern& p);
    void setVolAuto(int track, const std::vector<AutoPoint>& pts);

    // ---- Undo / redo ----
    void checkpoint();   // call before a gesture that modifies the project
    void undo();
    void redo();
    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }

    // ---- Transport ----
    void play();
    void pause();
    void stop();                 // pauses; a second stop returns to the start
    bool isPlaying() const { return m_playing; }
    bool isRecording() const { return m_recording; }
    bool startRecording();
    void stopRecording();
    void seekBeats(double beat);
    double positionBeats() const { return double(m_pos.load()) / framesPerBeat(); }
    void setBpm(double b);
    double bpm() const { return m_bpm; }
    double framesPerBeat() const { return kSampleRate * 60.0 / m_bpm; }
    void setMetronome(bool on) { m_metro = on; }
    bool metronome() const { return m_metro; }
    void setCountIn(bool on) { m_countIn = on; }
    bool countIn() const { return m_countIn; }
    void setLoop(bool on, double a = -1, double b = -1);
    bool loopOn() const { return m_loopOn; }
    double loopA() const { return m_loopA; }
    double loopB() const { return m_loopB; }
    void masterPeaks(float& l, float& r) const;

    // ---- Live playing (musical typing / piano roll preview) ----
    void noteOn(int track, int pitch, int velocity);
    void noteOff(int track, int pitch);

    // ---- Project ----
    QString projectPath() const { return m_path; }
    bool isDirty() const { return m_dirty; }
    void markDirty() { m_dirty = true; }
    void newProject();
    bool saveProject(const QString& path, QString* error);
    bool loadProject(const QString& path, QString* error);
    bool exportWav(const QString& path, QString* error);

    // ---- Called by the audio threads ----
    void fillInt16(qint16* out, int frames);
    void pushRecorded(const std::vector<float>& stereo);

signals:
    void tracksChanged();    // structural change: tracks, effects, names
    void regionsChanged();   // regions, notes, patterns or automation changed
    void loopChanged();
    void message(const QString& text);

private:
    struct Ev { int off; bool on; int pitch; int vel; };

    Track makeTrack(TrackType type, const QString& name);
    void changed(bool structural);
    void pushUndo();
    void decodeAsync(const QString& path, std::function<void(std::shared_ptr<AudioData>, QString)> done);
    void startPlayback();
    void synthDrums();
    void flushDsp();
    void renderBlock(qint64 pos, int frames, float* out, bool running);
    void addClicks(qint64 timelineFrame, int frames, float* out);
    void masterProcess(float* buf, int frames);

    mutable QMutex m_mutex;
    std::vector<Track> m_tracks;
    std::vector<std::vector<Track>> m_undo, m_redo;
    int m_nextId = 1;
    std::array<std::shared_ptr<dsp::Effect>, 2> m_aux;   // aux A: reverb, aux B: echo
    std::shared_ptr<dsp::Effect> m_limiter;

    std::array<std::vector<float>, kDrumRows> m_drum;
    qint64 m_drumMax = 0;
    std::vector<float> m_scratch, m_trackBuf;
    std::array<std::vector<float>, 2> m_auxBuf;
    std::vector<Ev> m_events;
    float m_masterL = 0, m_masterR = 0;

    std::atomic<qint64> m_pos{0}, m_expected{-1}, m_countRemain{0}, m_countTotal{0}, m_idle{0};
    std::atomic<double> m_bpm{120.0}, m_loopA{0.0}, m_loopB{16.0};
    std::atomic<bool> m_playing{false}, m_recording{false}, m_recAudio{false};
    std::atomic<bool> m_metro{false}, m_countIn{false}, m_loopOn{false}, m_dirty{false};
    std::atomic<int> m_flush{0};   // bit 0: release synth notes, bit 1: reset every effect
    std::atomic<float> m_master{0.9f};

    // Recording state
    QMutex m_recMutex;
    std::vector<float> m_recBuf;
    std::atomic<int> m_recTrackId{-1};
    double m_recStartBeat = 0;
    struct Pending { double start; int vel; };
    std::map<int, Pending> m_recPending;
    std::vector<Note> m_recNotes;   // absolute beat positions

    QString m_path;
    QThread m_thread;
    AudioOutput* m_out = nullptr;
    AudioInput* m_in = nullptr;
};
