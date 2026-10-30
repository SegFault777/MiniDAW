// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"

#include <QAbstractSpinBox>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>
#include "Browser.h"
#include "MidiInput.h"
#include "Panels.h"
#include "PianoRoll.h"
#include "Preferences.h"
#include "Theme.h"
#include "Timeline.h"

#ifndef MINIDAW_VERSION
#define MINIDAW_VERSION "0.6"
#endif

namespace {
constexpr const char* kFileFilter = "MiniDAW Project (*.mdaw)";

/// Semitone offset of a musical-typing key relative to the base note, or -1.
int noteForKey(int key) {
    switch (key) {
    case Qt::Key_A: return 0;   case Qt::Key_W: return 1;   case Qt::Key_S: return 2;
    case Qt::Key_E: return 3;   case Qt::Key_D: return 4;   case Qt::Key_F: return 5;
    case Qt::Key_T: return 6;   case Qt::Key_G: return 7;   case Qt::Key_Y: return 8;
    case Qt::Key_H: return 9;   case Qt::Key_U: return 10;  case Qt::Key_J: return 11;
    case Qt::Key_K: return 12;  case Qt::Key_O: return 13;  case Qt::Key_L: return 14;
    case Qt::Key_P: return 15;  case Qt::Key_Semicolon: return 16;
    default: return -1;
    }
}
}  // namespace

MainWindow::MainWindow() {
    setWindowTitle("MiniDAW");
    m_timeline = new Timeline(&m_engine);
    m_inspector = new Inspector(&m_engine);
    m_mixer = new MixerPanel(&m_engine);
    m_smart = new InstrumentPanel(&m_engine);
    m_drummer = new DrummerPanel(&m_engine);
    m_roll = new PianoRoll(&m_engine);
    m_seq = new StepSequencer(&m_engine);
    m_loops = new LoopBrowser(&m_engine);
    m_loops->hide();
    m_midi = new MidiInput(this);

    m_bottom = new QTabWidget;
    m_bottom->addTab(m_mixer, tr("Mixer"));
    m_bottom->addTab(m_smart, tr("Smart Controls"));
    m_bottom->addTab(m_roll, tr("Piano Roll"));
    m_bottom->addTab(m_seq, tr("Step Sequencer"));
    m_bottom->addTab(m_drummer, tr("Drummer"));
    m_bottom->setCurrentIndex(TabSmart);

    m_vsplit = new QSplitter(Qt::Vertical);
    m_vsplit->addWidget(m_timeline);
    m_vsplit->addWidget(m_bottom);
    m_vsplit->setSizes({430, 300});
    m_hsplit = new QSplitter(Qt::Horizontal);
    m_hsplit->addWidget(m_inspector);
    m_hsplit->addWidget(m_vsplit);
    m_hsplit->addWidget(m_loops);
    m_hsplit->setSizes({260, 960, 260});
    m_hsplit->setStretchFactor(1, 1);

    buildActions();
    buildMenus();
    auto* central = new QWidget;
    auto* lay = new QVBoxLayout(central);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(buildControlBar());
    lay->addWidget(m_hsplit, 1);
    setCentralWidget(central);

    m_audioLabel = new QLabel;
    m_audioLabel->setStyleSheet("color:#8c8c94");
    m_keysLabel = new QLabel;
    statusBar()->addPermanentWidget(m_keysLabel);
    statusBar()->addPermanentWidget(m_audioLabel);
    statusBar()->showMessage(tr("Space: play · R: record · C: cycle · double-click an instrument lane to create a MIDI region · Ctrl+K: musical typing"));

    connect(&m_engine, &Engine::tracksChanged, this, [this] {
        m_timeline->refreshLayout();
        m_mixer->rebuild();
        m_inspector->refresh();
        m_smart->setTrack(m_selTrack);
    });
    connect(&m_engine, &Engine::regionsChanged, m_timeline, &Timeline::refreshLayout);
    connect(&m_engine, &Engine::tempoChanged, this, [this] { m_timeline->update(); });
    connect(&m_engine, &Engine::loopChanged, this, [this] {
        QSignalBlocker b(aCycle);
        aCycle->setChecked(m_engine.loopOn());
        m_timeline->update();
    });
    connect(&m_engine, &Engine::message, this, [this](const QString& s) { statusBar()->showMessage(s, 9000); });
    connect(&m_engine, &Engine::audioStatusChanged, this, [this](const QString& s) { m_audioLabel->setText(s); });
    connect(m_timeline, &Timeline::selectionChanged, this, &MainWindow::onSelection);
    connect(m_timeline, &Timeline::regionOpened, this, [this](int t, int r) {
        m_roll->setRegion(t, r);
        const auto trks = m_engine.snapshot();
        const bool drummer = t >= 0 && t < int(trks.size()) && r >= 0 && r < int(trks[size_t(t)].regions.size()) && trks[size_t(t)].regions[size_t(r)].drummer;
        showBottom(drummer ? TabDrummer : TabRoll);
    });
    connect(m_mixer, &MixerPanel::trackSelected, m_timeline, &Timeline::selectTrack);
    connect(m_loops, &LoopBrowser::addRequested, this, [this](const QString& path) {
        m_engine.importAudio(path, m_selTrack, m_engine.positionBeats());
    });
    connect(m_midi, &MidiInput::noteOn, this, [this](int p, int v) { liveNoteOn(p, v); });
    connect(m_midi, &MidiInput::noteOff, this, [this](int p) { liveNoteOff(p); });
    connect(m_midi, &MidiInput::controller, this, [this](int cc, int value) {
        if (cc != 64) return;   // sustain pedal
        m_sustain = value >= 64;
        if (!m_sustain) {
            for (const auto& n : m_sustained) m_engine.noteOff(n.first, n.second);
            m_sustained.clear();
        }
    });

    m_mixer->rebuild();
    qApp->installEventFilter(this);   // musical typing needs to see keys before shortcuts do
    connect(&m_timer, &QTimer::timeout, this, &MainWindow::tick);
    m_timer.start(33);
    m_timeline->selectTrack(1);       // the default software instrument
    m_engine.setAutosaveDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/autosave");
    connect(&m_autosaveTimer, &QTimer::timeout, this, [this] {
        if (m_engine.isDirty() && !m_engine.isRecording()) m_engine.autosave();
    });
    QTimer::singleShot(0, this, [this] {   // after the window is visible: recovery question, audio device, MIDI, plug-in scan
        checkRecovery();
        applySettings();
    });
}

