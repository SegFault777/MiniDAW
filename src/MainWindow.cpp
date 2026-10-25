// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QCloseEvent>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>
#include "Panels.h"
#include "PianoRoll.h"
#include "Theme.h"
#include "Timeline.h"

#ifndef MINIDAW_VERSION
#define MINIDAW_VERSION "0.2.0"
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
    m_synth = new SynthPanel(&m_engine);
    m_roll = new PianoRoll(&m_engine);
    m_seq = new StepSequencer(&m_engine);

    m_bottom = new QTabWidget;
    m_bottom->addTab(m_mixer, tr("Mixer"));
    m_bottom->addTab(m_synth, tr("Smart Controls"));
    m_bottom->addTab(m_roll, tr("Piano Roll"));
    m_bottom->addTab(m_seq, tr("Step Sequencer"));
    m_bottom->setCurrentIndex(1);

    m_vsplit = new QSplitter(Qt::Vertical);
    m_vsplit->addWidget(m_timeline);
    m_vsplit->addWidget(m_bottom);
    m_vsplit->setSizes({430, 290});
    m_hsplit = new QSplitter(Qt::Horizontal);
    m_hsplit->addWidget(m_inspector);
    m_hsplit->addWidget(m_vsplit);
    m_hsplit->setSizes({250, 1050});
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

    m_keysLabel = new QLabel;
    statusBar()->addPermanentWidget(m_keysLabel);
    statusBar()->showMessage(tr("Space: play · R: record · C: cycle · double-click an instrument lane to create a MIDI region · Ctrl+K: musical typing"));

    connect(&m_engine, &Engine::tracksChanged, this, [this] {
        m_timeline->refreshLayout();
        m_mixer->rebuild();
        m_inspector->refresh();
        m_synth->setTrack(m_selTrack);
    });
    connect(&m_engine, &Engine::regionsChanged, m_timeline, &Timeline::refreshLayout);
    connect(&m_engine, &Engine::loopChanged, this, [this] {
        QSignalBlocker b(aCycle);
        aCycle->setChecked(m_engine.loopOn());
        m_timeline->update();
    });
    connect(&m_engine, &Engine::message, this, [this](const QString& s) { statusBar()->showMessage(s, 8000); });
    connect(m_timeline, &Timeline::selectionChanged, this, &MainWindow::onSelection);
    connect(m_timeline, &Timeline::regionOpened, this, [this](int t, int r) {
        m_roll->setRegion(t, r);
        showBottom(2);
    });
    connect(m_mixer, &MixerPanel::trackSelected, m_timeline, &Timeline::selectTrack);

    m_mixer->rebuild();
    qApp->installEventFilter(this);   // musical typing needs to see keys before shortcuts do
    connect(&m_timer, &QTimer::timeout, this, &MainWindow::tick);
    m_timer.start(33);
    m_timeline->selectTrack(1);       // the default software instrument
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
    aBounce = make(tr("Bounce to WAV…"), QKeySequence(Qt::CTRL | Qt::Key_B));
    aQuit = make(tr("Quit"), QKeySequence::Quit);
    aUndo = make(tr("Undo"), QKeySequence::Undo);
    aRedo = make(tr("Redo"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z));
    aDelete = make(tr("Delete"), QKeySequence(Qt::Key_Delete));
    aDuplicate = make(tr("Duplicate Region"), QKeySequence(Qt::CTRL | Qt::Key_D));
    aSplit = make(tr("Split at Playhead"), QKeySequence(Qt::CTRL | Qt::Key_T));
    aSnap = make(tr("Snap to Grid"), {}, true);
    aSnap->setChecked(true);
    aAutomation = make(tr("Volume Automation"), QKeySequence(Qt::Key_A), true);
    aNewAudio = make(tr("New Audio Track"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_A));
    aNewInstrument = make(tr("New Software Instrument Track"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S));
    aNewDrums = make(tr("New Drum Machine Track"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_D));
    aDeleteTrack = make(tr("Delete Track"), QKeySequence(Qt::CTRL | Qt::Key_Backspace));

    aRewind = make(tr("Go to Beginning"), QKeySequence(Qt::Key_Return));
    aPlay = make(tr("Play / Pause"), QKeySequence(Qt::Key_Space));
    aStop = make(tr("Stop"), QKeySequence(Qt::Key_Period));
    aRecord = make(tr("Record"), QKeySequence(Qt::Key_R), true);
    aCycle = make(tr("Cycle"), QKeySequence(Qt::Key_C), true);
    aMetronome = make(tr("Metronome Click"), QKeySequence(Qt::Key_K), true);
    aCountIn = make(tr("Count-in (1 bar)"), {}, true);
    aRewind->setIcon(theme::icon(theme::Glyph::Rewind, theme::kText, theme::kText));
    aPlay->setIcon(theme::icon(theme::Glyph::Play, theme::kText, theme::kText));
    aStop->setIcon(theme::icon(theme::Glyph::Stop, theme::kText, theme::kText));
    aRecord->setIcon(theme::icon(theme::Glyph::Record, QColor(190, 70, 70), theme::kRed));
    aCycle->setIcon(theme::icon(theme::Glyph::Cycle, theme::kText, theme::kYellow));
    aMetronome->setIcon(theme::icon(theme::Glyph::Metronome, theme::kText, theme::kAccent));
    aCountIn->setIcon(theme::icon(theme::Glyph::CountIn, theme::kText, theme::kAccent));

    aInspector = make(tr("Inspector"), QKeySequence(Qt::Key_I), true);
    aInspector->setChecked(true);
    aMixer = make(tr("Mixer"), QKeySequence(Qt::Key_X));
    aSmart = make(tr("Smart Controls"), QKeySequence(Qt::Key_B));
    aRoll = make(tr("Piano Roll"), QKeySequence(Qt::Key_P));
    aSteps = make(tr("Step Sequencer"), QKeySequence(Qt::SHIFT | Qt::Key_S));
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
    connect(aBounce, &QAction::triggered, this, &MainWindow::exportDialog);
    connect(aQuit, &QAction::triggered, this, &QWidget::close);
    connect(aUndo, &QAction::triggered, this, [this] { m_engine.undo(); });
    connect(aRedo, &QAction::triggered, this, [this] { m_engine.redo(); });
    connect(aDelete, &QAction::triggered, this, &MainWindow::onDelete);
    connect(aDuplicate, &QAction::triggered, this, [this] { m_timeline->duplicateSelected(); });
    connect(aSplit, &QAction::triggered, this, [this] { m_timeline->splitAtPlayhead(); });
    connect(aSnap, &QAction::toggled, this, [this](bool on) { m_timeline->setSnap(on); });
    connect(aAutomation, &QAction::toggled, this, [this](bool on) { m_timeline->setAutomationMode(on); });
    connect(aNewAudio, &QAction::triggered, this, [this] { m_timeline->selectTrack(m_engine.addTrack(TrackType::Audio)); });
    connect(aNewInstrument, &QAction::triggered, this, [this] { m_timeline->selectTrack(m_engine.addTrack(TrackType::Instrument)); });
    connect(aNewDrums, &QAction::triggered, this, [this] { m_timeline->selectTrack(m_engine.addTrack(TrackType::Drums)); });
    connect(aDeleteTrack, &QAction::triggered, this, [this] {
        if (m_selTrack >= 0) { const int t = m_selTrack; m_timeline->selectTrack(-1); m_engine.removeTrack(t); }
    });

    connect(aRewind, &QAction::triggered, this, [this] { m_engine.seekBeats(m_engine.loopOn() ? m_engine.loopA() : 0.0); });
    connect(aPlay, &QAction::triggered, this, [this] { m_engine.isPlaying() ? m_engine.pause() : m_engine.play(); });
    connect(aStop, &QAction::triggered, this, [this] { m_engine.stop(); });
    connect(aRecord, &QAction::triggered, this, &MainWindow::onRecord);
    connect(aCycle, &QAction::toggled, this, [this](bool on) { m_engine.setLoop(on); });
    connect(aMetronome, &QAction::toggled, this, [this](bool on) { m_engine.setMetronome(on); });
    connect(aCountIn, &QAction::toggled, this, [this](bool on) { m_engine.setCountIn(on); });

    connect(aInspector, &QAction::toggled, this, [this](bool on) { m_inspector->setVisible(on); });
    connect(aMixer, &QAction::triggered, this, [this] { showBottom(0); });
    connect(aSmart, &QAction::triggered, this, [this] { showBottom(1); });
    connect(aRoll, &QAction::triggered, this, [this] { showBottom(2); });
    connect(aSteps, &QAction::triggered, this, [this] { showBottom(3); });
    connect(aEditors, &QAction::triggered, this, [this] { m_bottom->setVisible(!m_bottom->isVisible()); });
    connect(aTyping, &QAction::toggled, this, [this](bool on) {
        m_keysLabel->setText(on ? tr("KEYS  C%1").arg(3 + m_octave) : QString());
        if (!on)
            for (auto& [key, tp] : m_heldKeys) m_engine.noteOff(tp.first, tp.second);
        if (!on) m_heldKeys.clear();
    });
    connect(aAbout, &QAction::triggered, this, &MainWindow::about);
}

