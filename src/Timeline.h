// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QWidget>
#include "Engine.h"

#include <QScrollBar>

/// The "Tracks" area: ruler, track headers, regions, cycle range and volume automation.
class Timeline : public QWidget {
    Q_OBJECT
public:
    explicit Timeline(Engine* e, QWidget* parent = nullptr);

    int selectedTrack() const { return m_selTrack; }
    int selectedRegion() const { return m_selRegion; }
    void selectTrack(int t);
    void deleteSelected();
    void splitAtPlayhead();
    void duplicateSelected();
    void setAutomationMode(bool on);
    void setSnap(bool on) { m_snap = on; }
    void refreshLayout();     ///< Recomputes scroll ranges after the project changed.
    void followPlayhead();    ///< Scrolls horizontally to keep the playhead visible.

signals:
    void selectionChanged(int track, int region);
    void regionOpened(int track, int region);   ///< Double-click on a MIDI region.

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;

private:
    enum class Drag { None, Playhead, Move, Resize, Loop, AutoPoint };
    static constexpr int kHeaderW = 170, kRulerH = 28, kLaneH = 76;

    int beatToX(double beat) const;
    double xToBeat(int x) const;
    double gridBeats() const;
    double snapBeat(double beat) const;
    int laneY(int i) const { return kRulerH + i * kLaneH - m_vbar->value(); }
    int laneAt(int y) const;
    QRect muteRect(int i) const;
    QRect soloRect(int i) const;
    QRect armRect(int i) const;
    bool hitRegion(const std::vector<Track>& tr, QPoint pt, int& t, int& r, bool& edge) const;
    float yToVolume(int y, int lane) const;
    int volumeToY(float v, int lane) const;

    Engine* m_e;
    QScrollBar *m_hbar, *m_vbar;
    double m_ppb = 56.0;   // pixels per beat (zoom)
    bool m_snap = true, m_autoMode = false;
    int m_selTrack = -1, m_selRegion = -1, m_autoIdx = -1;
    Drag m_drag = Drag::None;
    double m_dragOff = 0, m_loopAnchor = 0;
};