/// A leftover lock file means the last session did not end normally: offer to restore the autosave.
void MainWindow::checkRecovery() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/autosave";
    const QString lock = dir + "/running.lock";
    if (QFile::exists(lock) && m_engine.hasAutosave()) {
        const auto r = QMessageBox::question(this, tr("Recover Project"),
                                             tr("MiniDAW did not close properly last time. Restore the last autosaved project?"),
                                             QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (r == QMessageBox::Yes) {
            QString err;
            if (m_engine.loadProject(m_engine.autosavePath(), &err)) {
                m_engine.markRecovered();
                m_timeline->selectTrack(0);
                statusBar()->showMessage(tr("Recovered the autosaved project. Please save it."), 8000);
            } else {
                QMessageBox::warning(this, tr("Recover Project"), err);
            }
        } else {
            m_engine.clearAutosave();
        }
    }
    QDir().mkpath(dir);
    QFile f(lock);
    if (f.open(QIODevice::WriteOnly)) f.write(QByteArray::number(QCoreApplication::applicationPid()));
}

void MainWindow::addRecent(const QString& path) {
    QSettings s;
    QStringList list = s.value("recent/files").toStringList();
    const QString abs = QFileInfo(path).absoluteFilePath();
    list.removeAll(abs);
    list.prepend(abs);
    while (list.size() > 8) list.removeLast();
    s.setValue("recent/files", list);
    updateRecentMenu();
}

void MainWindow::updateRecentMenu() {
    if (!m_recentMenu) return;
    m_recentMenu->clear();
    QStringList list = QSettings().value("recent/files").toStringList();
    list.erase(std::remove_if(list.begin(), list.end(), [](const QString& p) { return !QFileInfo::exists(p); }), list.end());
    for (const QString& p : list) {
        QAction* a = m_recentMenu->addAction(QFileInfo(p).fileName());
        a->setToolTip(p);
        connect(a, &QAction::triggered, this, [this, p] {
            if (maybeSave()) openProject(p);
        });
    }
    m_recentMenu->setEnabled(!list.isEmpty());
    if (!list.isEmpty()) {
        m_recentMenu->addSeparator();
        connect(m_recentMenu->addAction(tr("Clear Menu")), &QAction::triggered, this, [this] {
            QSettings().remove("recent/files");
            updateRecentMenu();
        });
    }
}

void MainWindow::applySettings() {
    QSettings s;
    const QByteArray dev = QByteArray::fromBase64(s.value("audio/device").toString().toLatin1());
    const int buf = s.value("audio/buffer", 2048).toInt();
    if (!dev.isEmpty() || buf != 2048) m_engine.setAudioOutput(dev, buf);
    m_engine.setAudioInput(QByteArray::fromBase64(s.value("audio/input").toString().toLatin1()), s.value("audio/recOffset", 0).toInt());
    m_loops->setFolder(s.value("loops/folder", LoopBrowser::defaultFolder()).toString());
    const QString midi = s.value("midi/device").toString();
    m_midi->close();
    if (!midi.isEmpty()) {
        const int idx = MidiInput::deviceNames().indexOf(midi);
        if (idx >= 0 && !m_midi->open(idx)) statusBar()->showMessage(tr("Could not open the MIDI input %1").arg(midi), 6000);
    }
    if (vst3::available() && PluginCatalog::list().empty()) {
        PluginCatalog::rescan();
        m_inspector->refresh();
    }
    m_audioLabel->setText(m_engine.audioStatus());
    const int minutes = s.value("general/autosaveMin", 3).toInt();
    if (minutes > 0) m_autosaveTimer.start(minutes * 60 * 1000);
    else m_autosaveTimer.stop();
}

