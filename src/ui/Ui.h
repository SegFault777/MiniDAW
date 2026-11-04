// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Qt-free user interface (v0.9): control bar, arrangement, mixer, menus. It draws into a ui::Canvas and reacts to
// plain mouse / keyboard events, so the window layer is only a thin shell and the whole UI is testable headless.
#include <chrono>
#include <functional>
#include <string>
#include <vector>
#include "../core/Session.h"
#include "../dsp/Param.h"
#include <map>
#include <set>
#include <memory>
#include "Canvas.h"
#include "Dialog.h"

namespace ui {

enum Mod { ModCtrl = 1, ModShift = 2, ModAlt = 4 };
// Letters are passed as their upper-case ASCII code, digits as '0'..'9'.
enum Key { KeySpace = ' ', KeyDelete = 256, KeyBackspace, KeyLeft, KeyRight, KeyUp, KeyDown, KeyHome, KeyEnd, KeyEnter, KeyEscape, KeyPlus, KeyMinus, KeyF2 };

/// A plug-in found by the host (MiniDAW plug-in or VST3).
struct PluginEntry {
    std::string id, name, vendor;
    bool instrument = false;
};

/// What the user interface needs from the operating system.
struct Host {
    /// Native open / save dialog. Returns the chosen path or "" when cancelled. 'filter' is "Name|*.ext;*.ext".
    std::function<std::string(const std::string& title, const std::string& filter, bool save)> fileDialog;
    std::function<void()> quit;
    std::function<void(const std::string& title)> setTitle;
    std::string loopFolder = "loops";
    // preferences
    std::function<std::vector<std::pair<std::string, std::string>>(bool input)> audioDevices;   // (id, name); the id "" is the system default
    std::function<std::vector<std::string>()> midiDevices;
    std::function<std::vector<std::pair<std::string, std::string>>()> languages;                // (code, native name)
    std::function<void(const std::map<std::string, std::string>&)> applySettings;               // persist and apply
    std::map<std::string, std::string> settings;                                                // current values
    // plug-ins
    std::function<std::vector<PluginEntry>()> plugins;                                          // everything found so far
    std::function<void()> rescanPlugins;
    std::function<bool(const std::shared_ptr<dsp::ParamSet>&)> openPluginEditor;                // VST3 editor window
};

struct MenuItem {
    std::string label, shortcut;
    bool enabled = true, separator = false;
    std::function<void()> action;
};

class NativeUi {
public:
    explicit NativeUi(core::Session& s, Host host = {});

    void resize(int w, int h) { m_winW = w; m_h = h; relayout(); }
    void paint(Canvas& c);
    void mouseDown(int x, int y, int button, int mods);   // button 0 = left, 1 = right
    void mouseMove(int x, int y, int mods);
    void mouseUp(int x, int y, int button, int mods);
    void doubleClick(int x, int y);
    void mouseWheel(int x, int y, double dx, double dy, int mods);
    bool key(int key, int mods);
    bool keyUp(int key);
    /// Typed characters (UTF-8), for text fields in dialogs.
    void text(const std::string& utf8) {
        if (m_dialog) { m_dialog->text(utf8); m_dirty = true; }
        else if (m_searchFocus) { for (char ch : utf8) if ((unsigned char)ch >= 32) m_search.push_back(ch); m_browserScroll = 0; m_dirty = true; }
    }
    /// Called ~30 times a second; returns true when the window needs a repaint.
    bool tick();

    void message(const std::string& text);
    /// A translatable message with one placeholder: msg("Saved %1", path).
    static std::string msg(const std::string& pattern, const std::string& arg = {}) {
        std::string t = Canvas::translate(pattern);
        const size_t at = t.find("%1");
        if (at != std::string::npos) t.replace(at, 2, arg);
        return t;
    }
    /// Opens a project file (used for "open with" and file associations).
    void openFile(const std::string& path) { openProject(path); m_dirty = true; }
    void setDeviceStatus(const std::string& text) { m_device = text; m_dirty = true; }
    void invalidate() { m_dirty = true; }

