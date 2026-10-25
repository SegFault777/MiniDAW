// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QWidget>
#include <vector>
#include "Controls.h"
#include "Engine.h"

class QComboBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QSlider;
class QToolButton;
class QVBoxLayout;

/// 16-step pattern editor for the selected Drum Machine track.
class StepSequencer : public QWidget {
    Q_OBJECT
public:
    explicit StepSequencer(Engine* e, QWidget* parent = nullptr);
    void setTrack(int t) { m_track = t; update(); }
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
private:
    QRectF cell(int r, int s) const;
    bool hit(const QPointF& p, int& r, int& s) const;
    bool isDrums() const;
    Engine* m_e;
    int m_track = -1, m_lastR = -1, m_lastS = -1;
    bool m_drag = false, m_val = false;
};

/// Full mixer: one strip per track plus the master strip.
class MixerPanel : public QWidget {
    Q_OBJECT
public:
    explicit MixerPanel(Engine* e, QWidget* parent = nullptr);
    void rebuild();
    void tick();
signals:
    void trackSelected(int track);
private:
    struct Strip { QWidget* w; QToolButton* name; QSlider* vol; QSlider* pan; QToolButton *m, *s, *r; Meter* meter; };
    Engine* m_e;
    QWidget* m_host;
    QHBoxLayout* m_lay;
    std::vector<Strip> m_strips;
    QSlider* m_masterVol;
    Meter* m_masterMeter;
};

/// Left-hand Inspector: channel strip, inserts and sends of the selected track.
class Inspector : public QWidget {
    Q_OBJECT
public:
    explicit Inspector(Engine* e, QWidget* parent = nullptr);
    void setTrack(int t);
    void refresh() { setTrack(m_track); }
    void tick();
private:
    void openInsert(int slot);
    Engine* m_e;
    int m_track = -1;
    QLabel *m_type;
    QLineEdit* m_name;
    QSlider *m_vol;
    Meter* m_meter;
    Knob *m_pan, *m_sendA, *m_sendB;
    QToolButton *m_mute, *m_solo, *m_arm;
    QListWidget* m_inserts;
    QToolButton *m_add, *m_remove;
    bool m_updating = false;
};

/// "Smart Controls": preset chooser and knobs of the selected instrument's synth.
class SynthPanel : public QWidget {
    Q_OBJECT
public:
    explicit SynthPanel(Engine* e, QWidget* parent = nullptr);
    void setTrack(int t);
private:
    Engine* m_e;
    int m_track = -1;
    QVBoxLayout* m_lay;
    QComboBox* m_preset;
    QWidget* m_panel = nullptr;
    QLabel* m_hint;
};