// ------------------------------------------------------------------ actions
void MainWindow::buildActions() {
    auto make = [this](const QString& text, const QKeySequence& ks, bool checkable = false) {
        auto* a = new QAction(text, this);
        if (!ks.isEmpty()) a->setShortcut(ks);
        a->setCheckable(checkable);
        addAction(a);   // makes the shortcut work even when the action is not in a visible menu
        return a;
    };
    aNew = make(tr("New"), QKeySequence::New);
    aOpen = make(tr("Open…"), QKeySequence::Open);
    aSave = make(tr("Save"), QKeySequence::Save);
    aSaveAs = make(tr("Save As…"), QKeySequence::SaveAs);
    aImport = make(tr("Import Audio…"), QKeySequence(Qt::CTRL | Qt::Key_I));
    aImportMidi = make(tr("Import MIDI File…"), {});
    aExportMidi = make(tr("Export MIDI File…"), {});
    aBounce = make(tr("Bounce to WAV…"), QKeySequence(Qt::CTRL | Qt::Key_B));
    aPrefs = make(tr("Preferences…"), QKeySequence(Qt::CTRL | Qt::Key_Comma));
    aQuit = make(tr("Quit"), QKeySequence::Quit);
    aUndo = make(tr("Undo"), QKeySequence::Undo);
    aRedo = make(tr("Redo"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z));
    aCut = make(tr("Cut"), QKeySequence::Cut);
    aCopy = make(tr("Copy"), QKeySequence::Copy);
    aPaste = make(tr("Paste"), QKeySequence::Paste);
    aDelete = make(tr("Delete"), QKeySequence(Qt::Key_Delete));
    aDuplicate = make(tr("Duplicate Region"), QKeySequence(Qt::CTRL | Qt::Key_D));
    aSplit = make(tr("Split at Playhead"), QKeySequence(Qt::CTRL | Qt::Key_T));
    aSnap = make(tr("Snap to Grid"), {}, true);
    aSnap->setChecked(true);
    aAutomation = make(tr("Automation"), QKeySequence(Qt::Key_A), true);
    aNewAudio = make(tr("New Audio Track"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_A));
    aNewInstrument = make(tr("New Software Instrument Track"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S));
    aNewDrums = make(tr("New Drum Machine Track"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_D));
    aNewDrummer = make(tr("New Drummer Track"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_R));
    aDuplicateTrack = make(tr("Duplicate Track"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D));
    aDeleteTrack = make(tr("Delete Track"), QKeySequence(Qt::CTRL | Qt::Key_Backspace));
    aTempoTrack = make(tr("Show Tempo Track"), QKeySequence(Qt::Key_G), true);
    aMeter = make(tr("Time Signature…"), {});

    aTapTempo = make(tr("Tap Tempo"), QKeySequence(Qt::SHIFT | Qt::Key_T));
    aMoveUp = make(tr("Move Track Up"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Up));
    aMoveDown = make(tr("Move Track Down"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Down));
    aMuteRegion = make(tr("Mute / Unmute Region"), QKeySequence(Qt::CTRL | Qt::Key_M));
    aAddMarker = make(tr("Add Marker"), QKeySequence(Qt::Key_M));
    aNextMarker = make(tr("Next Marker"), QKeySequence(Qt::ALT | Qt::Key_Right));
    aPrevMarker = make(tr("Previous Marker"), QKeySequence(Qt::ALT | Qt::Key_Left));
    aRewind = make(tr("Go to Beginning"), QKeySequence(Qt::Key_Return));
    aPlay = make(tr("Play / Pause"), QKeySequence(Qt::Key_Space));
    aStop = make(tr("Stop"), QKeySequence(Qt::Key_Period));
    aRecord = make(tr("Record"), QKeySequence(Qt::Key_R), true);
    aCycle = make(tr("Cycle"), QKeySequence(Qt::Key_C), true);
    aMetronome = make(tr("Metronome Click"), QKeySequence(Qt::Key_K), true);
    aCountIn = make(tr("Count-in (1 bar)"), {}, true);
    aRewind->setIcon(theme::icon(theme::Glyph::Rewind, theme::kText, theme::kText));
    aPlay->setIcon(theme::icon(theme::Glyph::Play, theme::kGreen, theme::kGreen));
    aStop->setIcon(theme::icon(theme::Glyph::Stop, theme::kText, theme::kText));
    aRecord->setIcon(theme::icon(theme::Glyph::Record, QColor(200, 70, 70), theme::kRed));
    aCycle->setIcon(theme::icon(theme::Glyph::Cycle, theme::kText, theme::kYellow));
    aMetronome->setIcon(theme::icon(theme::Glyph::Metronome, theme::kText, theme::kAccent));
    aCountIn->setIcon(theme::icon(theme::Glyph::CountIn, theme::kText, theme::kAccent));

    aInspector = make(tr("Inspector"), QKeySequence(Qt::Key_I), true);
    aInspector->setChecked(true);
    aMixer = make(tr("Mixer"), QKeySequence(Qt::Key_X));
    aSmart = make(tr("Smart Controls"), QKeySequence(Qt::Key_B));
    aRoll = make(tr("Piano Roll"), QKeySequence(Qt::Key_P));
    aSteps = make(tr("Step Sequencer"), QKeySequence(Qt::SHIFT | Qt::Key_S));
    aDrummerTab = make(tr("Drummer Editor"), QKeySequence(Qt::SHIFT | Qt::Key_D));
    aLoopBrowser = make(tr("Loop Browser"), QKeySequence(Qt::Key_O), true);
    aEditors = make(tr("Show / Hide Editors"), QKeySequence(Qt::Key_E));
    aTyping = make(tr("Musical Typing"), QKeySequence(Qt::CTRL | Qt::Key_K), true);
    aAbout = make(tr("About MiniDAW"), {});

    connect(aNew, &QAction::triggered, this, &MainWindow::newProject);
    connect(aOpen, &QAction::triggered, this, [this] {
        if (!maybeSave()) return;
        const QString p = QFileDialog::getOpenFileName(this, tr("Open Project"), {}, kFileFilter);
        if (!p.isEmpty()) openProject(p);
    });
    connect(aSave, &QAction::triggered, this, [this] { saveProject(false); });
    connect(aSaveAs, &QAction::triggered, this, [this] { saveProject(true); });
    connect(aImport, &QAction::triggered, this, &MainWindow::importDialog);
    connect(aImportMidi, &QAction::triggered, this, [this] {
        const QString p = QFileDialog::getOpenFileName(this, tr("Import MIDI File"), {}, tr("MIDI files (*.mid *.midi)"));
        QString err;
        if (!p.isEmpty() && !m_engine.importMidiFile(p, &err)) QMessageBox::warning(this, tr("Import MIDI File"), err);
    });
    connect(aExportMidi, &QAction::triggered, this, [this] {
        const QString p = QFileDialog::getSaveFileName(this, tr("Export MIDI File"), "export.mid", tr("MIDI files (*.mid)"));
        QString err;
        if (!p.isEmpty() && !m_engine.exportMidiFile(p, &err)) QMessageBox::warning(this, tr("Export MIDI File"), err);
    });
    connect(aBounce, &QAction::triggered, this, &MainWindow::exportDialog);
    connect(aPrefs, &QAction::triggered, this, &MainWindow::preferences);
    connect(aQuit, &QAction::triggered, this, &QWidget::close);
    connect(aUndo, &QAction::triggered, this, [this] { m_engine.undo(); });
    connect(aRedo, &QAction::triggered, this, [this] { m_engine.redo(); });
    connect(aCut, &QAction::triggered, this, [this] { m_timeline->cutSelected(); });
    connect(aCopy, &QAction::triggered, this, [this] { m_timeline->copySelected(); });
    connect(aPaste, &QAction::triggered, this, [this] { m_timeline->pasteAtPlayhead(); });
    connect(aDuplicateTrack, &QAction::triggered, this, [this] { m_timeline->duplicateSelectedTrack(); });
    connect(aDelete, &QAction::triggered, this, &MainWindow::onDelete);
    connect(aDuplicate, &QAction::triggered, this, [this] { m_timeline->duplicateSelected(); });
    connect(aSplit, &QAction::triggered, this, [this] { m_timeline->splitAtPlayhead(); });
    connect(aSnap, &QAction::toggled, this, [this](bool on) { m_timeline->setSnap(on); });
    connect(aAutomation, &QAction::toggled, this, [this](bool on) { m_timeline->setAutomationMode(on); });
    connect(aNewAudio, &QAction::triggered, this, [this] { m_timeline->selectTrack(m_engine.addTrack(TrackType::Audio)); });
    connect(aNewInstrument, &QAction::triggered, this, [this] { m_timeline->selectTrack(m_engine.addTrack(TrackType::Instrument)); });
    connect(aNewDrums, &QAction::triggered, this, [this] { m_timeline->selectTrack(m_engine.addTrack(TrackType::Drums)); });
    connect(aNewDrummer, &QAction::triggered, this, [this] { m_timeline->selectTrack(m_engine.addTrack(TrackType::Drummer)); });
    connect(aDeleteTrack, &QAction::triggered, this, [this] {
        if (m_selTrack >= 0) { const int t = m_selTrack; m_timeline->selectTrack(-1); m_engine.removeTrack(t); }
    });
    connect(aTempoTrack, &QAction::toggled, this, [this](bool on) { m_timeline->showTempoTrack(on); });
    connect(aMeter, &QAction::triggered, this, &MainWindow::meterDialog);

    connect(aTapTempo, &QAction::triggered, this, [this] {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (!m_taps.empty() && now - m_taps.back() > 2000) m_taps.clear();   // a pause starts a new measurement
        m_taps.push_back(now);
        if (m_taps.size() > 5) m_taps.erase(m_taps.begin());
        if (m_taps.size() < 2) return;
        const double avg = double(m_taps.back() - m_taps.front()) / double(m_taps.size() - 1);
        const double bpm = std::clamp(std::round(60000.0 / std::max(avg, 1.0) * 10.0) / 10.0, 20.0, 300.0);
        m_engine.setBpm(bpm);
        m_timeline->refreshLayout();
        statusBar()->showMessage(tr("Tempo: %1 BPM").arg(bpm, 0, 'f', 1), 2000);
    });
    connect(aMoveUp, &QAction::triggered, this, [this] {
        const int t = m_timeline->selectedTrack();
        if (t > 0) { m_engine.moveTrack(t, t - 1); m_timeline->selectTrack(t - 1); }
    });
    connect(aMoveDown, &QAction::triggered, this, [this] {
        const int t = m_timeline->selectedTrack();
        if (t >= 0 && t + 1 < m_engine.trackCount()) { m_engine.moveTrack(t, t + 1); m_timeline->selectTrack(t + 1); }
    });
    connect(aMuteRegion, &QAction::triggered, this, [this] { m_timeline->toggleMuteSelected(); });
    connect(aAddMarker, &QAction::triggered, this, [this] { m_engine.addMarker(m_engine.positionBeats()); });
    connect(aNextMarker, &QAction::triggered, this, [this] {
        const double p = m_engine.positionBeats();
        for (const Marker& m : m_engine.markers())
            if (m.beat > p + 1e-6) { m_engine.seekBeats(m.beat); break; }
    });
    connect(aPrevMarker, &QAction::triggered, this, [this] {
        const double p = m_engine.positionBeats();
        const auto marks = m_engine.markers();
        for (auto it = marks.rbegin(); it != marks.rend(); ++it)
            if (it->beat < p - 1e-6) { m_engine.seekBeats(it->beat); break; }
    });
    connect(aRewind, &QAction::triggered, this, [this] { m_engine.seekBeats(m_engine.loopOn() ? m_engine.loopA() : 0.0); });
    connect(aPlay, &QAction::triggered, this, [this] { m_engine.isPlaying() ? m_engine.pause() : m_engine.play(); });
    connect(aStop, &QAction::triggered, this, [this] { m_engine.stop(); });
    connect(aRecord, &QAction::triggered, this, &MainWindow::onRecord);
    connect(aCycle, &QAction::toggled, this, [this](bool on) { m_engine.setLoop(on); });
    connect(aMetronome, &QAction::toggled, this, [this](bool on) { m_engine.setMetronome(on); });
    connect(aCountIn, &QAction::toggled, this, [this](bool on) { m_engine.setCountIn(on); });

    connect(aInspector, &QAction::toggled, this, [this](bool on) { m_inspector->setVisible(on); });
    connect(aMixer, &QAction::triggered, this, [this] { showBottom(TabMixer); });
    connect(aSmart, &QAction::triggered, this, [this] { showBottom(TabSmart); });
    connect(aRoll, &QAction::triggered, this, [this] { showBottom(TabRoll); });
    connect(aSteps, &QAction::triggered, this, [this] { showBottom(TabSteps); });
    connect(aDrummerTab, &QAction::triggered, this, [this] { showBottom(TabDrummer); });
    connect(aLoopBrowser, &QAction::toggled, this, [this](bool on) { m_loops->setVisible(on); });
    connect(aEditors, &QAction::triggered, this, [this] { m_bottom->setVisible(!m_bottom->isVisible()); });
    connect(aTyping, &QAction::toggled, this, [this](bool on) {
        m_keysLabel->setText(on ? tr("KEYS  C%1").arg(3 + m_octave) : QString());
        if (!on) {
            for (auto& [key, tp] : m_heldKeys) m_engine.noteOff(tp.first, tp.second);
            m_heldKeys.clear();
        }
    });
    connect(aAbout, &QAction::triggered, this, &MainWindow::about);
}

