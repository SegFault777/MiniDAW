// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QScrollBar>
#include <QWidget>
#include "Engine.h"

/// The "Tracks" area: ruler with bars and time signatures, optional tempo track, track headers,
/// regions, cycle range and volume automation.
class Timeline : public QWidget {
    Q_OBJECT
public:
    explicit Timeline(Engine* e, QWidget* parent = nullptr);

    int selectedTrack() const { return m_selTrack; }
    int selectedRegion() const { return m_selRegion; }
    void selectTrack(int t);
    void selectRegion(int t, int r);
    void deleteSelected();
    void splitAtPlayhead();
    void duplicateSelected();
    void copySelected();               ///< Copy / cut / paste of regions (v0.5).
    void cutSelected();
    void pasteAtPlayhead();
    void duplicateSelectedTrack();
    void setAutomationMode(bool on);
    void showTempoTrack(bool on);
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
    enum class Drag { None, Playhead, Move, Resize, Loop, AutoPoint, TempoPoint };
    static constexpr int kHeaderW = 190, kRulerH = 34, kLaneH = 80, kTempoH = 72;

    int lanesTop() const { return kRulerH + (m_showTempo ? kTempoH : 0); }
    int beatToX(double beat) const;
    double xToBeat(int x) const;
    double gridBeats() const;
    double snapBeat(double beat) const;
    int laneY(int i) const { return lanesTop() + i * kLaneH - m_vbar->value(); }
    int laneAt(int y) const;
    QRect muteRect(int i) const;
    QRect soloRect(int i) const;
    QRect armRect(int i) const;
    bool hitRegion(const std::vector<Track>& tr, QPoint pt, int& t, int& r, bool& edge) const;
    float yToVolume(int y, int lane) const;
    int volumeToY(float v, int lane) const;
    int bpmToY(double bpm) const;
    double yToBpm(int y) const;
    int tempoPointAt(const std::vector<dsp::TempoPoint>& pts, QPoint pt) const;
    void tempoPress(QMouseEvent* e);
    void regionMenu(const QPoint& globalPos, int track, int region, double beat);

    Engine* m_e;
    QScrollBar *m_hbar, *m_vbar;
    double m_ppb = 56.0;   // pixels per beat (zoom)
    bool m_snap = true, m_autoMode = false, m_showTempo = false;
    int m_selTrack = -1, m_selRegion = -1, m_autoIdx = -1, m_tempoIdx = -1;
    Drag m_drag = Drag::None;
    double m_dragOff = 0, m_loopAnchor = 0;
};
