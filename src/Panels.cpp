// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Panels.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>
#include "NativePlugins.h"
#include "Theme.h"

// ================================================================= helpers
std::vector<vst3::PluginInfo>& PluginCatalog::list() {
    static std::vector<vst3::PluginInfo> plugins;
    return plugins;
}

void PluginCatalog::rescan() {
    QSettings s;
    QStringList folders = s.value("vst3/folders").toStringList();
    if (folders.isEmpty()) folders = vst3::defaultFolders();
    list() = vst3::scan(folders);
}

namespace {
QString effectLabel(const QString& id) {
    auto fx = dsp::createEffect(id.toStdString());
    return fx ? QString::fromUtf8(fx->displayName()) : id;
}

/// A menu with the built-in effects followed by the VST3 effect plug-ins.
QMenu* makeEffectMenu(QWidget* parent, std::function<void(const QString&)> pick) {
    auto* menu = new QMenu(parent);
    for (const std::string& id : dsp::insertEffectIds()) {
        const QString qid = QString::fromStdString(id);
        menu->addAction(QObject::tr(effectLabel(qid).toUtf8().constData()), parent, [pick, qid] { pick(qid); });
    }
    QMenu* nat = nullptr;
    for (const nativeplug::Info& p : nativeplug::list()) {   // MiniDAW plug-ins (plugin-sdk)
        if (!nat) { menu->addSeparator(); nat = menu->addMenu(QObject::tr("MiniDAW Plug-ins")); }
        nat->addAction(p.name + (p.vendor.isEmpty() ? QString() : "  (" + p.vendor + ")"), parent, [pick, id = p.id] { pick(id); });
    }
    QMenu* vst = nullptr;
    for (const vst3::PluginInfo& p : PluginCatalog::list()) {
        if (p.instrument) continue;
        if (!vst) { menu->addSeparator(); vst = menu->addMenu(QObject::tr("VST3 Plug-ins")); }
        vst->addAction(p.name + (p.vendor.isEmpty() ? QString() : "  (" + p.vendor + ")"), parent, [pick, id = p.id] { pick(id); });
    }
    return menu;
}

QString dbText(float gain) {
    return gain <= 0.001f ? QString::fromUtf8("−∞") : QString::asprintf("%.1f", 20.0 * std::log10(double(gain)));
}
}  // namespace

// ================================================================= StepSequencer
static const char* kRowNames[kDrumRows] = {"Kick", "Snare", "Hi-Hat", "Clap"};
static const QColor kRowColors[kDrumRows] = {QColor(70, 130, 240), QColor(165, 90, 225), QColor(40, 200, 170), QColor(225, 80, 170)};

StepSequencer::StepSequencer(Engine* e, QWidget* p) : QWidget(p), m_e(e) { setMinimumHeight(150); }

bool StepSequencer::isDrums() const {
    const auto trk = m_e->snapshot();
    return m_track >= 0 && m_track < int(trk.size()) && trk[size_t(m_track)].type == TrackType::Drums;
}

QRectF StepSequencer::cell(int r, int s) const {
    const double lw = 90, cw = (width() - lw - 8) / kSteps, ch = (height() - 8) / double(kDrumRows);
    return QRectF(lw + s * cw, 4 + r * ch, cw - 3, ch - 3);
}

bool StepSequencer::hit(const QPointF& pt, int& r, int& s) const {
    for (r = 0; r < kDrumRows; ++r)
        for (s = 0; s < kSteps; ++s)
            if (cell(r, s).contains(pt)) return true;
    return false;
}

void StepSequencer::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (!isDrums()) {
        p.setPen(theme::kDim);
        p.drawText(rect(), Qt::AlignCenter, tr("Select a Drum Machine track to edit its pattern."));
        return;
    }
    const DrumPattern pat = m_e->drumPattern(m_track);
    const int cur = m_e->isPlaying() ? int(m_e->positionBeats() * 4.0) % kSteps : -1;   // 16th notes
    for (int r = 0; r < kDrumRows; ++r) {
        p.setPen(theme::kText);
        p.drawText(QRectF(10, cell(r, 0).top(), 78, cell(r, 0).height()), Qt::AlignVCenter, tr(kRowNames[r]));
        for (int s = 0; s < kSteps; ++s) {
            QColor fill = ((s / 4) % 2) ? QColor(44, 44, 50) : QColor(54, 54, 60);
            if (pat[size_t(r)][size_t(s)]) fill = (s % 4 == 0) ? kRowColors[r].lighter(125) : kRowColors[r];
            p.setPen(s == cur ? QPen(Qt::white, 2) : Qt::NoPen);
            p.setBrush(fill);
            p.drawRoundedRect(cell(r, s), 4, 4);
        }
    }
}