void MainWindow::buildMenus() {
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addActions({aNew, aOpen});
    m_recentMenu = file->addMenu(tr("Open Recent"));
    updateRecentMenu();
    file->addActions({aSave, aSaveAs});
    file->addSeparator();
    file->addActions({aImport, aImportMidi, aExportMidi, aBounce});
    file->addSeparator();
    file->addActions({aPrefs, aQuit});

    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    edit->addActions({aUndo, aRedo});
    edit->addSeparator();
    edit->addActions({aCut, aCopy, aPaste, aMuteRegion});
    edit->addSeparator();
    edit->addActions({aDelete, aDuplicate, aSplit});
    edit->addSeparator();
    edit->addActions({aSnap, aAutomation});
    {   // which parameter the automation lanes edit
        QMenu* am = edit->addMenu(tr("Automation Parameter"));
        auto* grp = new QActionGroup(this);
        const char* names[4] = {QT_TR_NOOP("Volume"), QT_TR_NOOP("Pan"), QT_TR_NOOP("Send A"), QT_TR_NOOP("Send B")};
        for (int k = 0; k < 4; ++k) {
            QAction* a = am->addAction(tr(names[k]));
            a->setCheckable(true);
            a->setChecked(k == 0);
            grp->addAction(a);
            connect(a, &QAction::triggered, this, [this, k] {
                m_timeline->setAutoParam(AutoParam(k));
                if (!aAutomation->isChecked()) aAutomation->setChecked(true);
            });
        }
    }

    QMenu* track = menuBar()->addMenu(tr("&Track"));
    track->addActions({aNewAudio, aNewInstrument, aNewDrummer, aNewDrums});
    track->addSeparator();
    track->addActions({aDuplicateTrack, aMoveUp, aMoveDown, aDeleteTrack});

    QMenu* transport = menuBar()->addMenu(tr("T&ransport"));
    transport->addActions({aPlay, aStop, aRecord, aRewind});
    transport->addSeparator();
    transport->addActions({aAddMarker, aPrevMarker, aNextMarker});
    transport->addSeparator();
    transport->addAction(aTapTempo);
    transport->addSeparator();
    transport->addActions({aCycle, aMetronome, aCountIn});
    transport->addSeparator();
    transport->addActions({aTempoTrack, aMeter});

    QMenu* view = menuBar()->addMenu(tr("&View"));
    view->addActions({aInspector, aLoopBrowser, aMixer, aSmart, aRoll, aSteps, aDrummerTab, aEditors});
    view->addSeparator();
    view->addAction(aTyping);

    menuBar()->addMenu(tr("&Help"))->addAction(aAbout);
}

