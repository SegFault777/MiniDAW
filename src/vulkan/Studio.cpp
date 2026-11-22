// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Vulkan UI: the frame loop, window layout, menu bar, transport, status bar, keyboard shortcuts, command registry and palette.
#include "IconFont.h"
#include "Studio.h"
#include <algorithm>
#include <cstdio>
#include "../core/MidiOps.h"
#include "../dsp/Effects.h"
#include "Icons.h"
#include "Theme.h"

namespace vkui {
using namespace theme;

namespace {
constexpr float kMenuH = 28, kTransportH = 60, kStatusH = 24, kSplit = 5;
std::string lower(std::string s) { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); }); return s; }
}  // namespace

Studio::Studio(core::Session& session, Host host) : m_s(session), m_host(std::move(host)) {
    registerCommands();
    {   // icon style: the fonts of Material Symbols are optional, a missing font falls back to the classic icons
        auto& th = IconTheme::instance();
        if (!m_host.iconFontDirs.empty()) th.setFontDirs(m_host.iconFontDirs);
        const auto it = m_host.settings.find("iconStyle");
        th.setStyle(IconTheme::parseStyle(it == m_host.settings.end() ? "outlined" : it->second));
    }
    for (const std::string& fid : dsp::insertEffectIds()) { auto fx = dsp::createEffect(fid); m_effectNames.emplace_back(fid, fx ? fx->displayName() : fid); }
    m_tempo = m_s.tempoMap();
    applyLayout(m_host.settings);
    m_lastLayout = layoutSettings();
    if (m_host.settings.count("scaleRoot")) m_scaleRoot = std::atoi(m_host.settings["scaleRoot"].c_str()) % 12;
    if (m_host.settings.count("scaleType")) m_scaleType = std::clamp(std::atoi(m_host.settings["scaleType"].c_str()), 0, int(core::ScaleParams::Count) - 1);
}

std::map<std::string, std::string> Studio::layoutSettings() const {
    auto num = [](double v) { char b[32]; std::snprintf(b, sizeof b, "%.1f", v); return std::string(b); };
    return {{"showBrowser", m_showBrowser ? "1" : "0"}, {"showInspector", m_showInspector ? "1" : "0"}, {"showDock", m_showDock ? "1" : "0"},
            {"dockTab", std::to_string(int(m_dock))}, {"dockH", num(m_dockH)}, {"browserW", num(m_browserW)}, {"inspectorW", num(m_inspectorW)},
            {"trackH", num(m_trackH)}, {"zoom", num(m_ppb)}, {"snap", m_snapOn ? "1" : "0"}, {"follow", m_follow ? "1" : "0"}};
}

void Studio::applyLayout(const std::map<std::string, std::string>& s) {
    auto has = [&](const char* k) { return s.count(k) != 0; };
    auto flag = [&](const char* k, bool& v) { if (has(k)) v = s.at(k) != "0"; };
    auto val = [&](const char* k, float& v, float lo, float hi) { if (has(k)) v = std::clamp(float(std::atof(s.at(k).c_str())), lo, hi); };
    flag("showBrowser", m_showBrowser); flag("showInspector", m_showInspector); flag("showDock", m_showDock);
    flag("snap", m_snapOn); flag("follow", m_follow);
    val("dockH", m_dockH, 120.f, 900.f); val("browserW", m_browserW, 160.f, 500.f); val("inspectorW", m_inspectorW, 180.f, 500.f);
    val("trackH", m_trackH, 28.f, 200.f);
    if (has("zoom")) m_ppb = std::clamp(std::atof(s.at("zoom").c_str()), 2.0, 400.0);
    if (has("dockTab")) m_dock = DockTab(std::clamp(std::atoi(s.at("dockTab").c_str()), 0, int(DockTab::Drummer)));
    if (has("scaleRoot")) m_scaleRoot = std::atoi(s.at("scaleRoot").c_str()) % 12;
    if (has("scaleType")) m_scaleType = std::clamp(std::atoi(s.at("scaleType").c_str()), 0, int(core::ScaleParams::Count) - 1);
}

const std::string& Studio::tr(const std::string& s) const {
    if (m_table) if (const std::string* f = m_table->find(s)) return *f;
    return s;
}

void Studio::message(const std::string& text) { m_toast = text; m_toastAt = m_time; }

double Studio::prGridBeats() const {
    if (!m_prSnap) return 0.0;
    if (m_prGridIdx > 0) return kGridBeats[m_prGridIdx];
    for (double g : {0.0625, 0.125, 0.25, 0.5, 1.0, 2.0, 4.0}) if (g * m_prPpb >= 12.0) return g;
    return 4.0;
}