void StepSequencer::mousePressEvent(QMouseEvent* e) {
    int r, s;
    if (e->button() == Qt::LeftButton && isDrums() && hit(e->position(), r, s)) {
        m_e->checkpoint();
        m_val = !m_e->drumPattern(m_track)[size_t(r)][size_t(s)];
        m_e->setStep(m_track, r, s, m_val);
        m_drag = true;
        m_lastR = r;
        m_lastS = s;
        update();
    }
}

void StepSequencer::mouseMoveEvent(QMouseEvent* e) {
    int r, s;
    if (m_drag && hit(e->position(), r, s) && (r != m_lastR || s != m_lastS)) {
        m_e->setStep(m_track, r, s, m_val);   // paint the same value across cells while dragging
        m_lastR = r;
        m_lastS = s;
        update();
    }
}

void StepSequencer::mouseReleaseEvent(QMouseEvent*) { m_drag = false; }

// ================================================================= MixerPanel
MixerPanel::MixerPanel(Engine* e, QWidget* p) : QWidget(p), m_e(e) {
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    auto* sa = new QScrollArea;
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);
    m_host = new QWidget;
    m_lay = new QHBoxLayout(m_host);
    m_lay->setSpacing(3);
    m_lay->setAlignment(Qt::AlignLeft);
    sa->setWidget(m_host);
    root->addWidget(sa, 1);

    auto* mw = new QFrame;
    mw->setFrameShape(QFrame::StyledPanel);
    mw->setFixedWidth(104);
    auto* v = new QVBoxLayout(mw);
    v->setContentsMargins(6, 6, 6, 6);
    auto* out = new QLabel(tr("Stereo Out"));
    out->setAlignment(Qt::AlignCenter);
    out->setStyleSheet("color:#8c8c94;font-size:10px");
    v->addWidget(out);
    v->addStretch(1);
    m_masterDb = new QLabel;
    m_masterDb->setAlignment(Qt::AlignCenter);
    m_masterDb->setStyleSheet("background:#18181a;border-radius:3px;padding:2px");
    v->addWidget(m_masterDb);
    auto* h = new QHBoxLayout;
    m_masterFader = new Fader;
    m_masterFader->setValue(m_e->masterVolume());
    m_masterMeter = new Meter;
    h->addWidget(m_masterFader);
    h->addWidget(m_masterMeter);
    v->addLayout(h, 4);
    auto* tag = new QLabel(tr("Master"));
    tag->setAlignment(Qt::AlignCenter);
    tag->setStyleSheet("background:#6a6a74;border-radius:3px;padding:4px;font-weight:bold");
    v->addWidget(tag);
    root->addWidget(mw);
    m_masterDb->setText(dbText(m_masterFader->value()));
    connect(m_masterFader, &Fader::valueChanged, this, [this](float g) {
        m_e->setMasterVolume(g);
        m_masterDb->setText(dbText(g));
    });
}

