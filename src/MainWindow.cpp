#include "MainWindow.h"

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QLabel>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <cmath>
#include "Widgets.h"

namespace {
struct Preset { const char* name; const char* rows[kDrumRows]; };
const Preset kPresets[] = {
    {"기본 4비트",  {"x...x...x...x...", "....x.......x...", "..x...x...x...x.", "................"}},
    {"힙합",        {"x.....x...x.....", "....x.......x...", "x.x.x.x.x.x.x.xx", "................"}},
    {"하우스",      {"x...x...x...x...", "................", ".x...x...x...x..", "....x.......x..."}},
    {"비우기",      {"................", "................", "................", "................"}},
};
}

MainWindow::MainWindow() {
    setWindowTitle("LiteDAW");
    m_timeline = new Timeline(&m_engine);
    m_seq = new StepSequencer(&m_engine);
    m_mixer = new MixerPanel(&m_engine);
    m_tabs = new QTabWidget;
    m_tabs->addTab(m_seq, "드럼 머신");
    m_tabs->addTab(m_mixer, "믹서");

    auto* split = new QSplitter(Qt::Vertical);
    split->addWidget(m_timeline);
    split->addWidget(m_tabs);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    setCentralWidget(split);

    buildToolbar();

    auto* hint = new QLabel("Space 재생/일시정지 · 오디오 파일을 타임라인에 끌어다 놓기 · Ctrl+휠 확대 · Del 삭제 · S 재생헤드에서 자르기 · 눈금자 우클릭 드래그 = 루프");
    statusBar()->addPermanentWidget(hint);

    connect(&m_engine, &Engine::tracksChanged, this, [this] { m_timeline->refreshLayout(); m_mixer->rebuild(); });
    connect(&m_engine, &Engine::clipsChanged, m_timeline, &Timeline::refreshLayout);
    connect(&m_engine, &Engine::loopChanged, this, [this] {
        QSignalBlocker b(m_actLoop);
        m_actLoop->setChecked(m_engine.loopOn());
        m_timeline->update();
    });
    connect(&m_engine, &Engine::message, this, [this](const QString& s) { statusBar()->showMessage(s, 6000); });

    m_mixer->rebuild();
    setMode(true);
    connect(&m_timer, &QTimer::timeout, this, &MainWindow::tick);
    m_timer.start(33);
}

void MainWindow::buildToolbar() {
    QToolBar* tb = addToolBar("transport");
    tb->setMovable(false);

    m_actPlay = tb->addAction("▶ 재생");
    m_actPlay->setShortcut(Qt::Key_Space);
    connect(m_actPlay, &QAction::triggered, this, [this] { m_engine.isPlaying() ? m_engine.pause() : m_engine.play(); });

    QAction* stop = tb->addAction("■ 정지");
    connect(stop, &QAction::triggered, this, [this] { m_engine.stop(); });

    m_actLoop = tb->addAction("반복");
    m_actLoop->setCheckable(true);
    m_actLoop->setShortcut(Qt::Key_L);
    connect(m_actLoop, &QAction::toggled, this, [this](bool on) { m_engine.setLoop(on); });

    m_actMetro = tb->addAction("메트로놈");
    m_actMetro->setCheckable(true);
    connect(m_actMetro, &QAction::toggled, this, [this](bool on) { m_engine.setMetronome(on); });

    tb->addSeparator();
    tb->addWidget(new QLabel(" BPM "));
    m_bpm = new QDoubleSpinBox;
    m_bpm->setRange(40, 240);
    m_bpm->setDecimals(1);
    m_bpm->setValue(120);
    connect(m_bpm, &QDoubleSpinBox::valueChanged, this, [this](double v) { m_engine.setBpm(v); m_timeline->refreshLayout(); });
    tb->addWidget(m_bpm);

    m_time = new QLabel("001.1  00:00.0");
    m_time->setStyleSheet("font-family:Consolas;font-size:16px;padding:0 12px");
    tb->addWidget(m_time);

    tb->addSeparator();
    m_preset = new QComboBox;
    m_preset->addItem("드럼 프리셋…");
    for (const Preset& p : kPresets) m_preset->addItem(p.name);
    connect(m_preset, &QComboBox::activated, this, [this](int i) {
        if (i > 0) {
            DrumPattern pat{};
            for (int r = 0; r < kDrumRows; ++r)
                for (int s = 0; s < kSteps; ++s) pat[size_t(r)][size_t(s)] = kPresets[i - 1].rows[r][s] == 'x';
            m_engine.setDrumPattern(pat);
        }
        m_preset->setCurrentIndex(0);
    });
    tb->addWidget(m_preset);

    QAction* imp = tb->addAction("오디오 가져오기");
    connect(imp, &QAction::triggered, this, &MainWindow::importDialog);
    QAction* add = tb->addAction("트랙 추가");
    connect(add, &QAction::triggered, this, [this] { m_engine.addAudioTrack(); });
    QAction* exp = tb->addAction("WAV 내보내기");
    connect(exp, &QAction::triggered, this, &MainWindow::exportDialog);

    tb->addSeparator();
    m_actSnap = tb->addAction("스냅");
    m_actSnap->setCheckable(true);
    m_actSnap->setChecked(true);
    connect(m_actSnap, &QAction::toggled, this, [this](bool on) { m_timeline->setSnap(on); });

    m_mode = new QComboBox;
    m_mode->addItems({"쉬운 모드", "프로 모드"});
    connect(m_mode, &QComboBox::currentIndexChanged, this, [this](int i) { setMode(i == 0); });
    tb->addWidget(m_mode);
}

void MainWindow::setMode(bool easy) {
    m_timeline->setEasyMode(easy);
    m_tabs->setTabVisible(1, !easy);  // 믹서는 프로 모드에서만
    if (easy) m_tabs->setCurrentIndex(0);
    m_actSnap->setVisible(!easy);
    if (easy) { m_actSnap->setChecked(true); }
}

void MainWindow::tick() {
    const bool pl = m_engine.isPlaying();
    m_actPlay->setText(pl ? "⏸ 일시정지" : "▶ 재생");
    const qint64 pos = m_engine.position();
    const int beat = int(pos / m_engine.framesPerBeat());
    const double sec = double(pos) / kSampleRate;
    m_time->setText(QString::asprintf("%03d.%d  %02d:%04.1f", beat / 4 + 1, beat % 4 + 1, int(sec) / 60, std::fmod(sec, 60.0)));
    if (pl) m_timeline->followPlayhead();
    m_timeline->update();
    m_seq->update();
    if (m_tabs->currentWidget() == m_mixer) m_mixer->tick();
}

void MainWindow::importDialog() {
    const QStringList files = QFileDialog::getOpenFileNames(this, "오디오 가져오기", {},
        "오디오 (*.wav *.mp3 *.flac *.ogg *.m4a *.aac);;모든 파일 (*)");
    int sel = m_timeline->selectedTrack();
    bool first = true;
    for (const QString& f : files) {
        m_engine.importAudio(f, (first && sel >= 1) ? sel : -1, m_engine.position());
        first = false;
    }
}

void MainWindow::exportDialog() {
    m_engine.pause();
    const QString path = QFileDialog::getSaveFileName(this, "WAV 내보내기", "mix.wav", "WAV (*.wav)");
    if (path.isEmpty()) return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString err;
    const bool ok = m_engine.exportWav(path, &err);
    QApplication::restoreOverrideCursor();
    statusBar()->showMessage(ok ? "내보내기 완료: " + path : "내보내기 실패: " + err, 8000);
}