void MainWindow::buildMenus() {
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addActions({aNew, aOpen, aSave, aSaveAs});
    file->addSeparator();
    file->addActions({aImport, aBounce});
    file->addSeparator();
    file->addAction(aQuit);

    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    edit->addActions({aUndo, aRedo});
    edit->addSeparator();
    edit->addActions({aDelete, aDuplicate, aSplit});
    edit->addSeparator();
    edit->addActions({aSnap, aAutomation});

    QMenu* track = menuBar()->addMenu(tr("&Track"));
    track->addActions({aNewAudio, aNewInstrument, aNewDrums});
    track->addSeparator();
    track->addAction(aDeleteTrack);

    QMenu* transport = menuBar()->addMenu(tr("T&ransport"));
    transport->addActions({aPlay, aStop, aRecord, aRewind});
    transport->addSeparator();
    transport->addActions({aCycle, aMetronome, aCountIn});

    QMenu* view = menuBar()->addMenu(tr("&View"));
    view->addActions({aInspector, aMixer, aSmart, aRoll, aSteps, aEditors});
    view->addSeparator();
    view->addAction(aTyping);

    menuBar()->addMenu(tr("&Help"))->addAction(aAbout);
}

QWidget* MainWindow::buildControlBar() {
    auto* bar = new QFrame;
    bar->setStyleSheet("QFrame#ctrl{background:#232326;border-bottom:1px solid #151517}");
    bar->setObjectName("ctrl");
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

    // LCD display: bar / beat / division / tick, time and tempo.
    auto* lcd = new QFrame;
    lcd->setStyleSheet("QFrame{background:#121416;border:1px solid #050506;border-radius:6px}"
                       "QLabel{border:none;color:#e8f0ff;font-family:Consolas,monospace;font-size:20px}");
    auto* lh = new QHBoxLayout(lcd);
    lh->setContentsMargins(12, 2, 12, 2);
    m_lcdPos = new QLabel("  1 1 1 000");
    m_lcdTime = new QLabel("00:00.000");
    m_lcdTime->setStyleSheet("color:#8fa3c0;font-size:15px");
    m_bpm = new QDoubleSpinBox;
    m_bpm->setRange(30, 300);
    m_bpm->setDecimals(1);
    m_bpm->setValue(m_engine.bpm());
    m_bpm->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_bpm->setSuffix(" bpm");
    m_bpm->setFixedWidth(100);
    m_bpm->setStyleSheet("QDoubleSpinBox{background:transparent;border:none;color:#e8f0ff;font-family:Consolas,monospace;font-size:18px}");
    connect(m_bpm, &QDoubleSpinBox::valueChanged, this, [this](double v) { m_engine.setBpm(v); m_timeline->refreshLayout(); });
    lh->addWidget(m_lcdPos);
    lh->addSpacing(8);
    lh->addWidget(m_lcdTime);
    lh->addSpacing(8);
    lh->addWidget(m_bpm);
    lh->addWidget(new QLabel("4/4"));
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
    const double frac = (beat - std::floor(beat)) * 4.0;   // sixteenth-note position within the beat
    m_lcdPos->setText(QString::asprintf("%3d %d %d %03d", int(beat / kBeatsPerBar) + 1, int(beat) % kBeatsPerBar + 1,
                                        int(frac) + 1, int((frac - std::floor(frac)) * 240)));
    const double sec = beat * 60.0 / m_engine.bpm();
    m_lcdTime->setText(QString::asprintf("%02d:%02d.%03d", int(sec) / 60, int(sec) % 60, int((sec - std::floor(sec)) * 1000)));
    {
        QSignalBlocker b(aRecord);
        aRecord->setChecked(m_engine.isRecording());
    }
    if (!m_bpm->hasFocus() && std::fabs(m_bpm->value() - m_engine.bpm()) > 0.05) {
        QSignalBlocker b(m_bpm);
        m_bpm->setValue(m_engine.bpm());
    }
    if (m_engine.isPlaying()) m_timeline->followPlayhead();
    m_timeline->update();
    m_inspector->tick();
    if (m_bottom->isVisible()) {
        switch (m_bottom->currentIndex()) {
        case 0: m_mixer->tick(); break;
        case 2: m_roll->update(); break;
        case 3: m_seq->update(); break;
        default: break;
        }
    }
    const QString name = m_engine.projectPath().isEmpty() ? tr("Untitled") : QFileInfo(m_engine.projectPath()).completeBaseName();
    const QString title = name + (m_engine.isDirty() ? " *" : "") + " — MiniDAW";
    if (title != m_lastTitle) { setWindowTitle(title); m_lastTitle = title; }
}

