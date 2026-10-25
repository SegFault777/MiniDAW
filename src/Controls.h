// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDialog>
#include <QWidget>
#include <functional>
#include <memory>
#include "Engine.h"

/// Stereo level meter with dB scaling and a falling ballistic.
class Meter : public QWidget {
    Q_OBJECT
public:
    explicit Meter(QWidget* parent = nullptr);
    void setLevel(float l, float r);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    float m_l = 0, m_r = 0;
};

/// Rotary knob: drag vertically, wheel to nudge, double-click to reset.
class Knob : public QWidget {
    Q_OBJECT
public:
    Knob(const QString& label, double min, double max, double def, bool log = false, QWidget* parent = nullptr);
    void setValue(double v);   // programmatic change, does not emit valueChanged
    double value() const { return m_v; }
    void setStepped(bool on) { m_stepped = on; }
    void setFormatter(std::function<QString(double)> f) { m_fmt = std::move(f); update(); }
    QSize sizeHint() const override { return {64, 84}; }
signals:
    void valueChanged(double v);   // emitted for user changes only
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
private:
    double norm() const;
    void setNorm(double n);
    QString m_label;
    double m_min, m_max, m_def, m_v;
    bool m_log, m_stepped = false;
    std::function<QString(double)> m_fmt;
    double m_pressY = 0, m_pressNorm = 0;
};

/// A grid of knobs bound to every parameter of an effect or synth.
class ParamPanel : public QWidget {
    Q_OBJECT
public:
    ParamPanel(std::shared_ptr<dsp::ParamSet> set, Engine* engine, int columns = 7, QWidget* parent = nullptr);
private:
    std::shared_ptr<dsp::ParamSet> m_set;   // keeps the parameters alive while knobs reference them
};

/// Floating editor window for one insert or aux effect.
class EffectWindow : public QDialog {
    Q_OBJECT
public:
    EffectWindow(std::shared_ptr<dsp::Effect> fx, Engine* engine, QWidget* parent = nullptr);
};
