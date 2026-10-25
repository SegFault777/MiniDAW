// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QWidget>
#include "Engine.h"

class QComboBox;
class RollCanvas;

/// MIDI note editor (Logic's "Piano Roll"): draw, move, resize, delete, velocity lane and quantize.
class PianoRoll : public QWidget {
    Q_OBJECT
public:
    explicit PianoRoll(Engine* e, QWidget* parent = nullptr);
    void setRegion(int track, int region);
    void deleteSelectedNote();
    bool hasSelectedNote() const;
private:
    RollCanvas* m_canvas;
    QComboBox* m_grid;
};
