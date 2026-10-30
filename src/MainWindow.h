// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QMainWindow>
#include <QMenu>
#include <QTimer>
#include <map>
#include <set>
#include "Engine.h"

class Timeline;
class Inspector;
class MixerPanel;
class InstrumentPanel;
class DrummerPanel;
class PianoRoll;
class StepSequencer;
class LoopBrowser;
class MidiInput;
class QTabWidget;
class QSplitter;
class QDoubleSpinBox;
class QToolButton;
class QLabel;
class QAction;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    bool openProject(const QString& path);

protected:
    void closeEvent(QCloseEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;

private:
    enum Tab { TabMixer, TabSmart, TabRoll, TabSteps, TabDrummer };

    void buildActions();
    void buildMenus();
    QWidget* buildControlBar();
    void applySettings();
    void tick();
    void onSelection(int track, int region);
    void onRecord();
    void onDelete();
    void showBottom(int tab);
    void newProject();
    bool maybeSave();
    bool saveProject(bool saveAs);
    void importDialog();
    void exportDialog();
    void meterDialog();
    void preferences();
    void about();
    int musicalTrack() const;
    void liveNoteOn(int pitch, int velocity);
    void liveNoteOff(int pitch);

    Engine m_engine;
    Timeline* m_timeline;
    Inspector* m_inspector;
    MixerPanel* m_mixer;
    InstrumentPanel* m_smart;
    DrummerPanel* m_drummer;
    PianoRoll* m_roll;
    StepSequencer* m_seq;
    LoopBrowser* m_loops;
    MidiInput* m_midi;
    QTabWidget* m_bottom;
    QSplitter *m_hsplit, *m_vsplit;

    QAction *aNew, *aOpen, *aSave, *aSaveAs, *aImport, *aImportMidi, *aExportMidi, *aBounce, *aPrefs, *aQuit;
    QAction *aUndo, *aRedo, *aDelete, *aDuplicate, *aSplit, *aSnap, *aAutomation, *aCut, *aCopy, *aPaste, *aDuplicateTrack, *aAddMarker, *aNextMarker, *aPrevMarker, *aTapTempo, *aMoveUp, *aMoveDown, *aMuteRegion;
    QAction *aNewAudio, *aNewInstrument, *aNewDrums, *aNewDrummer, *aDeleteTrack, *aTempoTrack, *aMeter;
    QAction *aRewind, *aPlay, *aStop, *aRecord, *aCycle, *aMetronome, *aCountIn;
    QAction *aInspector, *aMixer, *aSmart, *aRoll, *aSteps, *aDrummerTab, *aLoopBrowser, *aEditors, *aTyping, *aAbout;

    QLabel *m_lcdPos, *m_lcdTime, *m_keysLabel, *m_audioLabel;
    QToolButton* m_sigButton;
    QDoubleSpinBox* m_bpm;
    QTimer m_timer, m_autosaveTimer;
    QMenu* m_recentMenu = nullptr;
    std::vector<qint64> m_taps;   // tap tempo: timestamps in ms

    void checkRecovery();
    void addRecent(const QString& path);
    void updateRecentMenu();

    int m_selTrack = -1, m_selRegion = -1;
    int m_octave = 0;                                   // musical typing octave offset
    bool m_sustain = false;                             // MIDI sustain pedal
    std::map<int, std::pair<int, int>> m_heldKeys;      // key -> (track, pitch)
    std::set<std::pair<int, int>> m_sustained;          // notes kept alive by the pedal
    QString m_lastTitle;
};