double Studio::gridBeats() const {
    if (m_snapIdx > 0) return kGridBeats[m_snapIdx];
    // Auto (Ableton Live): the coarsest grid whose lines are still at least 12 dp apart at this zoom
    for (double g : {0.0625, 0.125, 0.25, 0.5, 1.0, 2.0, 4.0})
        if (g * m_ppb >= 12.0) return g;
    return 4.0;
}
double Studio::snapGrid() const { return m_snapOn ? gridBeats() : 0.0; }
std::string Studio::gridName() const {
    if (!m_snapOn) return "Off";
    if (m_snapIdx > 0) return kGridNames[m_snapIdx];
    const double g = gridBeats();
    char buf[32];
    if (g >= 4) return "Auto: Bar";
    std::snprintf(buf, sizeof buf, "1/%d", int(4.0 / g));
    return fmt("Auto: %1", buf);
}
double Studio::snapBeat(double beat, bool force) const {
    const double g = (m_snapOn || force) ? gridBeats() : 0.0;
    if (g <= 0 || m_g->alt()) return std::max(0.0, beat);   // hold Alt to move freely
    return std::max(0.0, std::round(beat / g) * g);
}

void Studio::refreshModel() {
    // The whole model is copied only when it changed (or while recording). While the song merely plays, the only things that move
    // are the meters, so just the peaks are fetched: a big project is no longer copied 60 times a second.
    const bool recording = m_s.isRecording();
    const bool playing = m_s.isPlaying();
    const bool wasRunning = m_wasRunning;
    m_wasRunning = playing || recording;
    const std::uint64_t rev = m_s.revision();
    if (m_modelDirty.exchange(false) || recording || m_tracks.empty() || rev != m_lastRev) {
        m_lastRev = rev;
        m_tracks = m_s.snapshot();
        ++m_fullSnapshots;
        for (size_t i = m_multi.size(); i-- > 0;) {   // drop selected regions that no longer exist
            const auto p = m_multi[i];
            if (p.first < 0 || p.first >= int(m_tracks.size()) || p.second < 0 || p.second >= int(m_tracks[size_t(p.first)].regions.size())) m_multi.erase(m_multi.begin() + std::ptrdiff_t(i));
        }
        m_markers = m_s.markers();
        m_tempo = m_s.tempoMap();
    } else if (playing || wasRunning || m_peaksLive) {   // after stopping, keep reading until the audio thread has cleared the meters
        m_s.trackPeaks(m_peakScratch);
        m_peaksLive = false;
        if (m_peakScratch.size() == m_tracks.size()) {
            for (size_t i = 0; i < m_tracks.size(); ++i) {
                m_tracks[i].peakL = m_peakScratch[i].first; m_tracks[i].peakR = m_peakScratch[i].second;
                if (m_peakScratch[i].first > 0 || m_peakScratch[i].second > 0) m_peaksLive = true;
            }
        } else m_modelDirty = true;   // the track list changed behind our back: take a full copy next frame
    }
    float l = 0, r = 0;
    m_s.masterPeaks(l, r);
    const float fall = std::min(1.f, m_g ? m_g->dt() * 4.f : 0.1f);
    m_meterL = std::max(l, m_meterL - fall * 0.5f * std::max(m_meterL, 0.1f));
    m_meterR = std::max(r, m_meterR - fall * 0.5f * std::max(m_meterR, 0.1f));
    for (const core::Track& t : m_tracks) { float& h = m_peakHold[t.id]; h = std::max(h, std::max(t.peakL, t.peakR)); }
    m_masterHold = std::max(m_masterHold, std::max(l, r));
    if (m_g) m_frameMs += (double(m_g->dt()) * 1000.0 - m_frameMs) * 0.1;
    const int n = int(m_tracks.size());
    if (n == 0) { m_selTrack = -1; m_selRegion = -1; }
    else {
        m_selTrack = std::clamp(m_selTrack, 0, n - 1);
        if (m_selRegion >= int(m_tracks[size_t(m_selTrack)].regions.size())) m_selRegion = -1;
    }
}

bool Studio::wantsFrame() const {
    return m_s.isPlaying() || m_s.isRecording() || m_meterL > 0.004f || m_meterR > 0.004f || m_time - m_toastAt < 3.4 || m_autoScrolling || m_panning || m_payActive || m_auditionPitch >= 0 || m_modelDirty.load();
}

void Studio::layout(float w, float h) {
    m_rMenu = {0, 0, w, kMenuH};
    m_rTransport = {0, kMenuH, w, kTransportH};
    m_rStatus = {0, h - kStatusH, w, kStatusH};
    Rect body{0, kMenuH + kTransportH, w, h - kMenuH - kTransportH - kStatusH};
    m_rBody = body;
    m_browserW = std::clamp(m_browserW, 180.f, std::max(180.f, w * 0.35f));
    m_inspectorW = std::clamp(m_inspectorW, 200.f, std::max(200.f, w * 0.35f));
    m_rBrowser = m_showBrowser ? body.takeLeft(m_browserW) : Rect{};
    m_rInspector = m_showInspector ? body.takeRight(m_inspectorW) : Rect{};
    m_dockH = std::clamp(m_dockH, 140.f, std::max(140.f, body.h - 160));
    m_rDock = m_showDock ? body.takeBottom(m_dockMax ? std::max(140.f, body.h - 150.f) : m_dockH) : Rect{};
    m_rArr = body;
}