void MixerPanel::rebuild() {
    for (auto& s : m_strips) {
        m_lay->removeWidget(s.w);
        s.w->hide();
        s.w->deleteLater();
    }
    m_strips.clear();
    const auto trks = m_e->snapshot();
    for (int i = 0; i < int(trks.size()); ++i) {
        const Track& t = trks[size_t(i)];
        auto* w = new QFrame;
        w->setFrameShape(QFrame::StyledPanel);
        w->setFixedWidth(100);
        auto* v = new QVBoxLayout(w);
        v->setContentsMargins(5, 5, 5, 5);
        v->setSpacing(3);

        // Audio FX slots (blue like in Logic); an empty slot opens the add menu.
        auto* fxLabel = new QLabel(tr("Audio FX"));
        fxLabel->setStyleSheet("color:#8c8c94;font-size:10px");
        v->addWidget(fxLabel);
        for (int slot = 0; slot < 5; ++slot) {
            auto* b = new QToolButton;
            b->setFixedHeight(18);
            b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            if (slot < int(t.inserts.size())) {
                const auto& fx = t.inserts[size_t(slot)];
                b->setText(QString::fromUtf8(fx->displayName()));
                b->setStyleSheet(QString("QToolButton{background:%1;border-radius:3px;font-size:10px;color:white}")
                                     .arg(fx->bypass.load() ? "#4a5568" : "#3b78d8"));
                connect(b, &QToolButton::clicked, this, [this, i, slot, fx] {
                    if (auto* win = vst3::createEditor(fx, window())) { win->show(); return; }
                    (new EffectWindow(fx, m_e, window()))->show();
                    Q_UNUSED(slot);
                });
                b->setContextMenuPolicy(Qt::CustomContextMenu);
                connect(b, &QToolButton::customContextMenuRequested, this, [this, i, slot, b](const QPoint& pt) {
                    QMenu menu;
                    QAction* rm = menu.addAction(tr("Remove"));
                    if (menu.exec(b->mapToGlobal(pt)) == rm) m_e->removeInsert(i, slot);
                });
            } else {
                b->setStyleSheet("QToolButton{background:#2a2a2e;border:1px solid #3a3a40;border-radius:3px}");
                b->setPopupMode(QToolButton::InstantPopup);
                b->setMenu(makeEffectMenu(b, [this, i](const QString& id) { m_e->addInsert(i, id); }));
            }
            v->addWidget(b);
        }
        auto* sends = new QHBoxLayout;
        for (int k = 0; k < 2; ++k) {
            auto* kn = new Knob(k ? tr("Send B") : tr("Send A"), 0, 1, 0);
            kn->setCompact(true);
            kn->setValue(k ? t.sendB : t.sendA);
            kn->setFormatter([](double x) { return QString("%1%").arg(int(x * 100)); });
            connect(kn, &Knob::valueChanged, this, [this, i, k](double x) { k ? m_e->setSendB(i, float(x)) : m_e->setSendA(i, float(x)); });
            sends->addWidget(kn);
        }
        v->addLayout(sends);

        auto* pan = new Knob(tr("Pan"), -1, 1, 0);
        pan->setCompact(true);
        pan->setValue(t.pan);
        pan->setFormatter([](double x) { return std::fabs(x) < 0.01 ? QString("C") : QString("%1%2").arg(x < 0 ? "L" : "R").arg(int(std::fabs(x) * 100)); });
        connect(pan, &Knob::valueChanged, this, [this, i](double x) { m_e->setPan(i, float(x)); });
        v->addWidget(pan, 0, Qt::AlignHCenter);

        auto* db = new QLabel(dbText(t.volume));
        db->setAlignment(Qt::AlignCenter);
        db->setStyleSheet("background:#18181a;border-radius:3px;padding:2px");
        v->addWidget(db);
        auto* h = new QHBoxLayout;
        auto* fader = new Fader;
        fader->setValue(t.volume);
        auto* meter = new Meter;
        h->addWidget(fader);
        h->addWidget(meter);
        v->addLayout(h, 1);

        auto* bh = new QHBoxLayout;
        bh->setSpacing(2);
        auto mk = [&](const QString& txt, const QString& tip) {
            auto* b = new QToolButton;
            b->setText(txt);
            b->setCheckable(true);
            b->setToolTip(tip);
            b->setFixedWidth(26);
            bh->addWidget(b);
            return b;
        };
        QToolButton* m = mk("M", tr("Mute"));
        QToolButton* s = mk("S", tr("Solo"));
        QToolButton* r = mk("R", tr("Record enable"));
        r->setVisible(t.type != TrackType::Drums);
        v->addLayout(bh);

        auto* name = new QToolButton;   // coloured name tag at the bottom, as in Logic's mixer
        name->setText(t.name);
        name->setToolTip(t.name);
        name->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        name->setFixedHeight(26);
        name->setStyleSheet(QString("QToolButton{background:%1;border-radius:3px;font-weight:bold;color:white}").arg(t.color.darker(125).name()));
        v->addWidget(name);

        m_lay->addWidget(w);
        m_strips.push_back({w, name, fader, db, pan, m, s, r, meter});

        connect(name, &QToolButton::clicked, this, [this, i] { emit trackSelected(i); });
        connect(fader, &Fader::valueChanged, this, [this, i, db](float g) { m_e->setVolume(i, g); db->setText(dbText(g)); });
        connect(m, &QToolButton::toggled, this, [this, i](bool on) { m_e->setMute(i, on); });
        connect(s, &QToolButton::toggled, this, [this, i](bool on) { m_e->setSolo(i, on); });
        connect(r, &QToolButton::toggled, this, [this, i](bool on) { m_e->setArmed(i, on); });
    }
}

