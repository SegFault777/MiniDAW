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
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <vector>
#include "dsp/AudioOps.h"
#include "dsp/DrumKit.h"
#include "dsp/Drummer.h"
#include "dsp/Effects.h"
#include "dsp/Instrument.h"
#include "dsp/Synth.h"
#include "dsp/TempoMap.h"

constexpr int kSampleRate = 44100;
constexpr int kPeakStride = 256;   // frames summarised by one waveform peak
constexpr int kDrumRows = 4;       // step sequencer rows: kick, snare, hi-hat, clap
constexpr int kSteps = 16;
using DrumPattern = std::array<std::array<bool, kSteps>, kDrumRows>;

enum class TrackType { Audio, Instrument, Drums, Drummer };

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

/// A region on a track. Positions are in quarter-note beats so music follows tempo changes.
struct Region {
    QString name;
    bool midi = false;
    double start = 0;                        // timeline position in beats
    double lengthBeats = 4;                  // MIDI regions only
    std::vector<Note> notes;
    bool drummer = false;                    // notes are generated from 'drum'
    dsp::DrummerParams drum;

    // Audio regions. 'audio' is what is played: either 'source' itself or a time-stretched copy of it.
    std::shared_ptr<const AudioData> audio;
    std::shared_ptr<const AudioData> source;
    QString pendingPath;                     // audio still being decoded after a project load
    qint64 offset = 0, frames = 0;           // used part of 'audio'
    qint64 srcOffset = 0, srcFrames = 0;     // used part of 'source'
    bool follow = false;                     // stretch to the project tempo
    double sourceBpm = 0;                    // tempo of the recording (needed for 'follow')
    double semitones = 0;                    // pitch transposition
    float gainDb = 0;                        // region gain (v0.5)
    qint64 fadeIn = 0, fadeOut = 0;          // fade lengths in frames of the played audio (v0.5)
    double flexBpm = 0;                      // project tempo that 'audio' was stretched for
    bool flexed() const { return follow || std::abs(semitones) > 0.01; }
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
    std::shared_ptr<dsp::Instrument> instrument;   // Instrument and Drummer tracks
    QString instrumentId;                          // "synth", "drumkit" or "vst3:..."
    int preset = 0;
    std::vector<AutoPoint> volAuto;
    float peakL = 0, peakR = 0;
};

/// Options of "Bounce to WAV" (v0.5).
struct ExportOptions {
    bool cycleOnly = false;                              ///< Bounce only the cycle range instead of the whole project.
    dsp::WavFormat format = dsp::WavFormat::Pcm16;       ///< 16-bit, 24-bit or 32-bit float.
    bool normalize = false;                              ///< Scale the result so that its peak reaches 'normalizeDb'.
    float normalizeDb = -1.f;
    bool dither = false;                                 ///< TPDF dither (16-bit only).
    bool tail = true;                                    ///< Add 2 s for reverb and echo tails.
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
    float masterVolume() const { return m_master; }

    void addInsert(int track, const QString& effectId);
    void removeInsert(int track, int slot);
    void setInsertBypass(int track, int slot, bool bypass);
    void setInstrument(int track, const QString& instrumentId);
    void setInstrumentPreset(int track, int preset);
    std::shared_ptr<dsp::Effect> auxEffect(int bus) const { return m_aux[size_t(bus)]; }
    std::shared_ptr<dsp::Effect> limiter() const { return m_limiter; }

    void importAudio(const QString& path, int track, double startBeat);
    bool importMidiFile(const QString& path, QString* error);
    bool exportMidiFile(const QString& path, QString* error);
    int addMidiRegion(int track, double startBeat, double lengthBeats);
    int addDrummerRegion(int track, double startBeat, int bars);
    void setDrummerParams(int track, int region, const dsp::DrummerParams& p);
    void moveRegion(int t, int r, double startBeat);
    void resizeRegion(int t, int r, double lengthBeats);
    void removeRegion(int t, int r);
    void splitRegion(int t, int r, double atBeat);
    int duplicateRegion(int t, int r);
    bool copyRegion(int t, int r);                       ///< Puts a region on the clipboard.
    bool hasClipboard() const { return m_hasClip; }
    int pasteRegion(int track, double beat);             ///< Pastes the clipboard region; returns its index or -1.
    int duplicateTrack(int i);                           ///< Copies a track with its regions, instrument and inserts.
    void setRegionAudio(int t, int r, float gainDb, qint64 fadeInFrames, qint64 fadeOutFrames);
    void reverseRegion(int t, int r);                    ///< Reverses the audio of a region (creates a reversed copy).
    void normalizeRegion(int t, int r, float targetDb = -1.f);   ///< Sets the region gain so that its peak is at targetDb.
    void setRegionNotes(int t, int r, const std::vector<Note>& notes);
    void setRegionFlex(int t, int r, bool follow, double sourceBpm, double semitones);
    void setStep(int track, int row, int step, bool on);
    void setDrumPattern(int track, const DrumPattern& p);
    void setVolAuto(int track, const std::vector<AutoPoint>& pts);