std::string Studio::positionText() const {
    const double pos = m_s.positionBeats();
    const auto tm = m_tempo ? m_tempo : m_s.tempoMap();
    const int bar = tm->barAt(pos);
    const double inBar = pos - tm->barStart(bar);
    const double unit = 4.0 / std::max(1, tm->meterAtBeat(pos).den);
    const int beat = int(inBar / unit + 1e-9) + 1;
    const int ticks = int(std::clamp((inBar - (beat - 1) * unit) / unit, 0.0, 0.9999) * 960.0);
    char buf[48];
    std::snprintf(buf, sizeof buf, "%3d . %d . %03d", bar, beat, ticks);
    return buf;
}

std::string Studio::timeText() const {
    const double sec = std::max(0.0, m_s.beatToFrame(m_s.positionBeats()) / double(::kSampleRate));
    char buf[48];
    std::snprintf(buf, sizeof buf, "%02d:%02d.%03d", int(sec / 60), int(std::fmod(sec, 60.0)), int(std::fmod(sec, 1.0) * 1000));
    return buf;
}

bool Studio::splitter(const std::string& name, const Rect& r, float& value, bool vertical, float lo, float hi, float sign) {
    const Id sid = id("split:" + name);
    const Interact it = m_g->interact(sid, r);
    const float h = m_g->anim(sid, (it.hovered || it.held) ? 1.f : 0.f);
    if (h > 0.01f) m_dl->rect(r, withAlpha(accent, int(110 * h)));
    if (it.hovered || it.held) m_g->setCursor(vertical ? Cursor::ResizeH : Cursor::ResizeV);
    if (it.held) {
        value = std::clamp(value + sign * (vertical ? it.dx : it.dy), lo, hi);
        return true;
    }
    return false;
}

// ------------------------------------------------------------------------------------------------------------ frame

void Studio::frame(Gui& g, double time) {
    m_g = &g;
    m_dl = &g.dl();
    m_time = time;
    if (!g.translate) g.translate = [this](const std::string& s) -> const std::string& { return tr(s); };
    m_s.setGroupLinking(!g.alt());   // Alt+drag changes only the track you touch, even when it belongs to a mixer group
    refreshModel();
    layout(m_dl->width(), m_dl->height());
    m_hint.clear();
    m_dl->rect({0, 0, m_dl->width(), m_dl->height()}, bg0);

    const bool modal = m_dlg.open || m_paletteOpen || m_helpOpen || m_tutOpen;
    const bool menuUp = g.menuOpen();
    g.setInputEnabled(!modal && !menuUp);
    if (!modal) handleShortcuts();
    m_prFocus = false;   // the piano roll sets it again while the mouse is over it

    drawArrangement();
    if (m_showBrowser) {
        drawBrowser(m_rBrowser);
        splitter("browser", {m_rBrowser.right() - 2, m_rBrowser.y, kSplit, m_rBrowser.h}, m_browserW, true, 180, 520, 1);
    }
    if (m_showInspector) {
        drawInspector(m_rInspector);
        splitter("inspector", {m_rInspector.x - 2, m_rInspector.y, kSplit, m_rInspector.h}, m_inspectorW, true, 200, 520, -1);
    }
    if (m_showDock) {
        drawDock();
        splitter("dock", {m_rDock.x, m_rDock.y - 2, m_rDock.w, kSplit}, m_dockH, false, 140, 900, -1);
    }
    drawTransport();
    drawMenuBar();
    drawStatusBar();
    drawOverlays();
    // The drag of a browser item over the arrangement
    if (m_payActive) {
        const std::string label = m_payName;
        const float w = m_dl->textWidth(label, normal) + 22;
        m_dl->shadow({g.mouseX() + 10, g.mouseY() + 6, w, 22}, 6, 6, withAlpha(rgb(0, 0, 0), 120));
        m_dl->rrect({g.mouseX() + 10, g.mouseY() + 6, w, 22}, 6, accent);
        m_dl->textIn({g.mouseX() + 10, g.mouseY() + 6, w, 22}, label, rgb(255, 255, 255), normal, Align::Center);
    }
    if (m_afterFrame) { auto f = std::move(m_afterFrame); m_afterFrame = nullptr; f(); }
    if (m_reopenPrefs) { m_reopenPrefs = false; dlgPreferences(); }
    if (m_auditionPitch >= 0 && m_time > m_auditionUntil) { m_s.noteOff(m_auditionTrack, m_auditionPitch); m_auditionPitch = -1; }
}