void MixerPanel::tick() {
    const auto trks = m_e->snapshot();
    if (trks.size() != m_strips.size()) { rebuild(); return; }
    for (size_t i = 0; i < trks.size(); ++i) {
        Strip& s = m_strips[i];
        const Track& t = trks[i];
        s.meter->setLevel(t.peakL, t.peakR);
        if (s.name->text() != t.name) s.name->setText(t.name);
        if (std::fabs(s.fader->value() - t.volume) > 0.002f) { s.fader->setValue(t.volume); s.db->setText(dbText(t.volume)); }
        if (std::fabs(s.pan->value() - double(t.pan)) > 0.005) s.pan->setValue(t.pan);
        if (s.m->isChecked() != t.mute) { QSignalBlocker b(s.m); s.m->setChecked(t.mute); }
        if (s.s->isChecked() != t.solo) { QSignalBlocker b(s.s); s.s->setChecked(t.solo); }
        if (s.r->isChecked() != t.armed) { QSignalBlocker b(s.r); s.r->setChecked(t.armed); }
    }
    float l, r;
    m_e->masterPeaks(l, r);
    m_masterMeter->setLevel(l, r);
}

// ================================================================= Inspector
Inspector::Inspector(Engine* e, QWidget* p) : QWidget(p), m_e(e) {
    setMinimumWidth(240);
    setMaximumWidth(320);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(8, 8, 8, 8);

    auto* title = new QLabel(tr("INSPECTOR"));
    title->setStyleSheet("color:#8c8c94;font-weight:bold;letter-spacing:1px");
    lay->addWidget(title);
    m_name = new QLineEdit;
    m_type = new QLabel;
    m_type->setStyleSheet("color:#8c8c94");
    lay->addWidget(m_name);
    lay->addWidget(m_type);
    m_instrument = new QToolButton;
    m_instrument->setPopupMode(QToolButton::InstantPopup);
    m_instrument->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_instrument->setStyleSheet("QToolButton{background:#2f6fd0;border-radius:4px;padding:4px;color:white}");
    lay->addWidget(m_instrument);

    // Channel strip.
    auto* strip = new QHBoxLayout;
    m_fader = new Fader;
    m_meter = new Meter;
    strip->addWidget(m_fader);
    strip->addWidget(m_meter);
    auto* knobs = new QGridLayout;
    m_pan = new Knob(tr("Pan"), -1, 1, 0);
    m_pan->setFormatter([](double v) { return std::fabs(v) < 0.01 ? QString("C") : QString("%1%2").arg(v < 0 ? "L" : "R").arg(int(std::fabs(v) * 100)); });
    m_sendA = new Knob(tr("Send A"), 0, 1, 0);
    m_sendB = new Knob(tr("Send B"), 0, 1, 0);
    for (Knob* k : {m_sendA, m_sendB}) k->setFormatter([](double v) { return QString("%1%").arg(int(v * 100)); });
    knobs->addWidget(m_pan, 0, 0);
    knobs->addWidget(m_sendA, 0, 1);
    knobs->addWidget(m_sendB, 1, 0);
    strip->addLayout(knobs);
    lay->addLayout(strip);

    auto* btns = new QHBoxLayout;
    auto mk = [&](const QString& txt, const QString& tip) {
        auto* b = new QToolButton;
        b->setText(txt);
        b->setToolTip(tip);
        b->setCheckable(true);
        btns->addWidget(b);
        return b;
    };
    m_mute = mk("M", tr("Mute"));
    m_solo = mk("S", tr("Solo"));
    m_arm = mk("R", tr("Record enable"));
    btns->addStretch(1);
    lay->addLayout(btns);

    // Insert effects.
    auto* insTitle = new QLabel(tr("Audio FX"));
    insTitle->setStyleSheet("color:#8c8c94");
    lay->addWidget(insTitle);
    m_inserts = new QListWidget;
    m_inserts->setMaximumHeight(96);
    lay->addWidget(m_inserts);
    auto* ib = new QHBoxLayout;
    m_add = new QToolButton;
    m_add->setText(tr("+ Add"));
    m_add->setPopupMode(QToolButton::InstantPopup);
    m_add->setMenu(makeEffectMenu(m_add, [this](const QString& id) { if (m_track >= 0) m_e->addInsert(m_track, id); }));
    m_remove = new QToolButton;
    m_remove->setText(tr("− Remove"));
    ib->addWidget(m_add);
    ib->addWidget(m_remove);
    ib->addStretch(1);
    lay->addLayout(ib);

    auto* auxA = new QPushButton(tr("Aux A: Reverb…"));
    auto* auxB = new QPushButton(tr("Aux B: Echo…"));
    lay->addWidget(auxA);
    lay->addWidget(auxB);

    // Region settings (audio regions): follow tempo, source tempo and transposition.
    m_regionBox = new QFrame;
    m_regionBox->setStyleSheet("QFrame{background:#303036;border-radius:5px}");
    auto* rl = new QGridLayout(m_regionBox);
    m_regionName = new QLabel;
    m_regionName->setStyleSheet("font-weight:bold");
    rl->addWidget(m_regionName, 0, 0, 1, 2);
    m_follow = new QCheckBox(tr("Follow Tempo"));
    rl->addWidget(m_follow, 1, 0, 1, 2);
    rl->addWidget(new QLabel(tr("Source tempo")), 2, 0);
    m_srcBpm = new QDoubleSpinBox;
    m_srcBpm->setRange(0, 400);
    m_srcBpm->setDecimals(1);
    m_srcBpm->setSuffix(" bpm");
    rl->addWidget(m_srcBpm, 2, 1);
    rl->addWidget(new QLabel(tr("Transpose")), 3, 0);
    m_semis = new QSpinBox;
    m_semis->setRange(-24, 24);
    m_semis->setSuffix(tr(" st"));
    rl->addWidget(m_semis, 3, 1);
    rl->addWidget(new QLabel(tr("Gain")), 4, 0);
    m_gain = new QDoubleSpinBox;
    m_gain->setRange(-60, 24);
    m_gain->setDecimals(1);
    m_gain->setSingleStep(0.5);
    m_gain->setSuffix(" dB");
    rl->addWidget(m_gain, 4, 1);
    rl->addWidget(new QLabel(tr("Fade In")), 5, 0);
    m_fadeIn = new QSpinBox;
    m_fadeIn->setRange(0, 10000);
    m_fadeIn->setSuffix(" ms");
    rl->addWidget(m_fadeIn, 5, 1);
    rl->addWidget(new QLabel(tr("Fade Out")), 6, 0);
    m_fadeOut = new QSpinBox;
    m_fadeOut->setRange(0, 10000);
    m_fadeOut->setSuffix(" ms");
    rl->addWidget(m_fadeOut, 6, 1);
    m_reverse = new QPushButton(tr("Reverse"));
    m_normalize = new QPushButton(tr("Normalize"));
    rl->addWidget(m_reverse, 7, 0);
    rl->addWidget(m_normalize, 7, 1);
    lay->addWidget(m_regionBox);
    lay->addStretch(1);

    connect(auxA, &QPushButton::clicked, this, [this] { (new EffectWindow(m_e->auxEffect(0), m_e, window()))->show(); });
    connect(auxB, &QPushButton::clicked, this, [this] { (new EffectWindow(m_e->auxEffect(1), m_e, window()))->show(); });
    connect(m_name, &QLineEdit::editingFinished, this, [this] {
        if (!m_updating && m_track >= 0 && !m_name->text().isEmpty()) m_e->renameTrack(m_track, m_name->text());
    });
    connect(m_fader, &Fader::valueChanged, this, [this](float g) { if (!m_updating) m_e->setVolume(m_track, g); });
    connect(m_pan, &Knob::valueChanged, this, [this](double v) { m_e->setPan(m_track, float(v)); });
    connect(m_sendA, &Knob::valueChanged, this, [this](double v) { m_e->setSendA(m_track, float(v)); });
    connect(m_sendB, &Knob::valueChanged, this, [this](double v) { m_e->setSendB(m_track, float(v)); });
    connect(m_mute, &QToolButton::toggled, this, [this](bool on) { if (!m_updating) m_e->setMute(m_track, on); });
    connect(m_solo, &QToolButton::toggled, this, [this](bool on) { if (!m_updating) m_e->setSolo(m_track, on); });
    connect(m_arm, &QToolButton::toggled, this, [this](bool on) { if (!m_updating) m_e->setArmed(m_track, on); });
    connect(m_inserts, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) { openInsert(m_inserts->currentRow()); });
    connect(m_inserts, &QListWidget::itemChanged, this, [this](QListWidgetItem* it) {
        if (!m_updating) m_e->setInsertBypass(m_track, m_inserts->row(it), it->checkState() != Qt::Checked);
    });
    connect(m_remove, &QToolButton::clicked, this, [this] {
        if (m_track >= 0 && m_inserts->currentRow() >= 0) m_e->removeInsert(m_track, m_inserts->currentRow());
    });
    connect(m_follow, &QCheckBox::toggled, this, [this] { applyFlex(); });
    connect(m_srcBpm, &QDoubleSpinBox::editingFinished, this, [this] { applyFlex(); });
    connect(m_semis, &QSpinBox::editingFinished, this, [this] { applyFlex(); });
    connect(m_gain, &QDoubleSpinBox::editingFinished, this, [this] { applyRegionAudio(); });
    connect(m_fadeIn, &QSpinBox::editingFinished, this, [this] { applyRegionAudio(); });
    connect(m_fadeOut, &QSpinBox::editingFinished, this, [this] { applyRegionAudio(); });
    connect(m_reverse, &QPushButton::clicked, this, [this] {
        if (m_track >= 0 && m_region >= 0) { m_e->reverseRegion(m_track, m_region); refresh(); }
    });
    connect(m_normalize, &QPushButton::clicked, this, [this] {
        if (m_track >= 0 && m_region >= 0) { m_e->normalizeRegion(m_track, m_region, -1.f); refresh(); }
    });
    setSelection(-1, -1);
}