    enum TabId { TabMixer = 0, TabPianoRoll = 1, TabSteps = 2, TabDrummer = 3, TabLoops = 4, TabAutomation = 5, TabInspector = 100, TabSmart = 101 };
    void showTab(int id) { m_mixer = true; if (id >= 100) m_tabExtra = id; else { m_tabExtra = 0; m_tab = Tab(id); } m_dirty = true; }
    Rect pianoGridRect() const { return m_prGrid; }
    Rect stepGridRect() const { return m_stepGrid; }
    Rect autoLaneRect() const { return m_autoLane; }
    void showAutomation(int param) { showTab(TabAutomation); m_autoParam = param; m_autoSel = -1; }
    int pianoSelectedNote() const { return m_prSel; }
    int pianoSelectionSize() const { return int(m_prSelected.size()); }
    void pianoSelectAll();
    bool typingOn() const { return m_typing; }
    int browserItemCount() { return int(browserItems().size()); }
    Rect browserRowRect(int index) const { return index >= 0 && index < int(m_browserRows.size()) ? m_browserRows[size_t(index)] : Rect{}; }
    Rect browserSearchRect() const { return {m_winW - kBrowserW + 12, kControlH + kToolbarH + 30, kBrowserW - 24, 24}; }
    int viewWidth() const { return m_w; }
    void setTrackHeight(int h) { m_trackH = h; m_dirty = true; }

    // ---- state (also used by the tests) ----
    int selectedTrack() const { return m_selTrack; }
    int selectedRegion() const { return m_selRegion; }
    double pixelsPerBeat() const { return m_ppb; }
    bool mixerVisible() const { return m_mixer; }
    void setMixerVisible(bool v) { m_mixer = v; m_dirty = true; }
    bool menuOpen() const { return !m_menu.empty(); }
    bool dialogOpen() const { return m_dialog && !m_dialog->finished(); }
    Dialog* dialog() { return m_dialog.get(); }
    const std::string& lastMessage() const { return m_message; }

private:
    enum class Hit { None, Rewind, Stop, Play, Record, Cycle, Metronome, CountIn, MenuFile, MenuTrack, MenuEdit, MenuFunctions, MenuView, AddTrack,
                     Snap, ZoomIn, ZoomOut, MixerToggle, Ruler, Lane, Region, RegionEdge, TrackHeader, TrackMute, TrackSolo, TrackArm,
                     TrackVol, Fader, Pan, MasterFader, MixerStrip, Insert, Tab, Slider, Action, PianoGrid, PianoKeys, PianoVel, StepCell, Marker, LoopItem, AutoLane, BrowserItem, BrowserSection, BrowserSearch, Knob };
    struct HitRect { Rect r; Hit kind; int a = 0, b = 0; };
    enum class Drag { None, Seek, Region, Resize, Volume, Fader, Pan, Master, Slider, Note, NoteResize, Velocity, Steps, MarkerMove, LoopRange, AutoPoint, Browser, Knob, Marquee };