// ----------------------------------------------------------------------------------------------------------- menu bar

std::vector<MenuItem> Studio::menuFor(const std::string& category) {
    std::vector<MenuItem> items;
    for (const Command& c : m_commands) {
        if (c.category != category || c.label[0] == '~') continue;
        if (c.label == "-") { if (!items.empty() && !items.back().separator) items.push_back(MenuItem::sep()); continue; }
        MenuItem m;
        m.label = c.label;
        m.shortcut = c.shortcut;
        m.enabled = !c.enabled || c.enabled();
        m.checked = c.checked && c.checked();
        m.action = [this, run = c.run] { run(); m_modelDirty = true; };
        items.push_back(std::move(m));
    }
    while (!items.empty() && items.back().separator) items.pop_back();
    return items;
}

void Studio::drawMenuBar() {
    Gui& g = *m_g;
    g.setInputEnabled(!m_dlg.open && !m_paletteOpen && !m_helpOpen && !m_tutOpen);   // menus stay usable while another menu is open
    m_dl->rect(m_rMenu, bg1);
    m_dl->hline(0, m_rMenu.w, m_rMenu.bottom() - 1, line);
    // brand
    const Rect brand{m_rMenu.x + 10, m_rMenu.y + 5, 74, 18};
    m_dl->rrect(brand, 5, accentDim);
    m_dl->textIn(brand, "MiniDAW", rgb(0xd8, 0xe4, 0xff), bold, Align::Center);
    std::vector<TopMenu> menus;
    for (const char* cat : {"File", "Edit", "Track", "Region", "MIDI", "Audio", "Mix", "View", "Transport", "Help"}) {
        const std::string c = cat;
        menus.push_back({c, [this, c] { return menuFor(c); }});
    }
    g.menuBar({m_rMenu.x + 86, m_rMenu.y, m_rMenu.w - 86 - 220, m_rMenu.h}, menus);
    // the command palette: the fastest way to anything (Ctrl+K)
    const Rect pal{m_rMenu.right() - 214, m_rMenu.y + 4, 204, 20};
    const Id pid = id("palette-button");
    const Interact it = g.interact(pid, pal, false);
    const float h = g.anim(pid, it.hovered ? 1.f : 0.f);
    m_dl->rrect(pal, 10, mix(bg0, bg2, h));
    m_dl->rrectStroke(pal, 10, 1, mix(border, accent, h));
    icons::search(*m_dl, {pal.x + 2, pal.y, 22, pal.h}, textDim);
    m_dl->textIn({pal.x + 26, pal.y, pal.w - 30, pal.h}, tr("Search commands") + "   Ctrl+K", textMute, small);
    if (it.hovered) g.setCursor(Cursor::Hand);
    if (it.clicked) { m_paletteOpen = true; m_paletteQuery.clear(); m_paletteSel = 0; }
    g.setInputEnabled(!m_dlg.open && !m_paletteOpen && !m_helpOpen && !m_tutOpen && !g.menuOpen());
}

// ------------------------------------------------------------------------------------------------------------ transport