void Inspector::applyFlex() {
    if (m_updating || m_track < 0 || m_region < 0) return;
    m_e->setRegionFlex(m_track, m_region, m_follow->isChecked(), m_srcBpm->value(), m_semis->value());
}

void Inspector::applyRegionAudio() {
    if (m_updating || m_track < 0 || m_region < 0) return;
    if (std::fabs(m_gain->value() - m_curGain) < 1e-6 && m_fadeIn->value() == m_curFadeIn && m_fadeOut->value() == m_curFadeOut) return;
    m_e->setRegionAudio(m_track, m_region, float(m_gain->value()), qint64(m_fadeIn->value()) * kSampleRate / 1000,
                        qint64(m_fadeOut->value()) * kSampleRate / 1000);
    m_curGain = m_gain->value();
    m_curFadeIn = m_fadeIn->value();
    m_curFadeOut = m_fadeOut->value();
}

void Inspector::openInsert(int slot) {
    const auto trks = m_e->snapshot();
    if (m_track < 0 || m_track >= int(trks.size())) return;
    const auto& ins = trks[size_t(m_track)].inserts;
    if (slot < 0 || slot >= int(ins.size())) return;
    if (auto* win = vst3::createEditor(ins[size_t(slot)], window())) { win->show(); return; }
    (new EffectWindow(ins[size_t(slot)], m_e, window()))->show();
}