QWidget* MainWindow::buildControlBar() {
    auto* bar = new QFrame;
    bar->setObjectName("ctrl");
    bar->setStyleSheet("QFrame#ctrl{background:#232326;border-bottom:1px solid #151517}");
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(10, 4, 10, 4);
    h->setSpacing(4);
    auto tool = [&](QAction* a) {
        auto* b = new QToolButton;
        b->setDefaultAction(a);
        b->setIconSize(QSize(24, 24));
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setToolTip(a->text() + (a->shortcut().isEmpty() ? QString() : "  (" + a->shortcut().toString() + ")"));
        h->addWidget(b);
    };
    h->addStretch(1);
    tool(aRewind);
    tool(aStop);
    tool(aPlay);
    tool(aRecord);
    tool(aCycle);
    h->addSpacing(10);

    // LCD display like Logic's: position (bar beat division tick), time, tempo and time signature.
    auto* lcd = new QFrame;
    lcd->setStyleSheet("QFrame{background:#101214;border:1px solid #050506;border-radius:7px}"
                       "QLabel{border:none;color:#e8f0ff;font-family:Consolas,monospace;font-size:20px}"
                       "QToolButton{border:none;color:#e8f0ff;font-family:Consolas,monospace;font-size:18px;padding:0 6px}");
    auto* lh = new QHBoxLayout(lcd);
    lh->setContentsMargins(14, 3, 14, 3);
    m_lcdPos = new QLabel("  1 1 1 000");
    m_lcdTime = new QLabel("00:00:00.000");
    m_lcdTime->setStyleSheet("color:#7f93b3;font-size:15px");
    auto* sep1 = new QFrame;
    sep1->setFrameShape(QFrame::VLine);
    sep1->setStyleSheet("color:#2a2d33");
    auto* sep2 = new QFrame;
    sep2->setFrameShape(QFrame::VLine);
    sep2->setStyleSheet("color:#2a2d33");
    m_bpm = new QDoubleSpinBox;
    m_bpm->setRange(20, 400);
    m_bpm->setDecimals(2);
    m_bpm->setValue(m_engine.bpm());
    m_bpm->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_bpm->setFixedWidth(88);
    m_bpm->setToolTip(tr("Tempo at the playhead"));
    m_bpm->setStyleSheet("QDoubleSpinBox{background:transparent;border:none;color:#e8f0ff;font-family:Consolas,monospace;font-size:18px}");
    connect(m_bpm, &QDoubleSpinBox::editingFinished, this, [this] {
        if (std::fabs(m_bpm->value() - m_engine.bpm()) > 0.005) { m_engine.setBpm(m_bpm->value()); m_timeline->refreshLayout(); }
    });
    m_sigButton = new QToolButton;
    m_sigButton->setText("4/4");
    m_sigButton->setToolTip(tr("Time Signature…"));
    connect(m_sigButton, &QToolButton::clicked, this, &MainWindow::meterDialog);
    lh->addWidget(m_lcdPos);
    lh->addSpacing(6);
    lh->addWidget(m_lcdTime);
    lh->addWidget(sep1);
    lh->addWidget(m_bpm);
    lh->addWidget(sep2);
    lh->addWidget(m_sigButton);
    h->addWidget(lcd);
    h->addSpacing(10);
    tool(aMetronome);
    tool(aCountIn);
    h->addStretch(1);
    return bar;
}