void Studio::drawTransport() {
    Gui& g = *m_g;
    DrawList& dl = *m_dl;
    const Rect r = m_rTransport;
    dl.rectGradient(r, bg2, bg1);
    dl.hline(r.x, r.right(), r.bottom() - 1, line);
    float x = r.x + 12;
    const float cy = r.cy(), bs = 38;
    auto slot = [&](float w) { Rect q{x, cy - 18, w, 36}; x += w + 5; return q; };
    auto tipFor = [&](const char* t) { setHint(t); };
    (void)tipFor;

    if (g.iconButton(id("t-rew"), slot(bs), icons::rewind, "Return to start (Enter)")) { m_s.seekBeats(0); ensureVisible(0); }
    if (g.iconButton(id("t-stop"), slot(bs), icons::stop, "Stop (Enter stops and returns)")) { m_s.stop(); }
    const bool playing = m_s.isPlaying();
    if (g.iconButton(id("t-play"), slot(bs + 6), icons::play, "Play / Pause (Space)", playing, play)) { if (playing) m_s.pause(); else m_s.play(); }
    const bool rec = m_s.isRecording();
    if (g.iconButton(id("t-rec"), slot(bs), icons::record, "Record (R) - needs an armed track", rec, record)) { if (rec) m_s.stopRecording(); else m_s.startRecording(); }
    const bool loopOn = m_s.loopOn();
    if (g.iconButton(id("t-loop"), slot(bs), icons::loop, "Cycle (C) - drag in the strip above the ruler to set the range", loopOn, loop)) m_s.setLoop(!loopOn);
    x += 8;

    // LCD: position, time, tempo, signature
    {
        const Rect lcd{x, cy - 22, 372, 44};
        dl.rrect(lcd, 8, lcdBg);
        dl.rrectStroke(lcd, 8, 1, rgb(0x22, 0x2c, 0x3a));
        dl.text(lcd.x + 12, lcd.y + 6, positionText(), lcdText, {22, true});
        dl.text(lcd.x + 14, lcd.y + 29, "BAR   BEAT   TICK", textMute, {9, false});
        dl.vline(lcd.x + 168, lcd.y + 7, lcd.bottom() - 7, rgb(0x22, 0x2c, 0x3a));
        dl.text(lcd.x + 180, lcd.y + 6, timeText(), lcdText, {16, true});
        dl.text(lcd.x + 182, lcd.y + 29, "MIN : SEC . MS", textMute, {9, false});
        dl.vline(lcd.x + 270, lcd.y + 7, lcd.bottom() - 7, rgb(0x22, 0x2c, 0x3a));
        double bpm = m_s.bpm();
        if (g.numberField(id("bpm"), {lcd.x + 278, lcd.y + 6, 82, 20}, bpm, 20, 400, 1, 1, "", 120)) { m_s.setBpm(bpm); m_modelDirty = true; }
        const auto mp = (m_tempo ? m_tempo : m_s.tempoMap())->meterAtBeat(m_s.positionBeats());
        const std::string sig = std::to_string(mp.num) + " / " + std::to_string(mp.den);
        const Rect sr{lcd.x + 278, lcd.y + 27, 82, 14};
        const Id sid = id("sig");
        const Interact sit = g.interact(sid, sr, false);
        dl.textIn(sr, sig + "  " + tr("BPM"), sit.hovered ? text : textMute, small, Align::Center);
        if (sit.hovered) g.setCursor(Cursor::Hand);
        if (sit.clicked) dlgTimeSignature();
        x = lcd.right() + 10;
    }
    bool metro = m_s.metronome(), count = m_s.countIn();
    if (g.iconButton(id("t-metro"), slot(34), icons::metronome, "Metronome (K)", metro, accent)) m_s.setMetronome(!metro);
    {
        const Rect q = slot(34);
        if (g.toggle(id("t-count"), q, "1", count, accent)) m_s.setCountIn(count);
        g.tip(id("t-count"), "Count-in: one bar before recording");
    }
    if (g.button(id("t-tap"), slot(40), "Tap")) tapTempo();
    g.tip(id("t-tap"), "Tap tempo (T)");
    x += 10;
    g.separator({x - 6, cy - 16, 1, 32}, true);

    // tools (Cubase / Pro Tools tool box): keys 1 - 5
    struct T { Tool t; Gui::IconFn icon; const char* tip; };
    const T tools[] = {{Tool::Pointer, icons::pointer, "Pointer (1): select, move, resize"}, {Tool::Pencil, icons::pencil, "Pencil (2): draw MIDI regions, add notes"},
                       {Tool::Scissors, icons::scissors, "Scissors (3): click a region to split it"}, {Tool::Eraser, icons::eraser, "Eraser (4): click to delete"},
                       {Tool::Mute, icons::mute, "Mute (5): click a region to mute it"}};
    for (const T& t : tools)
        if (g.iconButton(id(std::string("tool") + std::to_string(int(t.t))), slot(32), t.icon, t.tip, m_tool == t.t, accent)) m_tool = t.t;
    x += 8;
    // snap / grid (Pro Tools grid + Ableton auto grid)
    {
        Rect q = slot(34);
        bool on = m_snapOn;
        if (g.toggle(id("snap"), q, "Snap", on, accent)) m_snapOn = on;
        g.tip(id("snap"), "Snap to grid (G)");
        std::vector<std::string> names(std::begin(kGridNames), std::end(kGridNames));
        int idx = m_snapIdx;
        if (g.dropdown(id("grid"), slot(92), idx, names)) m_snapIdx = idx;
        g.tip(id("grid"), "Grid size (Shift+G cycles). Auto follows the zoom level.");
    }
    x += 6;
    if (r.right() - x > 330) {
        if (g.iconButton(id("z-out"), slot(28), icons::minus, "Zoom out (-)")) zoomAt(m_rLanes.cx(), 1 / 1.3);
        if (g.iconButton(id("z-fit"), slot(28), icons::fit, "Zoom to fit the project (Z)")) zoomToFit();
        if (g.iconButton(id("z-in"), slot(28), icons::plus, "Zoom in (+)")) zoomAt(m_rLanes.cx(), 1.3);
        if (g.iconButton(id("follow"), slot(28), icons::follow, "Follow the playhead (F)", m_follow, accent)) m_follow = !m_follow;
    }
    // right side: master meter and panel toggles
    float rx = r.right() - 12;
    auto rslot = [&](float w) { rx -= w; Rect q{rx, cy - 14, w, 28}; rx -= 5; return q; };
    if (g.iconButton(id("p-dock"), rslot(30), icons::dock, "Show / hide the bottom panel (X)", m_showDock, accent)) m_showDock = !m_showDock;
    if (g.iconButton(id("p-insp"), rslot(30), icons::inspector, "Show / hide the inspector (I)", m_showInspector, accent)) { m_showInspector = !m_showInspector; m_host.settings["showInspector"] = m_showInspector ? "1" : "0"; }
    if (g.iconButton(id("p-brow"), rslot(30), icons::browser, "Show / hide the browser (B)", m_showBrowser, accent)) { m_showBrowser = !m_showBrowser; m_host.settings["showBrowser"] = m_showBrowser ? "1" : "0"; }
    rx -= 6;
    {
        const Rect m{rx - 84, cy - 12, 84, 24};
        dl.rrect(m, 4, bg0);
        auto bar = [&](float y, float v) {
            const float db = 20.f * std::log10(std::max(v, 1e-4f)), t = std::clamp((db + 60.f) / 60.f, 0.f, 1.f);
            const Rect b{m.x + 4, y, m.w - 8, 7};
            dl.rrect(b, 2, rgb(0x0a, 0x0c, 0x10));
            if (t > 0.01f) {
                dl.pushClip({b.x, b.y, b.w * t, b.h});
                dl.rectGradient(b, meterMid, meterMid);
                dl.rect({b.x, b.y, b.w * 0.72f, b.h}, meterLow);
                dl.rect({b.x + b.w * 0.93f, b.y, b.w * 0.07f, b.h}, meterHigh);
                dl.popClip();
            }
        };
        bar(m.y + 4, m_meterL);
        bar(m.y + 13, m_meterR);
        if (m_meterL > 0.99f || m_meterR > 0.99f) dl.circle(m.right() + 6, cy, 3, record);
    }
    g.setInputEnabled(g.inputEnabled());
}