void Inspector::setSelection(int t, int r) {
    m_track = t;
    m_region = r;
    const auto trks = m_e->snapshot();
    const bool valid = t >= 0 && t < int(trks.size());
    setEnabled(valid);
    m_updating = true;
    m_inserts->clear();
    m_regionBox->setVisible(false);
    if (valid) {
        const Track& tk = trks[size_t(t)];
        m_name->setText(tk.name);
        m_type->setText(tk.type == TrackType::Audio ? tr("Audio track") : tk.type == TrackType::Instrument ? tr("Software Instrument")
                        : tk.type == TrackType::Drums ? tr("Drum Machine") : tr("Drummer"));
        m_fader->setValue(tk.volume);
        m_pan->setValue(tk.pan);
        m_sendA->setValue(tk.sendA);
        m_sendB->setValue(tk.sendB);
        m_mute->setChecked(tk.mute);
        m_solo->setChecked(tk.solo);
        m_arm->setChecked(tk.armed);
        m_arm->setVisible(tk.type != TrackType::Drums);
        for (const auto& fx : tk.inserts) {
            auto* it = new QListWidgetItem(QString::fromUtf8(fx->displayName()), m_inserts);
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            it->setCheckState(fx->bypass.load() ? Qt::Unchecked : Qt::Checked);
        }
        // Instrument chooser: the built-in instrument or any VST3 instrument.
        m_instrument->setVisible(tk.instrument != nullptr);
        if (tk.instrument) {
            const QString cur = tk.instrumentId == "synth" ? tr("MiniDAW Synth") : tk.instrumentId == "drumkit" ? tr("Drum Kit")
                              : QString::fromUtf8(tk.instrument->kind()).section('|', 0, 0).section('/', -1).section('\\', -1);
            m_instrument->setText(cur);
            auto* menu = new QMenu(m_instrument);
            const QString builtin = tk.type == TrackType::Drummer ? "drumkit" : "synth";
            menu->addAction(builtin == "synth" ? tr("MiniDAW Synth") : tr("Drum Kit"), this, [this, builtin] { m_e->setInstrument(m_track, builtin); });
            bool any = false;
            for (const vst3::PluginInfo& pi : PluginCatalog::list())
                if (pi.instrument) {
                    if (!any) { menu->addSeparator(); any = true; }
                    menu->addAction(pi.name, this, [this, id = pi.id] { m_e->setInstrument(m_track, id); });
                }
            delete m_instrument->menu();
            m_instrument->setMenu(menu);
        }
        if (r >= 0 && r < int(tk.regions.size()) && !tk.regions[size_t(r)].midi) {
            const Region& rg = tk.regions[size_t(r)];
            m_regionBox->setVisible(true);
            m_regionName->setText(rg.name);
            m_follow->setChecked(rg.follow);
            m_srcBpm->setValue(rg.sourceBpm);
            m_semis->setValue(int(std::lround(rg.semitones)));
            m_gain->setValue(double(rg.gainDb));
            m_fadeIn->setValue(int(std::lround(double(rg.fadeIn) * 1000.0 / kSampleRate)));
            m_fadeOut->setValue(int(std::lround(double(rg.fadeOut) * 1000.0 / kSampleRate)));
            m_curGain = m_gain->value();   // rounded to the spin box precision
            m_curFadeIn = m_fadeIn->value();
            m_curFadeOut = m_fadeOut->value();
        }
    } else {
        m_name->clear();
        m_type->setText(tr("No track selected"));
    }
    m_updating = false;
}

