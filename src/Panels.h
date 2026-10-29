// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QWidget>
#include <vector>
#include "Controls.h"
#include "Engine.h"
#include "vst3/Vst3Host.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QPushButton;
class QSpinBox;
class QToolButton;
class QVBoxLayout;

/// The VST3 plug-ins found on this computer (empty when the build has no VST3 support).
struct PluginCatalog {
    static std::vector<vst3::PluginInfo>& list();
    static void rescan();   ///< Scans the folders stored in the settings (or the standard VST3 folders).
};

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

/// Full mixer: Logic-style channel strips (FX slots, pan, fader, meter, M/S/R and a coloured name tag).
class MixerPanel : public QWidget {
    Q_OBJECT
public:
    explicit MixerPanel(Engine* e, QWidget* parent = nullptr);
    void rebuild();
    void tick();
signals:
    void trackSelected(int track);
private:
    struct Strip { QWidget* w; QToolButton* name; Fader* fader; QLabel* db; Knob* pan; QToolButton *m, *s, *r; Meter* meter; };
    Engine* m_e;
    QWidget* m_host;
    QHBoxLayout* m_lay;
    std::vector<Strip> m_strips;
    Fader* m_masterFader;
    QLabel* m_masterDb;
    Meter* m_masterMeter;
};

/// Left-hand Inspector: instrument, channel strip, inserts, sends and region settings of the selection.
class Inspector : public QWidget {
    Q_OBJECT
public:
    explicit Inspector(Engine* e, QWidget* parent = nullptr);
    void setSelection(int track, int region);
    void refresh() { setSelection(m_track, m_region); }
    void tick();
private:
    void openInsert(int slot);
    void applyFlex();
    Engine* m_e;
    int m_track = -1, m_region = -1;
    QLabel* m_type;
    QLineEdit* m_name;
    QToolButton* m_instrument;
    Fader* m_fader;
    Meter* m_meter;
    Knob *m_pan, *m_sendA, *m_sendB;
    QToolButton *m_mute, *m_solo, *m_arm;
    QListWidget* m_inserts;
    QToolButton *m_add, *m_remove;
    QWidget* m_regionBox;
    QLabel* m_regionName;
    QCheckBox* m_follow;
    QDoubleSpinBox* m_srcBpm;
    QSpinBox* m_semis;
    bool m_updating = false;
};

/// "Smart Controls": preset chooser and knobs of the selected instrument.
class InstrumentPanel : public QWidget {
    Q_OBJECT
public:
    explicit InstrumentPanel(Engine* e, QWidget* parent = nullptr);
    void setTrack(int t);
private:
    Engine* m_e;
    int m_track = -1;
    QVBoxLayout* m_lay;
    QComboBox* m_preset;
    QPushButton* m_editor;
    QWidget* m_panel = nullptr;
    QLabel* m_hint;
    std::shared_ptr<dsp::Instrument> m_inst;
};

/// Drummer editor: style, XY pad (complexity / loudness), swing, fills and variations for the selected region.
class DrummerPanel : public QWidget {
    Q_OBJECT
public:
    explicit DrummerPanel(Engine* e, QWidget* parent = nullptr);
    void setRegion(int track, int region);
private:
    void push();
    Engine* m_e;
    int m_t = -1, m_r = -1;
    dsp::DrummerParams m_p;
    QComboBox* m_style;
    XYPad* m_pad;
    QSpinBox* m_bars;
    QDoubleSpinBox* m_swing;
    QCheckBox* m_fills;
    QPushButton* m_new;
    QLabel* m_hint;
    QWidget* m_box;
    bool m_updating = false;
};