void MainWindow::onSelection(int track, int region) {
    m_selTrack = track;
    m_inspector->setTrack(track);
    m_synth->setTrack(track);
    m_seq->setTrack(track);
    const auto tr = m_engine.snapshot();
    if (track < 0 || track >= int(tr.size())) return;
    const Track& t = tr[size_t(track)];
    if (region >= 0 && region < int(t.regions.size()) && t.regions[size_t(region)].midi) m_roll->setRegion(track, region);
    if (t.type == TrackType::Drums) showBottom(3);
    else if (t.type == TrackType::Instrument && m_bottom->currentIndex() == 3) showBottom(1);
}

void MainWindow::onRecord() {
    if (m_engine.isRecording()) {
        m_engine.stopRecording();
        return;
    }
    const auto tr = m_engine.snapshot();
    const bool anyArmed = std::any_of(tr.begin(), tr.end(), [](const Track& t) { return t.armed; });
    if (!anyArmed && m_selTrack >= 0 && m_selTrack < int(tr.size()) && tr[size_t(m_selTrack)].type != TrackType::Drums)
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
    const auto tr = m_engine.snapshot();
    if (m_selTrack >= 0 && m_selTrack < int(tr.size()) && tr[size_t(m_selTrack)].type == TrackType::Instrument) return m_selTrack;
    for (size_t i = 0; i < tr.size(); ++i)
        if (tr[i].type == TrackType::Instrument && tr[i].armed) return int(i);
    for (size_t i = 0; i < tr.size(); ++i)
        if (tr[i].type == TrackType::Instrument) return int(i);
    return -1;
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

// ------------------------------------------------------------------ files
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
    m_bpm->setValue(m_engine.bpm());
    m_timeline->selectTrack(0);
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
    return true;
}

void MainWindow::importDialog() {
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Import Audio"), {},
                                                            tr("Audio (*.wav *.mp3 *.flac *.ogg *.m4a *.aac);;All files (*)"));
    for (const QString& f : files) m_engine.importAudio(f, -1, m_engine.positionBeats());
}

void MainWindow::exportDialog() {
    m_engine.pause();
    const QString path = QFileDialog::getSaveFileName(this, tr("Bounce to WAV"), "bounce.wav", "WAV (*.wav)");
    if (path.isEmpty()) return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString err;
    const bool ok = m_engine.exportWav(path, &err);
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
           "<p>MiniDAW is an independent project and is not affiliated with or endorsed by Apple Inc.</p>")
            .arg(MINIDAW_VERSION));
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (maybeSave()) {
        m_engine.pause();
        e->accept();
    } else {
        e->ignore();
    }
}
