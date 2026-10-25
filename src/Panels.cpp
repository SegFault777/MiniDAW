// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Panels.h"

#include <QComboBox>
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
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>
#include "Theme.h"

// ================================================================= StepSequencer
static const char* kRowNames[kDrumRows] = {"Kick", "Snare", "Hi-Hat", "Clap"};

StepSequencer::StepSequencer(Engine* e, QWidget* p) : QWidget(p), m_e(e) { setMinimumHeight(150); }

bool StepSequencer::isDrums() const {
    const auto tr = m_e->snapshot();
    return m_track >= 0 && m_track < int(tr.size()) && tr[size_t(m_track)].type == TrackType::Drums;
}

QRectF StepSequencer::cell(int r, int s) const {
    const double lw = 80, cw = (width() - lw - 8) / kSteps, ch = (height() - 8) / double(kDrumRows);
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
        p.drawText(QRectF(8, cell(r, 0).top(), 70, cell(r, 0).height()), Qt::AlignVCenter, kRowNames[r]);
        for (int s = 0; s < kSteps; ++s) {
            QColor fill = ((s / 4) % 2) ? QColor(50, 50, 55) : QColor(62, 62, 68);
            if (pat[size_t(r)][size_t(s)]) fill = (s % 4 == 0) ? theme::kOrange.lighter(115) : theme::kOrange;
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
    m_lay->setAlignment(Qt::AlignLeft);
    sa->setWidget(m_host);
    root->addWidget(sa, 1);

    auto* mw = new QFrame;
    mw->setFrameShape(QFrame::StyledPanel);
    mw->setFixedWidth(92);
    auto* v = new QVBoxLayout(mw);
    auto* lbl = new QLabel(tr("Master"));
    lbl->setAlignment(Qt::AlignCenter);
    v->addWidget(lbl);
    auto* h = new QHBoxLayout;
    m_masterVol = new QSlider(Qt::Vertical);
    m_masterVol->setRange(0, 120);
    m_masterVol->setValue(90);
    m_masterMeter = new Meter;
    h->addWidget(m_masterVol);
    h->addWidget(m_masterMeter);
    v->addLayout(h, 1);
    root->addWidget(mw);
    connect(m_masterVol, &QSlider::valueChanged, this, [this](int val) { m_e->setMasterVolume(val / 100.f); });
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
        w->setFixedWidth(92);
        auto* v = new QVBoxLayout(w);
        v->setContentsMargins(6, 6, 6, 6);
        v->setSpacing(4);
        auto* name = new QToolButton;
        name->setText(t.name);
        name->setToolButtonStyle(Qt::ToolButtonTextOnly);
        name->setStyleSheet(QString("QToolButton{color:%1;font-weight:bold}").arg(t.color.name()));
        name->setToolTip(t.name);
        v->addWidget(name);
        auto* h = new QHBoxLayout;
        auto* vol = new QSlider(Qt::Vertical);
        vol->setRange(0, 150);
        vol->setValue(int(std::lround(t.volume * 100)));
        auto* meter = new Meter;
        h->addWidget(vol);
        h->addWidget(meter);
        v->addLayout(h, 1);
        auto* pan = new QSlider(Qt::Horizontal);
        pan->setRange(-100, 100);
        pan->setValue(int(std::lround(t.pan * 100)));
        pan->setToolTip(tr("Pan"));
        v->addWidget(pan);
        auto* bh = new QHBoxLayout;
        auto mk = [&](const QString& txt, const QString& tip) {
            auto* b = new QToolButton;
            b->setText(txt);
            b->setCheckable(true);
            b->setToolTip(tip);
            bh->addWidget(b);
            return b;
        };
        QToolButton* m = mk("M", tr("Mute"));
        QToolButton* s = mk("S", tr("Solo"));
        QToolButton* r = mk("R", tr("Record enable"));
        r->setVisible(t.type != TrackType::Drums);
        v->addLayout(bh);
        m_lay->addWidget(w);
        m_strips.push_back({w, name, vol, pan, m, s, r, meter});

        connect(name, &QToolButton::clicked, this, [this, i] { emit trackSelected(i); });
        connect(vol, &QSlider::valueChanged, this, [this, i](int val) { m_e->setVolume(i, val / 100.f); });
        connect(pan, &QSlider::valueChanged, this, [this, i](int val) { m_e->setPan(i, val / 100.f); });
        connect(m, &QToolButton::toggled, this, [this, i](bool on) { m_e->setMute(i, on); });
        connect(s, &QToolButton::toggled, this, [this, i](bool on) { m_e->setSolo(i, on); });
        connect(r, &QToolButton::toggled, this, [this, i](bool on) { m_e->setArmed(i, on); });
    }
}