// ------------------------------------------------------------------ behaviour
void MainWindow::tick() {
    const double beat = m_engine.positionBeats();
    const auto tm = m_engine.tempoMap();
    const int bar = tm->barAt(beat);
    const double inBar = beat - tm->barStart(bar), unit = 4.0 / tm->meterAtBeat(beat).den;
    const double sub = (inBar / unit - std::floor(inBar / unit)) * 4.0;   // sixteenth-note position inside the beat
    m_lcdPos->setText(QString::asprintf("%3d %d %d %03d", bar, int(inBar / unit) + 1, int(sub) + 1, int((sub - std::floor(sub)) * 240)));
    const double sec = tm->beatToFrame(beat) / kSampleRate;
    m_lcdTime->setText(QString::asprintf("%02d:%02d:%02d.%03d", int(sec) / 3600, int(sec) / 60 % 60, int(sec) % 60, int((sec - std::floor(sec)) * 1000)));
    const dsp::MeterPoint mp = tm->meterAtBeat(beat);
    const QString sig = QString("%1/%2").arg(mp.num).arg(mp.den);
    if (m_sigButton->text() != sig) m_sigButton->setText(sig);
    {
        QSignalBlocker b(aRecord);
        aRecord->setChecked(m_engine.isRecording());
    }
    if (!m_bpm->hasFocus() && std::fabs(m_bpm->value() - tm->bpmAt(beat)) > 0.004) {
        QSignalBlocker b(m_bpm);
        m_bpm->setValue(tm->bpmAt(beat));
    }
    if (m_engine.isPlaying()) m_timeline->followPlayhead();
    m_timeline->update();
    m_inspector->tick();
    if (m_bottom->isVisible()) {
        switch (m_bottom->currentIndex()) {
        case TabMixer: m_mixer->tick(); break;
        case TabRoll: m_roll->update(); break;
        case TabSteps: m_seq->update(); break;
        default: break;
        }
    }
    const QString name = m_engine.projectPath().isEmpty() ? tr("Untitled") : QFileInfo(m_engine.projectPath()).completeBaseName();
    const QString title = name + (m_engine.isDirty() ? " *" : "") + " — MiniDAW";
    if (title != m_lastTitle) { setWindowTitle(title); m_lastTitle = title; }
}

void MainWindow::onSelection(int track, int region) {
    m_selTrack = track;
    m_selRegion = region;
    m_inspector->setSelection(track, region);
    m_smart->setTrack(track);
    m_seq->setTrack(track);
    const auto trks = m_engine.snapshot();
    if (track < 0 || track >= int(trks.size())) return;
    const Track& t = trks[size_t(track)];
    if (region >= 0 && region < int(t.regions.size())) {
        if (t.regions[size_t(region)].midi) m_roll->setRegion(track, region);
        m_drummer->setRegion(track, region);
    } else {
        m_drummer->setRegion(-1, -1);
    }
    if (t.type == TrackType::Drums) showBottom(TabSteps);
    else if (t.type == TrackType::Drummer && region >= 0 && t.regions[size_t(region)].drummer) showBottom(TabDrummer);
    else if (m_bottom->currentIndex() == TabSteps || m_bottom->currentIndex() == TabDrummer) showBottom(TabSmart);
}

void MainWindow::onRecord() {
    if (m_engine.isRecording()) {
        m_engine.stopRecording();
        return;
    }
    const auto trks = m_engine.snapshot();
    const bool anyArmed = std::any_of(trks.begin(), trks.end(), [](const Track& t) { return t.armed; });
    if (!anyArmed && m_selTrack >= 0 && m_selTrack < int(trks.size()) && trks[size_t(m_selTrack)].type != TrackType::Drums)
        m_engine.setArmed(m_selTrack, true);   // like Logic: record into the selected track
    m_engine.startRecording();
}

