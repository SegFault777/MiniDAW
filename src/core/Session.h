// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The Qt-free heart of MiniDAW: project model, undo, tempo, transport, recording, rendering, export,
// project files and autosave. Audio devices, file decoding and the user interface live in the platform
// layer, which talks to the Session through 'Hooks'.
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "../dsp/TempoMap.h"
#include "AudioIO.h"
#include "I18n.h"
#include "MidiOps.h"
#include "Model.h"

namespace core {

class Session {
public:
    using DecodeDone = std::function<void(std::shared_ptr<AudioData>, std::string error)>;

    /// Everything the Session needs from the platform layer. All members are optional.
    struct Hooks {
        std::function<void(bool structural)> changed;                 ///< model changed (structural: tracks / effects, else regions)
        std::function<void()> tempoChanged, loopChanged;
        std::function<void(const std::string&)> message;              ///< a message for the user (already translated)
        std::function<std::string(const char*)> translate;            ///< English source text -> user language
        std::function<std::shared_ptr<dsp::Effect>(const std::string& id)> makeEffect;           ///< "vst3:..." / "native:..." effects
        std::function<std::shared_ptr<dsp::Instrument>(const std::string& id)> makeInstrument;   ///< "vst3:..." instruments
        std::function<std::string(dsp::ParamSet&)> saveState;         ///< plug-in state as base64 text
        std::function<void(dsp::ParamSet&, const std::string&)> loadState;
        std::function<void(const std::string& path, DecodeDone)> decode;   ///< decodes any audio file asynchronously
        std::function<void()> startInput, stopInput;                  ///< microphone capture for audio recording
    };

    Session();
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    void setHooks(Hooks h) { m_hooks = std::move(h); }
    /// Used for message texts when no translate hook is set.
    void setTranslations(std::shared_ptr<const TranslationTable> t) { m_table = std::move(t); }

    // ---- Model (UI thread) ----
    std::vector<Track> snapshot() const;
    int trackCount() const;
    DrumPattern drumPattern(int track) const;
    double regionBeats(const Region& r) const;
    double projectEndBeats() const;

    int addTrack(TrackType type, const std::string& name = {});
    void removeTrack(int i);
    void renameTrack(int i, const std::string& name);
    void setVolume(int i, float v);
    void setPan(int i, float p);
    void setSendA(int i, float v);
    void setSendB(int i, float v);
    void setMute(int i, bool on);
    void setSolo(int i, bool on);
    void setArmed(int i, bool on);
    void setMasterVolume(float v) { m_master = v; markDirty(); }
    float masterVolume() const { return m_master; }

    void addInsert(int track, const std::string& effectId);
    void removeInsert(int track, int slot);
    void setInsertBypass(int track, int slot, bool bypass);
    void setInstrument(int track, const std::string& instrumentId);
    void setInstrumentPreset(int track, int preset);
    std::shared_ptr<dsp::Effect> auxEffect(int bus) const { return m_aux[size_t(bus)]; }
    std::shared_ptr<dsp::Effect> limiter() const { return m_limiter; }

    void importAudio(const std::string& path, int track, double startBeat);
    bool importMidiFile(const std::string& path, std::string* error);
    bool exportMidiFile(const std::string& path, std::string* error);
    int addMidiRegion(int track, double startBeat, double lengthBeats);
    int addDrummerRegion(int track, double startBeat, int bars);
    void setDrummerParams(int track, int region, const dsp::DrummerParams& p);
    void moveRegion(int t, int r, double startBeat);
    void resizeRegion(int t, int r, double lengthBeats);
    void removeRegion(int t, int r);
    void splitRegion(int t, int r, double atBeat);
    int duplicateRegion(int t, int r);
    bool copyRegion(int t, int r);
    bool hasClipboard() const { return m_hasClip; }
    int pasteRegion(int track, double beat);
    int duplicateTrack(int i);
    void setRegionAudio(int t, int r, float gainDb, i64 fadeInFrames, i64 fadeOutFrames);
    void setRegionMute(int t, int r, bool mute);
    void moveTrack(int from, int to);
    void setTrackColor(int i, Color color);
    void reverseRegion(int t, int r);
    void normalizeRegion(int t, int r, float targetDb = -1.f);
    void setRegionNotes(int t, int r, const std::vector<Note>& notes);
    void setRegionFlex(int t, int r, bool follow, double sourceBpm, double semitones);
    void setStep(int track, int row, int step, bool on);
    void setDrumPattern(int track, const DrumPattern& p);

