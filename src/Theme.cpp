// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Theme.h"

#include <QApplication>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QStyleFactory>

namespace theme {

void apply(QApplication& app) {
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette p;
    p.setColor(QPalette::Window, kBg);
    p.setColor(QPalette::WindowText, kText);
    p.setColor(QPalette::Base, kPanelDark);
    p.setColor(QPalette::AlternateBase, QColor(38, 38, 42));
    p.setColor(QPalette::Text, kText);
    p.setColor(QPalette::Button, kPanel);
    p.setColor(QPalette::ButtonText, kText);
    p.setColor(QPalette::Highlight, kAccent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::ToolTipBase, QColor(50, 50, 55));
    p.setColor(QPalette::ToolTipText, kText);
    p.setColor(QPalette::Disabled, QPalette::Text, kDim);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, kDim);
    app.setPalette(p);
    app.setStyleSheet(R"(
        QMenuBar { background: #232326; }
        QMenuBar::item:selected { background: #3d8bff; }
        QMenu { background: #2f2f33; border: 1px solid #44444a; }
        QMenu::item:selected { background: #3d8bff; }
        QToolButton { background: transparent; border: 1px solid transparent; border-radius: 4px; padding: 3px; }
        QToolButton:hover { background: #3d3d42; }
        QToolButton:checked { background: #4a4a52; border-color: #5a5a64; }
        QToolButton::menu-indicator { image: none; }
        QTabWidget::pane { border-top: 1px solid #1d1d20; }
        QTabBar::tab { background: #2f2f33; padding: 5px 16px; border: 1px solid #1d1d20; border-bottom: none; }
        QTabBar::tab:selected { background: #3a3a3f; color: white; }
        QLineEdit, QComboBox, QDoubleSpinBox, QListWidget { background: #1f1f22; border: 1px solid #45454b; border-radius: 3px; padding: 2px 4px; }
        QSlider::groove:vertical { background: #18181a; width: 6px; border-radius: 3px; }
        QSlider::handle:vertical { background: #d4d4da; height: 18px; margin: 0 -7px; border-radius: 3px; }
        QSlider::groove:horizontal { background: #18181a; height: 6px; border-radius: 3px; }
        QSlider::handle:horizontal { background: #d4d4da; width: 14px; margin: -5px 0; border-radius: 3px; }
        QScrollBar:horizontal { background: #232326; height: 12px; }
        QScrollBar:vertical { background: #232326; width: 12px; }
        QScrollBar::handle { background: #55555c; border-radius: 5px; min-width: 24px; min-height: 24px; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
        QSplitter::handle { background: #1d1d20; }
        QToolTip { background: #323237; color: #dedee2; border: 1px solid #55555c; }
        QGroupBox { border: 1px solid #45454b; border-radius: 4px; margin-top: 10px; padding-top: 6px; }
        QGroupBox::title { subcontrol-origin: margin; left: 8px; color: #8c8c94; }
    )");
}

QIcon icon(Glyph g, const QColor& off, const QColor& on) {
    auto draw = [g](const QColor& c) {
        QPixmap pm(32, 32);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        switch (g) {
        case Glyph::Rewind:
            p.drawRect(6, 8, 3, 16);
            p.drawPolygon(QPolygonF({QPointF(24, 8), QPointF(24, 24), QPointF(11, 16)}));
            break;
        case Glyph::Play:
            p.drawPolygon(QPolygonF({QPointF(9, 6), QPointF(9, 26), QPointF(26, 16)}));
            break;
        case Glyph::Stop:
            p.drawRoundedRect(8, 8, 16, 16, 2, 2);
            break;
        case Glyph::Record:
            p.drawEllipse(QPointF(16, 16), 9, 9);
            break;
        case Glyph::Cycle:
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(c, 3, Qt::SolidLine, Qt::RoundCap));
            p.drawArc(QRectF(6, 7, 20, 18), 30 * 16, 290 * 16);
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            p.drawPolygon(QPolygonF({QPointF(21, 3), QPointF(29, 10), QPointF(19, 12)}));
            break;
        case Glyph::Metronome:
            p.drawPolygon(QPolygonF({QPointF(11, 5), QPointF(21, 5), QPointF(26, 27), QPointF(6, 27)}));
            p.setPen(QPen(kBg, 2, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(16, 24, 21, 9);
            break;
        case Glyph::CountIn:
            p.setPen(QPen(c, 2));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QPointF(16, 16), 10, 10);
            p.setFont(QFont("Segoe UI", 11, QFont::Bold));
            p.drawText(QRectF(6, 6, 20, 20), Qt::AlignCenter, "1");
            break;
        }
        return pm;
    };
    QIcon ic;
    ic.addPixmap(draw(off), QIcon::Normal, QIcon::Off);
    ic.addPixmap(draw(on), QIcon::Normal, QIcon::On);
    return ic;
}

}  // namespace theme
