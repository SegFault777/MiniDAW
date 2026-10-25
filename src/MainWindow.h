// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QMainWindow>
#include <QTimer>
#include <map>
#include "Engine.h"

class Timeline;
class Inspector;
class MixerPanel;
class SynthPanel;
class PianoRoll;
class StepSequencer;
class QTabWidget;
class QSplitter;
class QDoubleSpinBox;
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
    void buildActions();
    void buildMenus();
    QWidget* buildControlBar();
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
    void about();
    int musicalTrack() const;

    Engine m_engine;
    Timeline* m_timeline;
    Inspector* m_inspector;
    MixerPanel* m_mixer;
    SynthPanel* m_synth;
    PianoRoll* m_roll;
    StepSequencer* m_seq;
    QTabWidget* m_bottom;
    QSplitter *m_hsplit, *m_vsplit;

    QAction *aNew, *aOpen, *aSave, *aSaveAs, *aImport, *aBounce, *aQuit;
    QAction *aUndo, *aRedo, *aDelete, *aDuplicate, *aSplit, *aSnap, *aAutomation;
    QAction *aNewAudio, *aNewInstrument, *aNewDrums, *aDeleteTrack;
    QAction *aRewind, *aPlay, *aStop, *aRecord, *aCycle, *aMetronome, *aCountIn;
    QAction *aInspector, *aMixer, *aSmart, *aRoll, *aSteps, *aEditors, *aTyping, *aAbout;

    QLabel *m_lcdPos, *m_lcdTime, *m_keysLabel;
    QDoubleSpinBox* m_bpm;
    QTimer m_timer;

    int m_selTrack = -1;
    int m_octave = 0;                                   // musical typing octave offset
    std::map<int, std::pair<int, int>> m_heldKeys;      // key -> (track, pitch)
    QString m_lastTitle;
};