    // layout
    Rect controlBar() const { return {0, 0, m_winW, kControlH}; }
    Rect toolbar() const { return {0, kControlH, m_winW, kToolbarH}; }
    Rect arrangeArea() const;
    Rect bottomArea() const;                 // tab bar + content
    Rect paneContent() const;                // below the tab bar
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
    void drawBottom(Canvas& c);
    void drawInspector(Canvas& c, const Rect& r);
    void drawSmartControls(Canvas& c, const Rect& r);   // the "Devices" tab
    void uiKnob(Canvas& c, double cx, double cy, double radius, const std::string& label, double t01, const std::string& valueText, std::function<void(double)> set, std::function<void()> reset, Rgba color);
    void addEffectMenu(int track, int x, int y);
    void drawPianoRoll(Canvas& c, const Rect& r);
    void drawSteps(Canvas& c, const Rect& r);
    void drawDrummer(Canvas& c, const Rect& r);
    struct BrowserItem { int kind = 0; std::string id, name, sub, path; double bpm = 0; };   // kind: 0 loop, 1 instrument, 2 effect
    std::vector<BrowserItem> browserItems();
    void drawBrowser(Canvas& c);
    void dropPayload(int x, int y);
    void drawAutomation(Canvas& c, const Rect& r);
    bool autoMouseDown(int x, int y, int button);
    void autoMouseMove(int x, int y);
    // generic controls: the paint pass registers them, so layout and hit areas stay in sync
    void uiSlider(Canvas& c, const Rect& r, const std::string& label, double t01, const std::string& valueText, std::function<void(double)> set, Rgba color);
    void uiAction(Canvas& c, const Rect& r, const std::string& label, std::function<void()> fn, bool on = false, Rgba onColor = 0xff26dec4u);
    bool paneMouseDown(int x, int y, int button, int mods);
    void paneMouseMove(int x, int y);
    void paneMouseUp();
    bool paneWheel(int x, int y, double dx, double dy, int mods);
    bool paneKey(int key, int mods);
    void reloadLoops();
    void auditionNote(int track, int pitch);
    void pianoSelectionChanged();
    bool pianoRegion(int& t, int& r) const;
    std::string trackTitle(int t) const;
    void drawStrip(Canvas& c, const Rect& r, const core::Track* t, int ti);
    void drawStatus(Canvas& c);
    std::string hintFor(Hit k) const;
    void drawMenu(Canvas& c);
    void button(Canvas& c, const Rect& r, const std::string& label, bool on, Rgba onColor, Hit kind, int a = 0, int b = 0, Font f = Font::Bold11);
    void addHit(const Rect& r, Hit kind, int a = 0, int b = 0) { m_hits.push_back({r, kind, a, b}); }

    // actions
    void openMenu(const std::vector<MenuItem>& items, int x, int y);
    std::vector<MenuItem> fileMenu(), recentMenu(), editMenu(), functionsMenu(), viewMenu(), addTrackMenu(), trackMenu(int t);
    void closeMenu() { m_menu.clear(); m_dirty = true; }
    void togglePlay();
    void doStop();
    void selectRegion(int t, int r) { m_selTrack = t; m_selRegion = r; m_dirty = true; }
    bool hasRegion() const;
    bool regionIsMidi() const;
    void withRegion(const std::function<void(int, int)>& f);
    void ensureVisible(double beat);
    void doFile(const std::string& what);
    void tapTempo();
    void gotoMarker(int dir);
    void dlgPreferences();
    // dialogs (UiDialogs.cpp)
    void openDialog(std::unique_ptr<Dialog> d);
    double last(const std::string& key, double def) const { auto it = m_last.find(key); return it == m_last.end() ? def : it->second; }
    void dlgTranspose(); void dlgVelocityRamp(); void dlgRepeat(); void dlgQuantize(); void dlgScale(); void dlgHumanize(); void dlgArpeggiate();
    void dlgChord(); void dlgFixedLength(); void dlgScaleVelocity(); void dlgStripSilence(); void dlgDetectTempo(); void dlgPatternToMidi();
    void dlgTimeSignature(); void dlgTempo(); void dlgBounce(); void dlgAbout(); void dlgRename();
    void applyFader(int track, int y, const Rect& r);
    static float faderToGain(double pos);
    static double gainToFader(float gain);