void MixerPanel::tick() {
    const auto tr = m_e->snapshot();
    if (tr.size() != m_strips.size()) { rebuild(); return; }
    for (size_t i = 0; i < tr.size(); ++i) {
        Strip& s = m_strips[i];
        const Track& t = tr[i];
        s.meter->setLevel(t.peakL, t.peakR);
        if (s.name->text() != t.name) s.name->setText(t.name);
        const int vv = int(std::lround(t.volume * 100)), pp = int(std::lround(t.pan * 100));
        if (!s.vol->isSliderDown() && s.vol->value() != vv) { QSignalBlocker b(s.vol); s.vol->setValue(vv); }
        if (!s.pan->isSliderDown() && s.pan->value() != pp) { QSignalBlocker b(s.pan); s.pan->setValue(pp); }
        if (s.m->isChecked() != t.mute) { QSignalBlocker b(s.m); s.m->setChecked(t.mute); }
        if (s.s->isChecked() != t.solo) { QSignalBlocker b(s.s); s.s->setChecked(t.solo); }
        if (s.r->isChecked() != t.armed) { QSignalBlocker b(s.r); s.r->setChecked(t.armed); }
    }
    float l, r;
    m_e->masterPeaks(l, r);
    m_masterMeter->setLevel(l, r);
}

// ================================================================= Inspector
static QString effectLabel(const QString& id) {
    auto fx = dsp::createEffect(id.toStdString());
    return fx ? QString::fromUtf8(fx->displayName()) : id;
}

Inspector::Inspector(Engine* e, QWidget* p) : QWidget(p), m_e(e) {
    setMinimumWidth(230);
    setMaximumWidth(300);
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

    // Channel strip.
    auto* strip = new QHBoxLayout;
    m_vol = new QSlider(Qt::Vertical);
    m_vol->setRange(0, 150);
    m_vol->setMinimumHeight(130);
    m_meter = new Meter;
    strip->addWidget(m_vol);
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
    m_inserts->setMaximumHeight(110);
    lay->addWidget(m_inserts);
    auto* ib = new QHBoxLayout;
    m_add = new QToolButton;
    m_add->setText(tr("+ Add"));
    m_add->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(m_add);
    for (const std::string& id : dsp::insertEffectIds()) {
        const QString qid = QString::fromStdString(id);
        menu->addAction(effectLabel(qid), this, [this, qid] { if (m_track >= 0) m_e->addInsert(m_track, qid); });
    }
    m_add->setMenu(menu);
    m_remove = new QToolButton;
    m_remove->setText(tr("− Remove"));
    ib->addWidget(m_add);
    ib->addWidget(m_remove);
    ib->addStretch(1);
    lay->addLayout(ib);

    // Aux buses.
    auto* auxA = new QPushButton(tr("Aux A: Reverb…"));
    auto* auxB = new QPushButton(tr("Aux B: Echo…"));
    lay->addWidget(auxA);
    lay->addWidget(auxB);
    lay->addStretch(1);
    connect(auxA, &QPushButton::clicked, this, [this] { (new EffectWindow(m_e->auxEffect(0), m_e, window()))->show(); });
    connect(auxB, &QPushButton::clicked, this, [this] { (new EffectWindow(m_e->auxEffect(1), m_e, window()))->show(); });

    // Edits.
    connect(m_name, &QLineEdit::editingFinished, this, [this] {
        if (!m_updating && m_track >= 0 && !m_name->text().isEmpty()) m_e->renameTrack(m_track, m_name->text());
    });
    connect(m_vol, &QSlider::valueChanged, this, [this](int v) { if (!m_updating) m_e->setVolume(m_track, v / 100.f); });
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
    setTrack(-1);
}

