// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Conversions between the Qt-free core types and the Qt user interface.
#include <QColor>
#include <QString>
#include <string>
#include "core/Model.h"

inline QString qs(const std::string& s) { return QString::fromStdString(s); }
inline QColor qc(core::Color c) { return QColor(c.r, c.g, c.b); }
