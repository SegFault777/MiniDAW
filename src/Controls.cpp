// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Controls.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <cmath>
#include "Theme.h"

namespace {
constexpr double kPi = 3.14159265358979323846;
}

// ------------------------------------------------------------------ Meter
Meter::Meter(QWidget* p) : QWidget(p) {
    setFixedWidth(14);
    setMinimumHeight(60);
}

void Meter::setLevel(float l, float r) {
    m_l = std::max(l, m_l * 0.85f);   // instant attack, exponential fall
    m_r = std::max(r, m_r * 0.85f);
    update();
}

void Meter::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(18, 18, 20));
    auto bar = [&](int x, int w, float v) {
        const float db = 20.f * std::log10(std::max(v, 1e-4f));
        const float fr = std::clamp((db + 60.f) / 60.f, 0.f, 1.f);
        const int h = int(fr * height());
        const QColor c = fr > 0.97f ? theme::kRed : fr > 0.82f ? theme::kYellow : theme::kGreen;
        p.fillRect(x, height() - h, w, h, c);
    };
    bar(1, width() / 2 - 1, m_l);
    bar(width() / 2 + 1, width() / 2 - 1, m_r);
}

// ------------------------------------------------------------------ Knob
Knob::Knob(const QString& label, double mn, double mx, double def, bool log, QWidget* parent)
    : QWidget(parent), m_label(label), m_min(mn), m_max(mx), m_def(def), m_v(def), m_log(log) {
    setFixedSize(64, 84);
    setCursor(Qt::SizeVerCursor);
}

double Knob::norm() const {
    if (m_max <= m_min) return 0;
    if (m_log && m_min > 0) return (std::log(m_v) - std::log(m_min)) / (std::log(m_max) - std::log(m_min));
    return (m_v - m_min) / (m_max - m_min);
}

void Knob::setNorm(double n) {
    n = std::clamp(n, 0.0, 1.0);
    double v = (m_log && m_min > 0) ? std::exp(std::log(m_min) + n * (std::log(m_max) - std::log(m_min)))
                                    : m_min + n * (m_max - m_min);
    if (m_stepped) v = std::round(v);
    m_v = std::clamp(v, m_min, m_max);
}

void Knob::setCompact(bool on) {
    m_compact = on;
    setFixedSize(on ? QSize(44, 46) : QSize(64, 84));
    update();
}

void Knob::setValue(double v) {
    m_v = std::clamp(v, m_min, m_max);
    update();
}