    // ---- Logic Pro style editing (v0.7) ----
    /// Renders a track (instrument, effects, fader and pan, without sends and master) to a new audio track and
    /// mutes the original, like Logic's "Bounce in Place". Returns the index of the new track or -1.
    int bounceTrackInPlace(int track);
    /// Replaces the chords of a MIDI region by an arpeggio.
    bool arpeggiateRegion(int t, int r, const ArpParams& p);
    /// Quantizes the notes of a MIDI region with strength and swing.
    bool quantizeRegion(int t, int r, const QuantizeParams& p);
    /// Moves the notes of a MIDI region into a scale.
    bool scaleQuantizeRegion(int t, int r, const ScaleParams& p);
    /// Randomises timing and velocity of the notes of a MIDI region.
    bool humanizeRegion(int t, int r, const HumanizeParams& p);
    /// Turns every note of a MIDI region into a chord.
    bool chordTriggerRegion(int t, int r, const ChordParams& p);
    // ---- more Logic Pro style editing (v0.8) ----
    bool legatoRegion(int t, int r);
    bool transposeRegion(int t, int r, int semitones);
    bool velocityRampRegion(int t, int r, int fromVelocity, int toVelocity);
    /// Puts 'copies' copies of a region directly after it (Logic's Repeat Regions). Returns how many were made.
    int repeatRegion(int t, int r, int copies);
    /// Merges a region with the next region of the track (MIDI regions, or neighbouring parts of one recording).
    bool joinRegions(int t, int r);
    /// Tempo of an audio region in BPM (0 when it has no clear pulse), like the analysis of Logic's Smart Tempo.
    double detectRegionTempo(int t, int r) const;
    /// Converts the step pattern of a drum machine track into a MIDI region on a new Drummer track.
    int convertPatternToMidi(int track, int bars);
    // ---- more Logic Pro style editing (v0.9) ----
    bool fixOverlapsRegion(int t, int r);
    bool removeDuplicatesRegion(int t, int r);
    bool fixedLengthRegion(int t, int r, double lengthBeats);
    bool retrogradeRegion(int t, int r);
    bool invertRegion(int t, int r);
    bool scaleVelocityRegion(int t, int r, int percent, int offset);
    /// Cuts the quiet parts out of an audio region (Logic's Strip Silence). Returns how many regions remain (0: failed).
    int stripSilenceRegion(int t, int r, double thresholdDb, double minSilenceMs, double padMs);
    /// Integrated loudness of an audio region in LUFS (dsp::kSilentLufs when silent or too short; 1000 when not audio).
    double regionLoudness(int t, int r) const;

    // ---- Markers (kept sorted by position) ----
    std::vector<Marker> markers() const;
    int addMarker(double beat, const std::string& name = {});
    void removeMarker(int i);
    void renameMarker(int i, const std::string& name);
    int moveMarker(int i, double beat);

    void setVolAuto(int track, const std::vector<AutoPoint>& pts);
    void setAuto(int track, AutoParam param, const std::vector<AutoPoint>& pts);

    // ---- Tempo and time signature ----
    std::shared_ptr<const dsp::TempoMap> tempoMap() const;
    void setTempoPoints(const std::vector<dsp::TempoPoint>& pts, bool commit = true);
    void setMeterPoints(const std::vector<dsp::MeterPoint>& pts);
    void commitTempo();
    double bpm() const;
    void setBpm(double bpm);
    double beatToFrame(double beat) const { return tempoMap()->beatToFrame(beat); }
    double frameToBeat(double frame) const { return tempoMap()->frameToBeat(frame); }

    // ---- Undo / redo ----
    void checkpoint() { pushUndo(); }
    void clearHistory() { m_undo.clear(); m_redo.clear(); }
    void undo();
    void redo();
    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }

    // ---- Transport ----
    void play();
    void pause();
    void stop();
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
    void setRecordingOffsetMs(int ms) { m_recOffsetMs = std::clamp(ms, -500, 500); }
    void playTestTone();
    void audition(const std::string& path);
    void stopAudition();

    // ---- Live playing ----
    void noteOn(int track, int pitch, int velocity);
    void noteOff(int track, int pitch);

