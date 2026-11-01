// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Qt-free user interface (v0.9): control bar, arrangement, mixer, menus. It draws into a ui::Canvas and reacts to
// plain mouse / keyboard events, so the window layer is only a thin shell and the whole UI is testable headless.
#include <functional>
#include <string>
#include <vector>
#include "../core/Session.h"
#include "Canvas.h"

namespace ui {

enum Mod { ModCtrl = 1, ModShift = 2, ModAlt = 4 };
// Letters are passed as their upper-case ASCII code, digits as '0'..'9'.
enum Key { KeySpace = ' ', KeyDelete = 256, KeyBackspace, KeyLeft, KeyRight, KeyUp, KeyDown, KeyHome, KeyEnd, KeyEnter, KeyEscape, KeyPlus, KeyMinus };

/// What the user interface needs from the operating system.
struct Host {
    /// Native open / save dialog. Returns the chosen path or "" when cancelled. 'filter' is "Name|*.ext;*.ext".
    std::function<std::string(const std::string& title, const std::string& filter, bool save)> fileDialog;
    std::function<void()> quit;
    std::function<void(const std::string& title)> setTitle;
};

struct MenuItem {
    std::string label, shortcut;
    bool enabled = true, separator = false;
    std::function<void()> action;
};

class NativeUi {
public:
    explicit NativeUi(core::Session& s, Host host = {});

    void resize(int w, int h) { m_w = w; m_h = h; m_dirty = true; }
    void paint(Canvas& c);
    void mouseDown(int x, int y, int button, int mods);   // button 0 = left, 1 = right
    void mouseMove(int x, int y, int mods);
    void mouseUp(int x, int y, int button, int mods);
    void doubleClick(int x, int y);
    void mouseWheel(int x, int y, double dx, double dy, int mods);
    bool key(int key, int mods);
    /// Called ~30 times a second; returns true when the window needs a repaint.
    bool tick();

    void message(const std::string& text);
    void setDeviceStatus(const std::string& text) { m_device = text; m_dirty = true; }
    void invalidate() { m_dirty = true; }

    // ---- state (also used by the tests) ----
    int selectedTrack() const { return m_selTrack; }
    int selectedRegion() const { return m_selRegion; }
    double pixelsPerBeat() const { return m_ppb; }
    bool mixerVisible() const { return m_mixer; }
    void setMixerVisible(bool v) { m_mixer = v; m_dirty = true; }
    bool menuOpen() const { return !m_menu.empty(); }
    const std::string& lastMessage() const { return m_message; }

private:
    enum class Hit { None, Rewind, Stop, Play, Record, Cycle, Metronome, CountIn, MenuFile, MenuEdit, MenuFunctions, MenuView, AddTrack,
                     Snap, ZoomIn, ZoomOut, MixerToggle, Ruler, Lane, Region, RegionEdge, TrackHeader, TrackMute, TrackSolo, TrackArm,
                     TrackVol, Fader, Pan, MasterFader, MixerStrip, Insert };
    struct HitRect { Rect r; Hit kind; int a = 0, b = 0; };
    enum class Drag { None, Seek, Region, Resize, Volume, Fader, Pan, Master };

    // layout
    Rect controlBar() const { return {0, 0, m_w, kControlH}; }
    Rect toolbar() const { return {0, kControlH, m_w, kToolbarH}; }
    Rect arrangeArea() const;
    Rect mixerArea() const;
    double beatToX(double beat) const { return kHeaderW + beat * m_ppb - m_scrollX; }
    double xToBeat(int x) const { return (x - kHeaderW + m_scrollX) / m_ppb; }
    double snapBeat(double b) const;
    double snapStep() const;

    // painting
    void drawControlBar(Canvas& c);
    void drawToolbar(Canvas& c);
    void drawArrange(Canvas& c);
    void drawRegion(Canvas& c, const core::Track& t, int ti, int ri, const Rect& r, Rgba color);
    void drawMixer(Canvas& c);
    void drawStrip(Canvas& c, const Rect& r, const core::Track* t, int ti);
    void drawStatus(Canvas& c);
    void drawMenu(Canvas& c);
    void button(Canvas& c, const Rect& r, const std::string& label, bool on, Rgba onColor, Hit kind, int a = 0, int b = 0, Font f = Font::Bold11);
    void addHit(const Rect& r, Hit kind, int a = 0, int b = 0) { m_hits.push_back({r, kind, a, b}); }

    // actions
    void openMenu(const std::vector<MenuItem>& items, int x, int y);
    std::vector<MenuItem> fileMenu(), editMenu(), functionsMenu(), viewMenu(), addTrackMenu(), trackMenu(int t);
    void closeMenu() { m_menu.clear(); m_dirty = true; }
    void togglePlay();
    void doStop();
    void selectRegion(int t, int r) { m_selTrack = t; m_selRegion = r; m_dirty = true; }
    bool hasRegion() const;
    bool regionIsMidi() const;
    void withRegion(const std::function<void(int, int)>& f);
    void ensureVisible(double beat);
    void doFile(const std::string& what);
    void applyFader(int track, int y, const Rect& r);
    static float faderToGain(double pos);
    static double gainToFader(float gain);

    core::Session& m_s;
    Host m_host;
    int m_w = 1280, m_h = 720;
    double m_ppb = 40, m_scrollX = 0, m_scrollY = 0;
    bool m_mixer = true, m_dirty = true, m_snapOn = true;
    int m_snapIdx = 2;   // 1 bar, 1/2, 1/4, 1/8, 1/16
    int m_selTrack = -1, m_selRegion = -1;
    std::string m_message, m_device;
    std::vector<core::Track> m_tracks;   // snapshot used while painting and hit-testing
    std::vector<HitRect> m_hits;
    std::vector<MenuItem> m_menu;
    Rect m_menuRect;
    int m_menuHover = -1;
    Drag m_drag = Drag::None;
    int m_dragTrack = -1, m_dragRegion = -1, m_dragX = 0, m_dragY = 0;
    Rect m_dragRect;
    bool m_dragMoved = false;
    double m_dragStart = 0, m_dragLen = 0;
    float m_dragValue = 0;
    int m_hoverX = 0, m_hoverY = 0;
    float m_meterL = 0, m_meterR = 0;
    std::vector<std::pair<float, float>> m_trackPeaks;

    static constexpr int kControlH = 54, kToolbarH = 32, kRulerH = 26, kHeaderW = 236, kTrackH = 60, kStatusH = 22, kMixerH = 312, kStripW = 88;
};

}  // namespace ui