// ----------------------------------------------------------------------------------------------------------- status bar

void Studio::drawStatusBar() {
    DrawList& dl = *m_dl;
    const Rect r = m_rStatus;
    dl.rect(r, bg1);
    dl.hline(r.x, r.right(), r.y, line);
    std::string left;
    if (m_time - m_toastAt < 3.4 && !m_toast.empty()) left = m_toast;
    else if (!m_hint.empty()) left = m_hint;
    else left = "Space: play  -  R: record  -  1-5: tools  -  Tab: next transient  -  Ctrl+K: search every command  -  F1: shortcuts";
    const bool toast = m_time - m_toastAt < 3.4 && !m_toast.empty();
    dl.textIn({r.x + 12, r.y, r.w * 0.6f, r.h}, tr(left), toast ? text : textMute, small);
    const char* toolNames[] = {"Pointer", "Pencil", "Scissors", "Eraser", "Mute"};
    char buf[160];
    std::snprintf(buf, sizeof buf, "%s  |  %s  |  %s  |  %.0f px/beat  |  %d tracks  |  %.0f fps%s", tr(toolNames[int(m_tool)]).c_str(), tr("Snap").c_str(), tr(gridName()).c_str(), m_ppb, int(m_tracks.size()), m_frameMs > 0.5 ? 1000.0 / m_frameMs : 0.0, m_host.portable ? "  |  Portable" : "");
    dl.textIn({r.x + r.w * 0.55f, r.y, r.w * 0.45f - 12, r.h}, buf, textDim, small, Align::Right);
}

// -------------------------------------------------------------------------------------------------------------- overlays

void Studio::drawOverlays() {
    Gui& g = *m_g;
    const bool menuUp = g.menuOpen();
    g.setInputEnabled(!menuUp);
    if (m_helpOpen) drawHelpSheet();
    if (m_tutOpen) drawTutorial();
    if (m_host.storeSettings && !g.mouseDown(0) && m_time - m_layoutCheck > 0.5) {   // remember the layout between sessions
        m_layoutCheck = m_time;
        const auto now = layoutSettings();
        if (now != m_lastLayout) { m_lastLayout = now; m_host.storeSettings(now); }
    }
    if (m_paletteOpen) drawPalette();
    if (m_dlg.open) drawDialog();
    drawToast();
}

void Studio::drawToast() {
    // a floating note above the status bar: it fades in, stays about three seconds and fades out
    const double age = m_time - m_toastAt;
    if (m_toast.empty() || age < 0 || age >= 3.4) return;
    DrawList& dl = *m_dl;
    const float a = float(std::clamp(std::min(age / 0.15, (3.4 - age) / 0.4), 0.0, 1.0));
    const std::string str = tr(m_toast);
    const float w = std::min(dl.width() - 80.f, dl.textWidth(str, normal) + 44.f), h = 34.f;
    const Rect r{(dl.width() - w) / 2, m_rStatus.y - h - 20.f + (1.f - a) * 8.f, w, h};
    dl.rrect({r.x, r.y + 3, r.w, r.h}, 17, withAlpha(rgb(0, 0, 0), uint8_t(70 * a)));
    dl.rrect(r, 17, withAlpha(bg2, uint8_t(240 * a)));
    dl.rrectStroke(r, 17, 1, withAlpha(border, uint8_t(255 * a)));
    dl.pushClip(r);
    dl.textIn(r, str, withAlpha(text, uint8_t(255 * a)), normal, Align::Center);
    dl.popClip();
}