    // ---- Project ----
    std::string projectPath() const { return m_path; }
    bool isDirty() const { return m_dirty; }
    void markDirty() { m_dirty = true; }
    void newProject();
    bool saveProject(const std::string& path, std::string* error);
    bool loadProject(const std::string& path, std::string* error);
    // ---- Autosave and crash recovery ----
    void setAutosaveDir(const std::string& dir) { m_autosaveDir = dir; }
    std::string autosavePath() const;
    bool hasAutosave() const;
    bool autosave(std::string* error = nullptr);
    void clearAutosave();
    void markRecovered();

    bool exportWav(const std::string& path, std::string* error);
    bool exportAudio(const std::string& path, const ExportOptions& opt, std::string* error);

    // ---- Called by the audio threads ----
    void fillInt16(std::int16_t* out, int frames);
    void pushRecorded(const std::vector<float>& stereo);

    /// Renders the whole project from 'startFrame' without a device (tests, offline tools). Returns stereo float.
    std::vector<float> renderOffline(i64 startFrame, i64 frames);

private:
    struct Ev { int off; bool on; int pitch; int vel; };
    struct UndoState { std::vector<Track> tracks; std::shared_ptr<const dsp::TempoMap> tempo; std::vector<Marker> markers; };

    Text tr(const char* key) const;
    void message(const std::string& text) const { if (m_hooks.message) m_hooks.message(text); }
    std::shared_ptr<dsp::Effect> makeEffect(const std::string& id) const;
    std::shared_ptr<dsp::Instrument> makeInstrument(const std::string& id) const;
    Track makeTrack(TrackType type, const std::string& name);
    void changed(bool structural);
    void pushUndo();
    void restore(UndoState s);
    void decodeAsync(const std::string& path, DecodeDone done);
    void startPlayback();
    void flushDsp();
    void regenDrummer(Region& r, const dsp::TempoMap& tm) const;
    void renderBlock(i64 pos, int frames, float* out, bool running);
    void addClicks(i64 timelineFrame, int frames, float* out);
    void masterProcess(float* buf, int frames);
    void setTempoMap(std::shared_ptr<const dsp::TempoMap> map, bool commit);
    bool writeProject(const std::string& path, bool autosave, std::string* error);
    void ensureScratch(size_t need);
    bool editNotes(int t, int r, const std::function<std::vector<Note>(const std::vector<Note>&, double regionBeats)>& edit);

    Hooks m_hooks;
    std::shared_ptr<const TranslationTable> m_table;
    mutable std::mutex m_mutex;
    std::vector<Track> m_tracks;
    std::vector<UndoState> m_undo, m_redo;
    std::vector<Marker> m_markers;   // guarded by m_mutex
    std::string m_autosaveDir;
    int m_autoTake = 1;
    std::map<const void*, std::pair<std::weak_ptr<const AudioData>, std::string>> m_autoFiles;
    int m_nextId = 1;
    std::array<std::shared_ptr<dsp::Effect>, 2> m_aux;   // aux A: reverb, aux B: echo
    std::shared_ptr<dsp::Effect> m_limiter;

    mutable std::mutex m_tempoMutex;
    std::shared_ptr<const dsp::TempoMap> m_tempo;
    const dsp::TempoMap* m_tm = nullptr;   // valid while the audio thread holds m_mutex

    i64 m_drumMax = 0;
    int m_only = -1;   // >= 0 while one track is rendered on its own (Bounce in Place); guarded by m_mutex
    std::vector<float> m_scratch, m_trackBuf;
    std::array<std::vector<float>, 2> m_auxBuf;
    std::vector<Ev> m_events;
    float m_masterL = 0, m_masterR = 0;

    std::atomic<i64> m_pos{0}, m_expected{-1}, m_countRemain{0}, m_countTotal{0}, m_idle{0};
    std::atomic<i64> m_countClick{0}, m_toneRemain{0};
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
    i64 m_auditionPos = 0;

    // Recording state
    std::mutex m_recMutex;
    std::vector<float> m_recBuf;
    std::atomic<int> m_recTrackId{-1};
    double m_recStartBeat = 0;
    struct Pending { double start; int vel; };
    std::map<int, Pending> m_recPending;
    std::vector<Note> m_recNotes;   // absolute beat positions

    std::string m_path;
};

}  // namespace core
