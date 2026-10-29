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
    void setCompact(bool on);   // small dial without text (label and value go to the tooltip)
    void setFormatter(std::function<QString(double)> f) { m_fmt = std::move(f); update(); }
    QSize sizeHint() const override { return {m_compact ? 44 : 64, m_compact ? 46 : 84}; }
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
    bool m_log, m_stepped = false, m_compact = false;
    std::function<QString(double)> m_fmt;
    double m_pressY = 0, m_pressNorm = 0;
};

/// Vertical fader with a dB scale. The value is a linear gain (0 .. 1.5, unity = 1.0).
class Fader : public QWidget {
    Q_OBJECT
public:
    explicit Fader(QWidget* parent = nullptr);
    void setValue(float gain);
    float value() const { return m_gain; }
    QSize sizeHint() const override { return {38, 180}; }
signals:
    void valueChanged(float gain);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
private:
    float posForGain(float g) const;
    float gainForPos(float p) const;
    int thumbY() const;
    void setFromY(int y);
    float m_gain = 0.8f;
};

/// Two-axis pad (the Drummer's complexity / loudness control).
class XYPad : public QWidget {
    Q_OBJECT
public:
    explicit XYPad(QWidget* parent = nullptr);
    void setValue(double x, double y);   // both 0..1; programmatic, does not emit
    QSize sizeHint() const override { return {220, 160}; }
signals:
    void pressed();
    void changed(double x, double y);    // x: left to right, y: bottom to top
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
private:
    void setFromPos(QPoint p);
    double m_x = 0.5, m_y = 0.5;
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