void MainWindow::onDelete() {
    if (m_roll->hasSelectedNote() && m_roll->isAncestorOf(QApplication::focusWidget())) m_roll->deleteSelectedNote();
    else m_timeline->deleteSelected();
}

void MainWindow::showBottom(int tab) {
    m_bottom->setVisible(true);
    m_bottom->setCurrentIndex(tab);
}

int MainWindow::musicalTrack() const {
    const auto trks = m_engine.snapshot();
    auto plays = [&](size_t i) { return trks[i].instrument != nullptr; };
    if (m_selTrack >= 0 && m_selTrack < int(trks.size()) && plays(size_t(m_selTrack))) return m_selTrack;
    for (size_t i = 0; i < trks.size(); ++i)
        if (plays(i) && trks[i].armed) return int(i);
    for (size_t i = 0; i < trks.size(); ++i)
        if (trks[i].type == TrackType::Instrument) return int(i);
    return -1;
}

void MainWindow::liveNoteOn(int pitch, int velocity) {
    const int t = musicalTrack();
    if (t < 0) return;
    m_sustained.erase({t, pitch});
    m_engine.noteOn(t, pitch, velocity);
}

void MainWindow::liveNoteOff(int pitch) {
    const int t = musicalTrack();
    if (t < 0) return;
    if (m_sustain) m_sustained.insert({t, pitch});   // the pedal keeps the note sounding
    else m_engine.noteOff(t, pitch);
}

bool MainWindow::eventFilter(QObject* obj, QEvent* ev) {
    if (!aTyping->isChecked() || !isActiveWindow()) return QMainWindow::eventFilter(obj, ev);
    const QEvent::Type type = ev->type();
    if (type != QEvent::KeyPress && type != QEvent::KeyRelease && type != QEvent::ShortcutOverride)
        return QMainWindow::eventFilter(obj, ev);
    auto* ke = static_cast<QKeyEvent*>(ev);
    QWidget* fw = QApplication::focusWidget();
    if ((ke->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) || qobject_cast<QLineEdit*>(fw) ||
        qobject_cast<QAbstractSpinBox*>(fw))
        return QMainWindow::eventFilter(obj, ev);
    const int semitone = noteForKey(ke->key());
    const bool octaveKey = ke->key() == Qt::Key_Z || ke->key() == Qt::Key_X;
    if (semitone < 0 && !octaveKey) return QMainWindow::eventFilter(obj, ev);

    if (type == QEvent::ShortcutOverride) {   // stop single-letter shortcuts from stealing note keys
        ev->accept();
        return true;
    }
    if (ke->isAutoRepeat()) return true;
    if (octaveKey) {
        if (type == QEvent::KeyPress) {
            m_octave = std::clamp(m_octave + (ke->key() == Qt::Key_X ? 1 : -1), -3, 3);
            m_keysLabel->setText(tr("KEYS  C%1").arg(3 + m_octave));
        }
        return true;
    }
    if (type == QEvent::KeyPress) {
        const int track = musicalTrack();
        if (track < 0) return true;
        const int pitch = std::clamp(60 + 12 * m_octave + semitone, 0, 127);
        m_heldKeys[ke->key()] = {track, pitch};
        m_engine.noteOn(track, pitch, 100);
    } else {
        const auto it = m_heldKeys.find(ke->key());
        if (it != m_heldKeys.end()) {
            m_engine.noteOff(it->second.first, it->second.second);
            m_heldKeys.erase(it);
        }
    }
    return true;
}

// ------------------------------------------------------------------ dialogs and files
void MainWindow::meterDialog() {
    const auto tm = m_engine.tempoMap();
    const int bar = tm->barAt(m_engine.positionBeats());
    const dsp::MeterPoint cur = tm->meterAtBeat(m_engine.positionBeats());
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Time Signature"));
    auto* form = new QFormLayout(&dlg);
    auto* num = new QSpinBox;
    num->setRange(1, 32);
    num->setValue(cur.num);
    auto* den = new QComboBox;
    for (int d : {1, 2, 4, 8, 16, 32}) den->addItem(QString::number(d), d);
    den->setCurrentIndex(std::max(0, den->findData(cur.den)));
    auto* from = new QSpinBox;
    from->setRange(1, 9999);
    from->setValue(bar);
    form->addRow(tr("Beats per bar"), num);
    form->addRow(tr("Note value"), den);
    form->addRow(tr("Starting at bar"), from);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    auto pts = tm->meterPoints();
    pts.erase(std::remove_if(pts.begin(), pts.end(), [&](const dsp::MeterPoint& m) { return m.bar == from->value(); }), pts.end());
    pts.push_back({from->value(), num->value(), den->currentData().toInt()});
    m_engine.setMeterPoints(pts);
}

void MainWindow::preferences() {
    PreferencesDialog dlg(&m_engine, this);
    if (dlg.exec() != QDialog::Accepted) return;
    applySettings();
    if (dlg.languageChanged()) QMessageBox::information(this, tr("Preferences"), tr("Restart MiniDAW to apply the new language."));
}