void Knob::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = m_compact ? QRectF(width() / 2.0 - 15, 6, 30, 30) : QRectF(width() / 2.0 - 19, 6, 38, 38);
    const double n = norm();
    p.setPen(QPen(QColor(24, 24, 27), 5, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(r, 225 * 16, -270 * 16);
    p.setPen(QPen(theme::kAccent, 5, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(r, 225 * 16, int(-270 * 16 * n));
    const double a = (225.0 - 270.0 * n) * kPi / 180.0;
    const QPointF c = r.center(), dir(std::cos(a), -std::sin(a));
    p.setPen(QPen(theme::kText, 2, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(c + dir * 5, c + dir * (m_compact ? 11 : 14));
    if (m_compact) {
        setToolTip(QString("%1: %2").arg(m_label, m_fmt ? m_fmt(m_v) : QString::number(m_v, 'g', 3)));
        return;
    }
    p.setFont(QFont(font().family(), 8));
    p.setPen(theme::kDim);
    p.drawText(QRectF(0, 48, width(), 15), Qt::AlignCenter, fontMetrics().elidedText(m_label, Qt::ElideRight, width()));
    p.setPen(theme::kText);
    p.drawText(QRectF(0, 63, width(), 15), Qt::AlignCenter, m_fmt ? m_fmt(m_v) : QString::number(m_v, 'g', 3));
}

void Knob::mousePressEvent(QMouseEvent* e) {
    m_pressY = e->position().y();
    m_pressNorm = norm();
}

void Knob::mouseMoveEvent(QMouseEvent* e) {
    if (!(e->buttons() & Qt::LeftButton)) return;
    const double range = (e->modifiers() & Qt::ShiftModifier) ? 600.0 : 160.0;  // Shift = fine adjustment
    setNorm(m_pressNorm + (m_pressY - e->position().y()) / range);
    update();
    emit valueChanged(m_v);
}

void Knob::mouseDoubleClickEvent(QMouseEvent*) {
    m_v = m_def;
    update();
    emit valueChanged(m_v);
}

void Knob::wheelEvent(QWheelEvent* e) {
    setNorm(norm() + (e->angleDelta().y() > 0 ? 0.02 : -0.02));
    update();
    emit valueChanged(m_v);
    e->accept();
}

// ------------------------------------------------------------------ ParamPanel
ParamPanel::ParamPanel(std::shared_ptr<dsp::ParamSet> set, Engine* engine, int columns, QWidget* parent)
    : QWidget(parent), m_set(std::move(set)) {
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(4, 4, 4, 4);
    grid->setSpacing(2);
    int i = 0;
    for (const auto& up : m_set->params) {
        dsp::Param* p = up.get();
        auto* k = new Knob(QString::fromStdString(p->name), p->min, p->max, p->def, p->log);
        k->setStepped(p->stepped);
        k->setValue(p->get());
        k->setFormatter([p](double v) {
            if (!p->choices.empty()) return QString::fromStdString(p->choices[size_t(std::clamp(int(v), 0, int(p->choices.size()) - 1))]);
            return QString::number(v, 'g', 3) + (p->unit.empty() ? QString() : " " + QString::fromStdString(p->unit));
        });
        connect(k, &Knob::valueChanged, this, [p, engine](double v) {
            p->set(float(v));
            engine->markDirty();
        });
        grid->addWidget(k, i / columns, i % columns);
        ++i;
    }
    grid->setRowStretch(i / columns + 1, 1);
    grid->setColumnStretch(columns, 1);
}

// ------------------------------------------------------------------ EffectWindow
EffectWindow::EffectWindow(std::shared_ptr<dsp::Effect> fx, Engine* engine, QWidget* parent) : QDialog(parent) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(QString::fromUtf8(fx->displayName()));
    auto* lay = new QVBoxLayout(this);
    auto* bypass = new QCheckBox(tr("Bypass"));
    bypass->setChecked(fx->bypass.load());
    connect(bypass, &QCheckBox::toggled, this, [fx, engine](bool on) { fx->bypass = on; engine->markDirty(); });
    lay->addWidget(bypass);
    lay->addWidget(new ParamPanel(fx, engine, 6));
    resize(440, 80 + 90 * int((fx->params.size() + 5) / 6));
}

// ------------------------------------------------------------------ Fader
Fader::Fader(QWidget* p) : QWidget(p) {
    setMinimumSize(38, 120);
    setCursor(Qt::SizeVerCursor);
}

// The scale runs from -60 dB (bottom) to +6 dB (top); a gain of 0 sits at the very bottom.
float Fader::posForGain(float g) const {
    if (g <= 0.001f) return 0.f;
    return std::clamp((20.f * std::log10(g) + 60.f) / 66.f, 0.f, 1.f);
}

float Fader::gainForPos(float p) const {
    if (p <= 0.004f) return 0.f;
    return std::min(1.5f, std::pow(10.f, (p * 66.f - 60.f) / 20.f));
}

int Fader::thumbY() const {
    const int top = 10, bottom = height() - 10;
    return bottom - int(posForGain(m_gain) * float(bottom - top));
}

void Fader::setValue(float g) {
    m_gain = std::clamp(g, 0.f, 1.5f);
    update();
}

void Fader::setFromY(int y) {
    const int top = 10, bottom = height() - 10;
    m_gain = gainForPos(1.f - float(y - top) / float(bottom - top));
    update();
    emit valueChanged(m_gain);
}

void Fader::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int top = 10, bottom = height() - 10, cx = width() / 2 + 6;
    p.setFont(QFont(font().family(), 7));
    for (float db : {6.f, 0.f, -6.f, -12.f, -24.f, -48.f}) {   // dB scale like a console fader
        const int y = bottom - int(posForGain(std::pow(10.f, db / 20.f)) * float(bottom - top));
        p.setPen(db == 0.f ? theme::kText : theme::kDim);
        p.drawLine(cx - 15, y, cx - 11, y);
        p.drawText(QRect(0, y - 6, cx - 17, 12), Qt::AlignRight | Qt::AlignVCenter, QString::number(int(db)));
    }
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(14, 14, 16));
    p.drawRoundedRect(QRect(cx - 3, top, 6, bottom - top), 3, 3);
    const int ty = thumbY();
    p.setBrush(QColor(205, 205, 212));
    p.drawRoundedRect(QRect(cx - 11, ty - 8, 22, 16), 3, 3);
    p.setPen(QPen(QColor(60, 60, 66), 2));
    p.drawLine(cx - 8, ty, cx + 8, ty);
}

void Fader::mousePressEvent(QMouseEvent* e) { setFromY(int(e->position().y())); }
void Fader::mouseMoveEvent(QMouseEvent* e) { if (e->buttons() & Qt::LeftButton) setFromY(int(e->position().y())); }

void Fader::mouseDoubleClickEvent(QMouseEvent*) {   // double-click returns to 0 dB
    m_gain = 1.f;
    update();
    emit valueChanged(m_gain);
}

void Fader::wheelEvent(QWheelEvent* e) {
    const float p = posForGain(m_gain) + (e->angleDelta().y() > 0 ? 0.01f : -0.01f);
    m_gain = gainForPos(std::clamp(p, 0.f, 1.f));
    update();
    emit valueChanged(m_gain);
    e->accept();
}

// ------------------------------------------------------------------ XYPad
XYPad::XYPad(QWidget* p) : QWidget(p) {
    setMinimumSize(160, 120);
    setCursor(Qt::CrossCursor);
}

void XYPad::setValue(double x, double y) {
    m_x = std::clamp(x, 0.0, 1.0);
    m_y = std::clamp(y, 0.0, 1.0);
    update();
}

void XYPad::setFromPos(QPoint pt) {
    const QRect r = rect().adjusted(6, 6, -6, -6);
    m_x = std::clamp(double(pt.x() - r.left()) / r.width(), 0.0, 1.0);
    m_y = std::clamp(1.0 - double(pt.y() - r.top()) / r.height(), 0.0, 1.0);
    update();
    emit changed(m_x, m_y);
}

void XYPad::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRect r = rect().adjusted(6, 6, -6, -6);
    p.setPen(QColor(10, 10, 12));
    p.setBrush(QColor(24, 24, 27));
    p.drawRoundedRect(r, 8, 8);
    p.setPen(QColor(255, 255, 255, 22));
    for (int i = 1; i < 4; ++i) {
        p.drawLine(r.left() + r.width() * i / 4, r.top(), r.left() + r.width() * i / 4, r.bottom());
        p.drawLine(r.left(), r.top() + r.height() * i / 4, r.right(), r.top() + r.height() * i / 4);
    }
    p.setFont(QFont(font().family(), 8));
    p.setPen(theme::kDim);
    p.drawText(r.adjusted(8, 0, 0, -4), Qt::AlignLeft | Qt::AlignBottom, tr("Simple"));
    p.drawText(r.adjusted(0, 0, -8, -4), Qt::AlignRight | Qt::AlignBottom, tr("Complex"));
    p.drawText(r.adjusted(8, 4, 0, 0), Qt::AlignLeft | Qt::AlignTop, tr("Loud"));
    const QPointF c(r.left() + m_x * r.width(), r.top() + (1.0 - m_y) * r.height());
    p.setPen(QPen(QColor(235, 200, 70), 2));
    p.setBrush(QColor(235, 200, 70, 90));
    p.drawEllipse(c, 11, 11);
    p.setBrush(QColor(235, 200, 70));
    p.drawEllipse(c, 3, 3);
}

void XYPad::mousePressEvent(QMouseEvent* e) {
    emit pressed();
    setFromPos(e->position().toPoint());
}

void XYPad::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons() & Qt::LeftButton) setFromPos(e->position().toPoint());
}