void Inspector::tick() {
    const auto trks = m_e->snapshot();
    if (m_track < 0 || m_track >= int(trks.size())) return;
    const Track& t = trks[size_t(m_track)];
    m_meter->setLevel(t.peakL, t.peakR);
    m_updating = true;
    if (m_mute->isChecked() != t.mute) m_mute->setChecked(t.mute);
    if (m_solo->isChecked() != t.solo) m_solo->setChecked(t.solo);
    if (m_arm->isChecked() != t.armed) m_arm->setChecked(t.armed);
    m_updating = false;
}

// ================================================================= InstrumentPanel
InstrumentPanel::InstrumentPanel(Engine* e, QWidget* p) : QWidget(p), m_e(e) {
    m_lay = new QVBoxLayout(this);
    auto* top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Preset")));
    m_preset = new QComboBox;
    top->addWidget(m_preset);
    m_editor = new QPushButton(tr("Open Plug-in Window"));
    top->addWidget(m_editor);
    top->addStretch(1);
    m_lay->addLayout(top);
    m_hint = new QLabel(tr("Select a Software Instrument or Drummer track to see its controls."));
    m_hint->setAlignment(Qt::AlignCenter);
    m_lay->addWidget(m_hint, 1);
    connect(m_preset, &QComboBox::activated, this, [this](int i) {
        m_e->setInstrumentPreset(m_track, i);
        setTrack(m_track);   // rebuild so the knobs show the new values
    });
    connect(m_editor, &QPushButton::clicked, this, [this] {
        if (m_inst) if (auto* win = vst3::createEditor(m_inst, window())) win->show();
    });
    setTrack(-1);
}