void Inspector::openInsert(int slot) {
    const auto tr = m_e->snapshot();
    if (m_track < 0 || m_track >= int(tr.size())) return;
    const auto& ins = tr[size_t(m_track)].inserts;
    if (slot >= 0 && slot < int(ins.size())) (new EffectWindow(ins[size_t(slot)], m_e, window()))->show();
}

void Inspector::setTrack(int t) {
    m_track = t;
    const auto trks = m_e->snapshot();
    const bool valid = t >= 0 && t < int(trks.size());
    setEnabled(valid);
    m_updating = true;
    m_inserts->clear();
    if (valid) {
        const Track& tk = trks[size_t(t)];
        m_name->setText(tk.name);
        m_type->setText(tk.type == TrackType::Audio ? tr("Audio track") : tk.type == TrackType::Instrument ? tr("Software Instrument") : tr("Drum Machine"));
        m_vol->setValue(int(std::lround(tk.volume * 100)));
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
    } else {
        m_name->clear();
        m_type->setText(tr("No track selected"));
    }
    m_updating = false;
}

void Inspector::tick() {
    const auto tr = m_e->snapshot();
    if (m_track < 0 || m_track >= int(tr.size())) return;
    const Track& t = tr[size_t(m_track)];
    m_meter->setLevel(t.peakL, t.peakR);
    m_updating = true;
    if (m_mute->isChecked() != t.mute) m_mute->setChecked(t.mute);
    if (m_solo->isChecked() != t.solo) m_solo->setChecked(t.solo);
    if (m_arm->isChecked() != t.armed) m_arm->setChecked(t.armed);
    m_updating = false;
}

// ================================================================= SynthPanel
SynthPanel::SynthPanel(Engine* e, QWidget* p) : QWidget(p), m_e(e) {
    m_lay = new QVBoxLayout(this);
    auto* top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Preset")));
    m_preset = new QComboBox;
    for (int i = 0; i < dsp::Synth::presetCount(); ++i) m_preset->addItem(dsp::Synth::presetName(i));
    top->addWidget(m_preset);
    top->addStretch(1);
    m_lay->addLayout(top);
    m_hint = new QLabel(tr("Select a Software Instrument track to see its synth controls."));
    m_hint->setAlignment(Qt::AlignCenter);
    m_lay->addWidget(m_hint, 1);
    connect(m_preset, &QComboBox::activated, this, [this](int i) {
        m_e->setSynthPreset(m_track, i);
        setTrack(m_track);   // rebuild so the knobs show the new values
    });
    setTrack(-1);
}

void SynthPanel::setTrack(int t) {
    m_track = t;
    if (m_panel) { m_lay->removeWidget(m_panel); m_panel->deleteLater(); m_panel = nullptr; }
    const auto tr = m_e->snapshot();
    const bool ok = t >= 0 && t < int(tr.size()) && tr[size_t(t)].synth;
    m_preset->setEnabled(ok);
    m_hint->setVisible(!ok);
    if (!ok) return;
    m_preset->setCurrentIndex(tr[size_t(t)].synthPreset);
    m_panel = new ParamPanel(tr[size_t(t)].synth, m_e, 7);
    m_lay->insertWidget(1, m_panel, 1);
}