bool MainWindow::maybeSave() {
    if (!m_engine.isDirty()) return true;
    const auto r = QMessageBox::question(this, tr("Unsaved changes"), tr("Save changes to the project before continuing?"),
                                         QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (r == QMessageBox::Cancel) return false;
    return r == QMessageBox::Discard || saveProject(false);
}

void MainWindow::newProject() {
    if (!maybeSave()) return;
    m_engine.newProject();
    m_timeline->selectTrack(1);
}

bool MainWindow::openProject(const QString& path) {
    QString err;
    if (!m_engine.loadProject(path, &err)) {
        QMessageBox::warning(this, tr("Open Project"), err);
        return false;
    }
    m_timeline->selectTrack(0);
    addRecent(path);
    return true;
}

bool MainWindow::saveProject(bool saveAs) {
    QString path = m_engine.projectPath();
    if (path.isEmpty() || saveAs) {
        path = QFileDialog::getSaveFileName(this, tr("Save Project"), "Untitled.mdaw", kFileFilter);
        if (path.isEmpty()) return false;
        if (!path.endsWith(".mdaw", Qt::CaseInsensitive)) path += ".mdaw";
    }
    QString err;
    if (!m_engine.saveProject(path, &err)) {
        QMessageBox::warning(this, tr("Save Project"), err);
        return false;
    }
    statusBar()->showMessage(tr("Saved %1").arg(path), 4000);
    addRecent(path);
    return true;
}

void MainWindow::importDialog() {
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Import Audio"), {},
                                                            tr("Audio (*.wav *.mp3 *.flac *.ogg *.m4a *.aac);;All files (*)"));
    for (const QString& f : files) m_engine.importAudio(f, -1, m_engine.positionBeats());
}

void MainWindow::exportDialog() {
    m_engine.pause();
    QSettings s;
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Bounce"));
    auto* form = new QFormLayout(&dlg);
    auto* range = new QComboBox;
    range->addItem(tr("Whole project"), false);
    range->addItem(tr("Cycle range"), true);
    range->setCurrentIndex(m_engine.loopOn() ? 1 : s.value("export/cycle", 0).toInt() == 1 ? 1 : 0);
    auto* format = new QComboBox;
    format->addItem(tr("16-bit PCM"), int(dsp::WavFormat::Pcm16));
    format->addItem(tr("24-bit PCM"), int(dsp::WavFormat::Pcm24));
    format->addItem(tr("32-bit float"), int(dsp::WavFormat::Float32));
    format->setCurrentIndex(std::clamp(s.value("export/format", 0).toInt(), 0, 2));
    auto* dither = new QCheckBox(tr("Dither (16-bit)"));
    dither->setChecked(s.value("export/dither", true).toBool());
    auto* normalize = new QCheckBox(tr("Normalize to"));
    normalize->setChecked(s.value("export/normalize", false).toBool());
    auto* level = new QDoubleSpinBox;
    level->setRange(-12.0, 0.0);
    level->setDecimals(1);
    level->setSingleStep(0.5);
    level->setSuffix(" dBFS");
    level->setValue(s.value("export/level", -1.0).toDouble());
    auto* normRow = new QHBoxLayout;
    normRow->addWidget(normalize);
    normRow->addWidget(level);
    normRow->addStretch(1);
    auto* tail = new QCheckBox(tr("Add reverb / echo tail (2 s)"));
    tail->setChecked(s.value("export/tail", true).toBool());
    form->addRow(tr("Range"), range);
    form->addRow(tr("Format"), format);
    form->addRow(QString(), dither);
    form->addRow(QString(), normRow);
    form->addRow(QString(), tail);
    auto sync = [=] {
        dither->setEnabled(format->currentData().toInt() == int(dsp::WavFormat::Pcm16));
        level->setEnabled(normalize->isChecked());
    };
    connect(format, QOverload<int>::of(&QComboBox::currentIndexChanged), &dlg, sync);
    connect(normalize, &QCheckBox::toggled, &dlg, sync);
    sync();
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText(tr("Bounce"));
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;

    s.setValue("export/cycle", range->currentIndex());
    s.setValue("export/format", format->currentIndex());
    s.setValue("export/dither", dither->isChecked());
    s.setValue("export/normalize", normalize->isChecked());
    s.setValue("export/level", level->value());
    s.setValue("export/tail", tail->isChecked());
    ExportOptions opt;
    opt.cycleOnly = range->currentData().toBool();
    opt.format = dsp::WavFormat(format->currentData().toInt());
    opt.dither = dither->isChecked() && opt.format == dsp::WavFormat::Pcm16;
    opt.normalize = normalize->isChecked();
    opt.normalizeDb = float(level->value());
    opt.tail = tail->isChecked();

    const QString path = QFileDialog::getSaveFileName(this, tr("Bounce to WAV"), "bounce.wav", "WAV (*.wav)");
    if (path.isEmpty()) return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString err;
    const bool ok = m_engine.exportAudio(path, opt, &err);
    QApplication::restoreOverrideCursor();
    statusBar()->showMessage(ok ? tr("Bounced to %1").arg(path) : tr("Bounce failed: %1").arg(err), 8000);
}

void MainWindow::about() {
    QMessageBox::about(this, tr("About MiniDAW"),
        tr("<h3>MiniDAW %1</h3>"
           "<p>A lightweight digital audio workstation inspired by the workflow of Logic Pro.</p>"
           "<p>Copyright &copy; 2026 MiniDAW contributors.<br>"
           "This program is free software: you can redistribute it and/or modify it under the terms of the "
           "GNU General Public License as published by the Free Software Foundation, either version 3 of the "
           "License, or (at your option) any later version. It comes with ABSOLUTELY NO WARRANTY.</p>"
           "<p>MiniDAW is an independent project and is not affiliated with or endorsed by Apple Inc. "
           "VST is a trademark of Steinberg Media Technologies GmbH.</p>")
            .arg(MINIDAW_VERSION));
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (maybeSave()) {
        m_engine.pause();
        m_midi->close();
        m_autosaveTimer.stop();
        m_engine.clearAutosave();   // a normal exit: nothing to recover
        QFile::remove(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/autosave/running.lock");
        e->accept();
    } else {
        e->ignore();
    }
}