void InstrumentPanel::setTrack(int t) {
    m_track = t;
    if (m_panel) { m_lay->removeWidget(m_panel); m_panel->deleteLater(); m_panel = nullptr; }
    const auto trks = m_e->snapshot();
    const bool ok = t >= 0 && t < int(trks.size()) && trks[size_t(t)].instrument;
    m_inst = ok ? trks[size_t(t)].instrument : nullptr;
    m_hint->setVisible(!ok);
    m_preset->setEnabled(ok);
    m_preset->clear();
    m_editor->setVisible(ok && QString::fromUtf8(m_inst->kind()).startsWith("vst3:"));
    if (!ok) return;
    for (int i = 0; i < m_inst->presetCount(); ++i) m_preset->addItem(QString::fromUtf8(m_inst->presetName(i)));
    m_preset->setVisible(m_inst->presetCount() > 0);
    if (m_inst->presetCount() > 0) m_preset->setCurrentIndex(trks[size_t(t)].preset);
    m_panel = new ParamPanel(m_inst, m_e, 7);
    m_lay->insertWidget(1, m_panel, 1);
}

// ================================================================= DrummerPanel
DrummerPanel::DrummerPanel(Engine* e, QWidget* p) : QWidget(p), m_e(e) {
    auto* root = new QVBoxLayout(this);
    m_hint = new QLabel(tr("Double-click a Drummer track lane to create a Drummer region, then select it to edit."));
    m_hint->setAlignment(Qt::AlignCenter);
    root->addWidget(m_hint, 1);
    m_box = new QWidget;
    auto* h = new QHBoxLayout(m_box);
    m_pad = new XYPad;
    h->addWidget(m_pad, 2);
    auto* form = new QGridLayout;
    form->addWidget(new QLabel(tr("Style")), 0, 0);
    m_style = new QComboBox;
    for (int i = 0; i < dsp::drummerStyleCount(); ++i) m_style->addItem(tr(dsp::drummerStyleName(i)));
    form->addWidget(m_style, 0, 1);
    form->addWidget(new QLabel(tr("Bars")), 1, 0);
    m_bars = new QSpinBox;
    m_bars->setRange(1, 32);
    form->addWidget(m_bars, 1, 1);
    form->addWidget(new QLabel(tr("Swing")), 2, 0);
    m_swing = new QDoubleSpinBox;
    m_swing->setRange(0, 1);
    m_swing->setSingleStep(0.05);
    form->addWidget(m_swing, 2, 1);
    m_fills = new QCheckBox(tr("Fills"));
    form->addWidget(m_fills, 3, 0, 1, 2);
    m_new = new QPushButton(tr("New Variation"));
    form->addWidget(m_new, 4, 0, 1, 2);
    form->setRowStretch(5, 1);
    h->addLayout(form, 1);
    root->addWidget(m_box, 1);

    connect(m_pad, &XYPad::pressed, this, [this] { m_e->checkpoint(); });
    connect(m_pad, &XYPad::changed, this, [this](double x, double y) { m_p.complexity = x; m_p.loudness = y; push(); });
    connect(m_style, &QComboBox::activated, this, [this](int i) { m_e->checkpoint(); m_p.style = i; push(); });
    connect(m_bars, &QSpinBox::editingFinished, this, [this] { m_e->checkpoint(); m_p.bars = m_bars->value(); push(); });
    connect(m_swing, &QDoubleSpinBox::editingFinished, this, [this] { m_e->checkpoint(); m_p.swing = m_swing->value(); push(); });
    connect(m_fills, &QCheckBox::toggled, this, [this](bool on) { if (!m_updating) { m_e->checkpoint(); m_p.fills = on; push(); } });
    connect(m_new, &QPushButton::clicked, this, [this] { m_e->checkpoint(); ++m_p.seed; push(); });
    setRegion(-1, -1);
}

void DrummerPanel::push() {
    if (!m_updating && m_t >= 0 && m_r >= 0) m_e->setDrummerParams(m_t, m_r, m_p);
}

void DrummerPanel::setRegion(int t, int r) {
    m_t = t;
    m_r = r;
    const auto trks = m_e->snapshot();
    const bool ok = t >= 0 && t < int(trks.size()) && r >= 0 && r < int(trks[size_t(t)].regions.size()) && trks[size_t(t)].regions[size_t(r)].drummer;
    m_box->setVisible(ok);
    m_hint->setVisible(!ok);
    if (!ok) return;
    m_updating = true;
    m_p = trks[size_t(t)].regions[size_t(r)].drum;
    m_pad->setValue(m_p.complexity, m_p.loudness);
    m_style->setCurrentIndex(m_p.style);
    m_bars->setValue(m_p.bars);
    m_swing->setValue(m_p.swing);
    m_fills->setChecked(m_p.fills);
    m_updating = false;
}
