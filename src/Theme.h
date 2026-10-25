// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QColor>
#include <QIcon>
#include <QString>

class QApplication;

namespace theme {

inline const QColor kBg(43, 43, 46);
inline const QColor kPanel(52, 52, 56);
inline const QColor kPanelDark(30, 30, 33);
inline const QColor kAccent(61, 139, 255);
inline const QColor kText(222, 222, 226);
inline const QColor kDim(140, 140, 148);
inline const QColor kRed(235, 68, 68);
inline const QColor kGreen(70, 200, 110);
inline const QColor kYellow(250, 200, 60);
inline const QColor kOrange(255, 159, 28);

/// Installs the dark Fusion palette and the application style sheet.
void apply(QApplication& app);

/// Shapes of the transport icons drawn at runtime (no image resources needed).
enum class Glyph { Rewind, Play, Stop, Record, Cycle, Metronome, CountIn };

/// Returns an icon that uses 'off' normally and 'on' while the (checkable) action is checked.
QIcon icon(Glyph g, const QColor& off, const QColor& on);

}  // namespace theme