    core::Session& m_s;
    Host m_host;
    int m_w = 1280, m_h = 720;   // m_w: width of the arrangement and the bottom panel (window width minus the browser)
    int m_winW = 1280;           // width of the whole window
    bool m_browser = true;
    int m_trackH = 60;   // 34 compact, 60 normal, 96 tall
    int browserW() const { return m_browser ? kBrowserW : 0; }
    void relayout() { m_w = std::max(400, m_winW - browserW()); m_dirty = true; }
    double m_ppb = 40, m_scrollX = 0, m_scrollY = 0;
    enum class Tab { Mixer, PianoRoll, Steps, Drummer, Loops, Automation };
    Tab m_tab = Tab::Mixer;
    bool m_mixer = true, m_dirty = true, m_snapOn = true;
    std::vector<double> m_taps;
    int m_snapIdx = 2;   // 1 bar, 1/2, 1/4, 1/8, 1/16
    int m_selTrack = -1, m_selRegion = -1;
    std::string m_message, m_device;
    std::vector<core::Track> m_tracks;   // snapshot used while painting and hit-testing
    std::vector<HitRect> m_hits;
    std::vector<std::function<void(double)>> m_sliderFns;
    std::vector<std::function<void()>> m_actionFns;
    std::function<void(double)> m_dragFn;
    struct KnobDef { double t01; std::function<void(double)> set; std::function<void()> reset; };
    std::vector<KnobDef> m_knobs;
    double m_devScroll = 0;
    std::map<std::string, int> m_devPage;
    // piano roll
    double m_prPpb = 80, m_prScroll = 0;
    int m_prTop = 84, m_prRowH = 13, m_prKey = -1, m_prSel = -1, m_prDragPitch = 0, m_prVelNote = -1;
    double m_prDragOff = 0, m_prMinLen = 0.25;
    bool m_prChecked = false;
    std::set<int> m_prSelected;                    // all selected notes (m_prSel is the one last clicked)
    std::vector<core::Note> m_prOrig;              // the notes as they were when a drag began
    double m_prAnchorBeat = 0;
    Rect m_prMarquee;
    bool m_prMarqueeOn = false;
    std::vector<core::Note> m_noteClip;
    Rect m_prGrid, m_prVel, m_prKeys;
    int m_auditionTrack = -1, m_auditionPitch = -1, m_auditionTicks = 0;
    // loop browser
    struct LoopEntry { std::string path, name; double bpm = 0; };
    std::vector<LoopEntry> m_loops;
    int m_loopSel = -1;
    double m_loopScroll = 0;
    bool m_loopsLoaded = false;
    int m_insertSel = 0;
    // browser
    std::string m_search;
    bool m_searchFocus = false;
    int m_browserSel = -1, m_browserContentH = 0, m_browserViewH = 0;
    double m_browserScroll = 0;
    std::map<int, bool> m_sectionClosed;
    std::vector<Rect> m_browserRows;
    std::vector<std::pair<std::string, std::string>> m_effectNames;
    bool m_payActive = false, m_payPending = false;
    int m_payKind = -1, m_payX = 0, m_payY = 0;
    std::string m_payId, m_payName, m_payPath;
    int m_autoParam = 0, m_autoSel = -1;   // 0..3 = AutoParam, 4 = tempo
    Rect m_autoLane;
    bool m_typing = false;
    int m_typingOct = 4;
    std::map<int, int> m_heldKeys;   // key -> pitch that is sounding
    void addRecent(const std::string& path);
    void openProject(const std::string& path);
    int m_tabExtra = 0;   // 100 = Inspector, 101 = Smart Controls, 0 = use m_tab
    Rect m_stepGrid;
    bool m_stepPaint = false;
    std::map<int, int> m_presetIdx;
    void setPresetDelta(int ti, int d, int n);
    std::vector<MenuItem> m_menu;
    std::unique_ptr<Dialog> m_dialog;
    std::map<std::string, double> m_last;   // values of the last used dialogs
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

    static constexpr int kControlH = 54, kToolbarH = 32, kRulerH = 26, kHeaderW = 236, kStatusH = 22, kBrowserW = 250, kMixerH = 344, kStripW = 88;
};

}  // namespace ui