void Studio::drawPalette() {
    Gui& g = *m_g;
    DrawList& dl = *m_dl;
    const float W = std::min(560.f, dl.width() - 40), x = (dl.width() - W) * 0.5f, y = 70;
    dl.rect({0, 0, dl.width(), dl.height()}, withAlpha(rgb(0, 0, 0), 120));
    struct Hit { const Command* c; int score; };
    std::vector<Hit> hits;
    const std::string q = lower(m_paletteQuery);
    std::vector<std::string> tokens;
    for (size_t i = 0; i < q.size();) {
        while (i < q.size() && q[i] == ' ') ++i;
        size_t j = i;
        while (j < q.size() && q[j] != ' ') ++j;
        if (j > i) tokens.push_back(q.substr(i, j - i));
        i = j;
    }
    for (const Command& c : m_commands) {
        if (c.label == "-" || c.label[0] == '~') continue;
        const std::string hay = lower(c.label), cat = lower(c.category);
        int score = 0;
        bool ok = true;
        for (const std::string& t : tokens) {
            const size_t p = hay.find(t);
            if (p != std::string::npos) score += p == 0 ? 30 : (hay[p - 1] == ' ' ? 20 : 8);
            else if (cat.find(t) != std::string::npos) score += 3;
            else { ok = false; break; }
        }
        if (ok) hits.push_back({&c, score - int(hay.size() / 8)});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.score > b.score; });
    const int shown = std::min<int>(int(hits.size()), 11);
    m_paletteSel = std::clamp(m_paletteSel, 0, std::max(0, shown - 1));
    const float rowH = 28, H = 54 + float(std::max(shown, 1)) * rowH + 10;
    const Rect panel{x, y, W, H};
    dl.shadow(panel, 10, 18, withAlpha(rgb(0, 0, 0), 170));
    dl.rrect(panel, 10, bg1);
    dl.rrectStroke(panel, 10, 1, border);
    g.setInputEnabled(true);
    const Id tid = id("palette-text");
    g.setFocus(tid);
    bool changed = false;
    // arrows and Enter are read before the text field sees them
    bool run = false;
    bool close = false;
    if (g.key(KeyDown, 0, true, true)) m_paletteSel = std::min(m_paletteSel + 1, std::max(0, shown - 1));
    if (g.key(KeyUp, 0, true, true)) m_paletteSel = std::max(0, m_paletteSel - 1);
    if (g.key(KeyEnter, 0, false, true)) run = true;
    if (g.key(KeyEscape, 0, false, true)) close = true;
    g.textField(tid, {x + 12, y + 12, W - 24, 30}, m_paletteQuery, "Type a command, e.g. quantize, riff, snapshot...", &changed);
    if (changed) m_paletteSel = 0;
    float ry = y + 52;
    if (hits.empty()) dl.textIn({x + 16, ry, W - 32, rowH}, tr("No command matches."), textMute, normal);
    for (int i = 0; i < shown; ++i) {
        const Command& c = *hits[size_t(i)].c;
        const Rect row{x + 6, ry, W - 12, rowH};
        const Id rid = id("palette-row", i);
        const Interact it = g.interact(rid, row, false);
        if (it.hovered) m_paletteSel = i;
        if (it.clicked) { m_paletteSel = i; run = true; }
        const bool en = !c.enabled || c.enabled();
        if (i == m_paletteSel) dl.rrect(row, 6, accentDim);
        dl.textIn({row.x + 10, row.y, 74, rowH}, tr(c.category), textMute, small);
        dl.textIn({row.x + 86, row.y, row.w - 86 - 110, rowH}, tr(c.label), en ? text : textMute, normal);
        if (!c.shortcut.empty()) dl.textIn({row.right() - 110, row.y, 100, rowH}, c.shortcut, textDim, small, Align::Right);
        ry += rowH;
    }
    if (run && shown > 0) {
        const Command c = *hits[size_t(m_paletteSel)].c;
        m_paletteOpen = false;
        g.clearFocus();
        if (!c.enabled || c.enabled()) m_afterFrame = [this, c] { c.run(); m_modelDirty = true; };
        else message(c.label + ": not available right now");
    }
    if (close || (g.mouseDown(0) && !panel.contains(g.mouseX(), g.mouseY()) && !g.anyActive())) { m_paletteOpen = false; g.clearFocus(); }
}