    // ---- Tempo and time signature ----
    std::shared_ptr<const dsp::TempoMap> tempoMap() const;
    void setTempoPoints(const std::vector<dsp::TempoPoint>& pts, bool commit = true);
    void setMeterPoints(const std::vector<dsp::MeterPoint>& pts);
    void commitTempo();                 ///< Re-stretches regions that follow the tempo.
    double bpm() const;                 ///< Tempo at the playhead.
    void setBpm(double bpm);            ///< Edits the tempo at the playhead.
    double beatToFrame(double beat) const { return tempoMap()->beatToFrame(beat); }
    double frameToBeat(double frame) const { return tempoMap()->frameToBeat(frame); }

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
    double positionBeats() const { return frameToBeat(double(m_pos.load())); }
    void setMetronome(bool on) { m_metro = on; }
    bool metronome() const { return m_metro; }
    void setCountIn(bool on) { m_countIn = on; }
    bool countIn() const { return m_countIn; }
    void setLoop(bool on, double a = -1, double b = -1);
    bool loopOn() const { return m_loopOn; }
    double loopA() const { return m_loopA; }
    double loopB() const { return m_loopB; }
    void masterPeaks(float& l, float& r) const;

    // ---- Audio device ----
    void setAudioOutput(const QByteArray& deviceId, int bufferFrames);
    /// Recording input device (empty = system default) and latency compensation in ms:
    /// positive values move recorded audio earlier, negative values later.
    void setAudioInput(const QByteArray& deviceId, int offsetMs);
    QString audioStatus() const { return m_audioStatus; }
    void playTestTone();
    void audition(const QString& path);   // plays a file once, independent of the transport
    void stopAudition();

    // ---- Live playing (musical typing, MIDI keyboard, piano roll preview) ----
    void noteOn(int track, int pitch, int velocity);
    void noteOff(int track, int pitch);

    // ---- Project ----
    QString projectPath() const { return m_path; }
    bool isDirty() const { return m_dirty; }
    void markDirty() { m_dirty = true; }
    void newProject();
    bool saveProject(const QString& path, QString* error);
    bool loadProject(const QString& path, QString* error);
    bool exportWav(const QString& path, QString* error);   ///< 16-bit WAV of the whole project.
    bool exportAudio(const QString& path, const ExportOptions& opt, QString* error);

    // ---- Called by the audio threads ----
    void fillInt16(qint16* out, int frames);
    void pushRecorded(const std::vector<float>& stereo);

signals:
    void tracksChanged();    // structural change: tracks, effects, names
    void regionsChanged();   // regions, notes, patterns or automation changed
    void tempoChanged();
    void loopChanged();
    void audioStatusChanged(const QString& status);
    void message(const QString& text);

private:
    struct Ev { int off; bool on; int pitch; int vel; };
    struct UndoState { std::vector<Track> tracks; std::shared_ptr<const dsp::TempoMap> tempo; };

    Track makeTrack(TrackType type, const QString& name);
    void changed(bool structural);
    void pushUndo();
    void restore(UndoState s);
    void decodeAsync(const QString& path, std::function<void(std::shared_ptr<AudioData>, QString)> done);
    void startPlayback();
    void flushDsp();
    void regenDrummer(Region& r, const dsp::TempoMap& tm) const;
    void renderBlock(qint64 pos, int frames, float* out, bool running);
    void addClicks(qint64 timelineFrame, int frames, float* out);
    void masterProcess(float* buf, int frames);
    void setTempoMap(std::shared_ptr<const dsp::TempoMap> map, bool commit);

    mutable QMutex m_mutex;
    std::vector<Track> m_tracks;
    std::vector<UndoState> m_undo, m_redo;
    int m_nextId = 1;
    std::array<std::shared_ptr<dsp::Effect>, 2> m_aux;   // aux A: reverb, aux B: echo
    std::shared_ptr<dsp::Effect> m_limiter;

    mutable QMutex m_tempoMutex;
    std::shared_ptr<const dsp::TempoMap> m_tempo;
    const dsp::TempoMap* m_tm = nullptr;   // valid while the audio thread holds m_mutex

    qint64 m_drumMax = 0;
    std::vector<float> m_scratch, m_trackBuf;
    std::array<std::vector<float>, 2> m_auxBuf;
    std::vector<Ev> m_events;
    float m_masterL = 0, m_masterR = 0;

    std::atomic<qint64> m_pos{0}, m_expected{-1}, m_countRemain{0}, m_countTotal{0}, m_idle{0};
    std::atomic<qint64> m_countClick{0}, m_toneRemain{0};
    std::atomic<int> m_countPerBar{4};
    std::atomic<double> m_loopA{0.0}, m_loopB{16.0};
    std::atomic<bool> m_playing{false}, m_recording{false}, m_recAudio{false};
    std::atomic<bool> m_metro{false}, m_countIn{false}, m_loopOn{false}, m_dirty{false};
    std::atomic<int> m_flush{0};   // bit 0: release synth notes, bit 1: reset every effect
    std::atomic<float> m_master{0.9f};
    std::atomic<int> m_recOffsetMs{0};
    Region m_clip;                 // region clipboard (guarded by m_mutex)
    std::atomic<bool> m_hasClip{false};
    std::shared_ptr<const AudioData> m_audition;
    qint64 m_auditionPos = 0;

    // Recording state
    QMutex m_recMutex;
    std::vector<float> m_recBuf;
    std::atomic<int> m_recTrackId{-1};
    double m_recStartBeat = 0;
    struct Pending { double start; int vel; };
    std::map<int, Pending> m_recPending;
    std::vector<Note> m_recNotes;   // absolute beat positions

    QString m_path, m_audioStatus;
    QThread m_thread;
    AudioOutput* m_out = nullptr;
    AudioInput* m_in = nullptr;
};