void Studio::drawHelpSheet() {
    Gui& g = *m_g;
    DrawList& dl = *m_dl;
    dl.rect({0, 0, dl.width(), dl.height()}, withAlpha(rgb(0, 0, 0), 150));
    const Rect panel{40, 40, dl.width() - 80, dl.height() - 80};
    dl.rrect(panel, 10, bg1);
    dl.rrectStroke(panel, 10, 1, border);
    g.setInputEnabled(true);
    dl.text(panel.x + 18, panel.y + 12, tr("Keyboard shortcuts"), text, big);
    dl.textIn({panel.right() - 260, panel.y + 12, 242, 20}, tr("Esc or F1 closes - Ctrl+K searches every command"), textMute, small, Align::Right);
    const Rect body = panel.inset(18, 0);
    Rect content{body.x, panel.y + 48, body.w, panel.h - 60};
    std::vector<std::string> cats;
    for (const Command& c : m_commands) if (std::find(cats.begin(), cats.end(), c.category) == cats.end()) cats.push_back(c.category);
    const int cols = std::max(1, int(content.w / 330));
    const float colW = content.w / float(cols);
    std::vector<float> ys(size_t(cols), 0.f);
    dl.pushClip(content);
    for (const std::string& cat : cats) {
        int n = 0;
        for (const Command& c : m_commands) if (c.category == cat && !c.shortcut.empty() && c.label != "-" && c.label[0] != '~') ++n;
        if (!n) continue;
        size_t col = size_t(std::min_element(ys.begin(), ys.end()) - ys.begin());
        float y = content.y + ys[col] - m_helpScroll;
        const float x = content.x + float(col) * colW;
        dl.text(x, y, tr(cat), accent, bold);
        y += 20;
        for (const Command& c : m_commands) {
            if (c.category != cat || c.shortcut.empty() || c.label == "-" || c.label[0] == '~') continue;
            dl.textIn({x, y, colW - 100, 18}, tr(c.label), text, small);
            dl.textIn({x + colW - 108, y, 96, 18}, c.shortcut, textDim, small, Align::Right);
            y += 18;
        }
        ys[col] += 20 + float(n) * 18 + 14;
    }
    dl.popClip();
    if (g.hovering(panel) && g.wheelY() != 0) { m_helpScroll = std::max(0.f, m_helpScroll - g.wheelY() * 40); g.consumeWheel(); }
    if (g.key(KeyEscape, 0, false, true) || g.key(KeyF1, 0, false, true)) m_helpOpen = false;
}

// ---------------------------------------------------------------------------------------------------------------- shortcuts

void Studio::handleShortcuts() {
    Gui& g = *m_g;
    if (g.textFocused()) return;
    for (const Command& c : m_commands) {
        if (!c.key || !c.run) continue;
        if (c.enabled && !c.enabled()) continue;   // a disabled command leaves its key to the next one (Z: typing octave or zoom)
        if (m_prFocus) {   // over the piano roll these keys edit notes (Logic Pro / Ableton Live), the roll reads them itself
            const bool arrow = c.key == KeyLeft || c.key == KeyRight || c.key == KeyUp || c.key == KeyDown;
            if (arrow || ((c.key == KeyDelete || c.key == KeyBackspace) && c.mods == 0) || (c.mods == ModCtrl && (c.key == 'D' || c.key == 'C' || c.key == 'V' || c.key == 'X' || c.key == 'A')) ||
                (c.mods == 0 && (c.key == 'Q' || c.key == 'B'))) continue;
        }
        if (g.key(c.key, c.mods, c.key == KeyLeft || c.key == KeyRight || c.key == KeyUp || c.key == KeyDown || c.key == KeyPlus || c.key == KeyMinus || c.key == KeyDelete)) {
            c.run();
            m_modelDirty = true;
            return;
        }
    }
    // Alt+1 ... 9 recalls a mixer snapshot
    for (int i = 0; i < 9; ++i)
        if (g.key('1' + i, ModAlt)) { if (m_s.recallMixSnapshot(i)) message(fmt("Recalled mixer snapshot %1", std::to_string(i + 1))); m_modelDirty = true; return; }
    if (m_typing) {
        static const char keys[] = "AWSEDFTGYHUJK";
        for (int i = 0; i < 13; ++i)
            if (g.key(keys[i], 0) && m_selTrack >= 0) { const int pitch = 12 * (m_typingOctave + 1) + i; auditionNote(m_selTrack, pitch); }
    }
}

bool Studio::runCommand(const std::string& label) {
    for (const Command& c : m_commands)
        if (c.label == label) { if (c.enabled && !c.enabled()) return false; c.run(); m_modelDirty = true; return true; }
    return false;
}

void Studio::addCmd(const char* category, const char* label, const char* shortcut, int key, int mods, std::function<void()> run, std::function<bool()> enabled, std::function<bool()> checked) {
    m_commands.push_back({category, label, shortcut, key, mods, std::move(run), std::move(enabled), std::move(checked)});
}

}  // namespace vkui
