// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Vulkan UI: the bottom dock (mixer, devices, snapshots), the inspector and the browser.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <regex>
#include "../dsp/Effects.h"
#include "Icons.h"
#include "Studio.h"
#include "Theme.h"

namespace vkui {
using namespace theme;

namespace {
std::string lowerS(std::string s) { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); }); return s; }
float gainToPos(float g) { return g <= 0.0005f ? 0.f : std::clamp((20.f * std::log10(g) + 60.f) / 66.f, 0.f, 1.f); }
float posToGain(float p) { return p <= 0.002f ? 0.f : std::pow(10.f, (p * 66.f - 60.f) / 20.f); }
std::string dbText(float g) { if (g <= 0.0005f) return "-inf"; char b[24]; std::snprintf(b, sizeof b, "%.1f dB", 20.f * std::log10(g)); return b; }
Color trackCol(const core::Track& t) { return Color(0xFF000000u) | (Color(t.color.r) << 16) | (Color(t.color.g) << 8) | Color(t.color.b); }
std::string paramText(const dsp::Param& p) {
    char b[48];
    const float v = p.get();
    if (p.stepped && !p.choices.empty()) return p.choices[size_t(std::clamp(int(v), 0, int(p.choices.size()) - 1))];
    if (std::fabs(v) >= 100) std::snprintf(b, sizeof b, "%.0f%s%s", v, p.unit.empty() ? "" : " ", p.unit.c_str());
    else std::snprintf(b, sizeof b, "%.2f%s%s", v, p.unit.empty() ? "" : " ", p.unit.c_str());
    return b;
}
}  // namespace

void Studio::addInsertTo(int track, const std::string& fxId) {
    if (track < 0 || track >= int(m_tracks.size())) return;
    m_s.addInsert(track, fxId);
    m_dock = DockTab::Devices;
    m_showDock = true;
    message("Effect added.");
}

// -------------------------------------------------------------------------------------------------------------- dock

void Studio::drawDock() {
    Gui& g = *m_g;
    DrawList& dl = *m_dl;
    const Rect r = m_rDock;
    dl.rect(r, bg1);
    dl.hline(r.x, r.right(), r.y, line);
    Rect bar{r.x, r.y, r.w, 30};
    dl.rect(bar, bg2);
    dl.hline(bar.x, bar.right(), bar.bottom() - 1, line);
    const char* names[] = {"Mixer", "Editor", "Steps", "Devices", "Snapshots", "Automation", "Drummer"};
    float x = r.x + 8;
    for (int i = 0; i < 7; ++i) {
        const float w = dl.textWidth(tr(names[i]), bold) + 26;
        const Rect tab{x, bar.y + 3, w, bar.h - 3};
        const bool on = int(m_dock) == i;
        const Id tid = id("dock-tab", i);
        const Interact it = g.interact(tid, tab, false);
        const float h = g.anim(tid, it.hovered ? 1.f : 0.f);
        dl.rrect({tab.x, tab.y, tab.w, tab.h + 2}, 6, on ? bg1 : mix(withAlpha(bg3, 0), bg3, h));
        dl.textIn(tab, tr(names[i]), on ? text : mix(textDim, text, h), bold, Align::Center);
        if (on) dl.rect({tab.x + 6, bar.bottom() - 3, tab.w - 12, 2}, accent);
        if (it.clicked) m_dock = DockTab(i);
        if (it.hovered) g.setCursor(Cursor::Hand);
        x += w + 2;
    }
    {   // selection summary on the right
        std::string s;
        if (m_selTrack >= 0 && m_selTrack < int(m_tracks.size())) s = m_tracks[size_t(m_selTrack)].name;
        if (hasRegion()) s += "  >  " + region()->name;
        dl.textIn({r.right() - 340, bar.y, 290, bar.h}, s, textMute, small, Align::Right);
        if (g.iconButton(id("dock-close"), {r.right() - 30, bar.y + 4, 22, 22}, icons::minus, "Hide the bottom panel (X)")) m_showDock = false;
    }
    const Rect body{r.x, bar.bottom(), r.w, r.h - bar.h};
    dl.pushClip(body);
    switch (m_dock) {
    case DockTab::Mixer: drawMixer(body); break;
    case DockTab::Editor: drawEditor(body); break;
    case DockTab::Steps: drawSteps(body); break;
    case DockTab::Devices: drawDevices(body); break;
    case DockTab::Snapshots: drawSnapshots(body); break;
    case DockTab::Automation: drawAutomation(body); break;
    case DockTab::Drummer: drawDrummer(body); break;
    }
    dl.popClip();
}

// ------------------------------------------------------------------------------------------------------------- mixer

namespace {
// Logic / Pro Tools style fader scale: tick marks and labels at the usual dB values
void faderScale(DrawList& dl, const Rect& fr, float x, bool labels) {
    static const int dbs[] = {6, 0, -6, -12, -24, -48};
    for (int d : dbs) {
        const float pos = gainToPos(std::pow(10.f, float(d) / 20.f));
        const float y = fr.y + 8 + (fr.h - 16) * (1.f - pos);
        dl.hline(x, x + (d == 0 ? 6.f : 4.f), y, d == 0 ? textDim : textMute);
        if (labels && (d == 0 || d == -12 || d == -24 || d == -48)) { char b[8]; std::snprintf(b, sizeof b, "%d", d); dl.text(x + 8, y - 5, b, d == 0 ? textDim : textMute, {9, false}); }
    }
}
// segmented meter: the unlit part shows the cells, the lit part the level
void segMeter(DrawList& dl, const Rect& m, float level) {
    dl.rrect(m, 2, rgb(0x0a, 0x0c, 0x10));
    const float t = gainToPos(level);
    if (t > 0.01f) {
        dl.pushClip({m.x, m.bottom() - m.h * t, m.w, m.h * t});
        dl.rect(m, meterLow);
        dl.rect({m.x, m.y, m.w, m.h * 0.18f}, meterHigh);
        dl.rect({m.x, m.y + m.h * 0.18f, m.w, m.h * 0.14f}, meterMid);
        dl.popClip();
    }
    for (float y = m.y + 3; y < m.bottom(); y += 4) dl.hline(m.x, m.right(), y, withAlpha(rgb(0x0a, 0x0c, 0x10), 150));
}
std::string peakText(float peak) {
    if (peak <= 0.0005f) return "-inf";
    char b[24];
    std::snprintf(b, sizeof b, "%.1f", 20.f * std::log10(peak));
    return b;
}
}  // namespace

void Studio::drawMixer(const Rect& r) {
    Gui& g = *m_g;
    DrawList& dl = *m_dl;
    // filter bar (Logic: Audio / Inst / ...; Cubase: narrow channels)
    const float barH = 24;
    {
        const Rect bar{r.x, r.y, r.w, barH};
        dl.rect(bar, bg1);
        dl.hline(bar.x, bar.right(), bar.bottom() - 1, line);
        const char* names[] = {"All", "Audio", "Instruments", "Drums"};
        float x = r.x + 8;
        for (int i = 0; i < 4; ++i) {
            const float w = dl.textWidth(tr(names[i]), small) + 18;
            bool on = m_mixFilter == i;
            if (g.toggle(id("mixf", i), {x, bar.y + 3, w, 18}, tr(names[i]), on, accent)) { m_mixFilter = i; m_mixScroll = 0; }
            x += w + 3;
        }
        bool nar = m_mixNarrow;
        if (g.toggle(id("mixnarrow"), {x + 10, bar.y + 3, 62, 18}, tr("Narrow"), nar, accent)) m_mixNarrow = nar;
        g.tip(id("mixnarrow"), "Narrow channel strips: more channels on screen");
    }
    const Rect body{r.x, r.y + barH, r.w, r.h - barH};
    const float stripW = m_mixNarrow ? 76.f : 96.f, masterW = 104;
    Rect strips{body.x, body.y, body.w - masterW, body.h};
    std::vector<int> shown;
    for (int i = 0; i < int(m_tracks.size()); ++i) {
        const core::TrackType ty = m_tracks[size_t(i)].type;
        const bool ok = m_mixFilter == 0 || (m_mixFilter == 1 && ty == core::TrackType::Audio) || (m_mixFilter == 2 && ty == core::TrackType::Instrument) ||
                        (m_mixFilter == 3 && (ty == core::TrackType::Drums || ty == core::TrackType::Drummer));
        if (ok) shown.push_back(i);
    }
    const float content = float(shown.size()) * stripW + 8;
    if (g.hovering(strips) && (g.wheelY() != 0 || g.wheelX() != 0)) { m_mixScroll = std::clamp(m_mixScroll - (g.wheelX() + g.wheelY()) * 50.f, 0.f, std::max(0.f, content - strips.w)); g.consumeWheel(); }
    dl.pushClip(strips);
    for (size_t k = 0; k < shown.size(); ++k) {
        const Rect c{strips.x + 4 + float(k) * stripW - m_mixScroll, strips.y + 4, stripW - 4, strips.h - 12};
        if (c.right() < strips.x || c.x > strips.right()) continue;
        drawChannel(c, shown[k]);
    }
    if (shown.empty()) dl.textIn(strips, tr("No channels of this kind."), textMute, normal, Align::Center);
    dl.popClip();
    float sx = m_mixScroll;
    g.scrollBar(id("mixscroll"), {strips.x, r.bottom() - 8, strips.w, 8}, sx, content, strips.w, false);
    m_mixScroll = sx;
    // master channel
    {
        const Rect c{r.right() - masterW + 2, body.y + 4, masterW - 6, body.h - 8};
        dl.rrect(c, 8, bg2);
        dl.rrectStroke(c, 8, 1, border);
        dl.rrect({c.x, c.y, c.w, 22}, 8, rgb(0x3a, 0x40, 0x50));
        dl.textIn({c.x, c.y, c.w, 22}, "Master", text, bold, Align::Center);
        float vol = gainToPos(m_s.masterVolume());
        const Rect fr{c.x + 10, c.y + 30, 38, c.h - 78};
        if (g.fader(id("master-fader"), fr, vol, 0.f, 1.f, gainToPos(0.8f))) m_s.setMasterVolume(posToGain(vol));
        faderScale(dl, fr, fr.right() + 2, false);
        segMeter(dl, {c.x + 56, fr.y + 8, 7, fr.h - 16}, m_meterL);
        segMeter(dl, {c.x + 66, fr.y + 8, 7, fr.h - 16}, m_meterR);
        const Rect vb{c.x + 6, c.bottom() - 44, (c.w - 16) / 2.f, 16}, pb{vb.right() + 4, vb.y, vb.w, 16};
        dl.rrect(vb, 3, bg0);
        dl.textIn(vb, dbText(m_s.masterVolume()).substr(0, 5), textDim, small, Align::Center);
        const Id pid = id("master-peak");
        const Interact pit = g.interact(pid, pb, false);
        dl.rrect(pb, 3, bg0);
        dl.textIn(pb, peakText(m_masterHold), m_masterHold >= 1.f ? record : textDim, small, Align::Center);
        if (pit.clicked) m_masterHold = 0;
        if (pit.hovered) { g.setCursor(Cursor::Hand); setHint("Peak hold - click to reset"); }
        dl.rrect({c.x + 4, c.bottom() - 24, c.w - 8, 18}, 4, rgb(0x3a, 0x40, 0x50));
        dl.textIn({c.x + 4, c.bottom() - 24, c.w - 8, 18}, "Stereo Out", text, small, Align::Center);
    }
}

void Studio::drawChannel(const Rect& c, int ti) {
    Gui& g = *m_g;
    DrawList& dl = *m_dl;
    const core::Track& t = m_tracks[size_t(ti)];
    const Color col = trackCol(t);
    const bool sel = ti == m_selTrack;
    const bool narrow = c.w < 84;
    // FL Studio: the whole strip carries a tint of the track colour
    dl.rrect(c, 8, mix(sel ? rgb(0x27, 0x2c, 0x38) : bg2, col, sel ? 0.12f : 0.07f));
    dl.rrectStroke(c, 8, 1, sel ? col : border);
    {   // header: number and kind (Cubase / Pro Tools); click selects, double-click renames
        const Id hid = g.id("ch-head", t.id);
        const Interact it = g.interact(hid, {c.x, c.y, c.w, 22}, false);
        if (it.pressed) { m_selTrack = ti; m_selRegion = -1; }
        if (it.doubleClicked) dlgRenameTrack(ti);
        dl.rrect({c.x, c.y, c.w, 22}, 8, mix(bg3, col, 0.65f));
        dl.rect({c.x, c.y + 12, c.w, 10}, mix(bg3, col, 0.65f));
        static const char* kind[] = {"Audio", "Inst", "Drums", "Drummer"};
        dl.textIn({c.x + 4, c.y, c.w - 28, 22}, std::to_string(ti + 1) + (narrow ? "" : "  " + tr(kind[int(t.type)])), rgb(255, 255, 255), bold, Align::Center);   // leaves room for the group badge
        // mixer group badge (Pro Tools / Logic Pro groups): click for the menu, Alt while changing a control bypasses the group
        const Rect gb{c.right() - 20, c.y + 3, 16, 16};
        const Interact gi = g.interact(g.id("ch-group", t.id), gb, false);
        if (t.group > 0) {
            dl.circle(gb.cx(), gb.cy(), 8, groupColor(t.group));
            const char letter[2] = {char('A' + t.group - 1), 0};
            dl.textIn(gb, letter, rgb(20, 22, 28), bold, Align::Center);
        } else {
            dl.ring(gb.cx(), gb.cy(), 7, 1.2f, withAlpha(rgb(255, 255, 255), gi.hovered ? 200 : 70));
        }
        if (gi.hovered) setHint("Mixer group: tracks in the same group move their faders, pans, sends, mute and solo together (hold Alt to change only one)");
        if (gi.clicked) { m_selTrack = ti; g.openMenu(gb.x, gb.bottom(), groupMenuItems(ti)); }
    }
    float y = c.y + 26;
    // inserts: a fixed number of rows so that all faders line up
    const int slots = c.h > 330 ? 4 : 2;
    const int shownInserts = std::min<int>(int(t.inserts.size()), slots);
    for (int k = 0; k < shownInserts; ++k) {
        const auto& fx = t.inserts[size_t(k)];
        const Rect row{c.x + 5, y + float(k) * 20, c.w - 10, 18};
        const Id rid = g.id("ins", t.id * 16 + k);
        const Interact it = g.interact(rid, {row.x, row.y, row.w - 20, row.h}, false);
        const bool byp = fx->bypass.load();
        dl.rrect(row, 4, mix(bg1, it.hovered ? accentDim : bg1, 0.8f));
        dl.circle(row.x + 8, row.cy(), 3, byp ? textMute : play);
        dl.textIn({row.x + 16, row.y, row.w - 36, row.h}, fx->displayName(), byp ? textMute : text, small);
        if (it.clicked) { m_selTrack = ti; m_devSel = k; m_dock = DockTab::Devices; }
        if (it.rightClicked) {
            std::vector<MenuItem> items;
            MenuItem a; a.label = byp ? "Enable" : "Bypass"; a.action = [this, ti, k, byp] { m_s.setInsertBypass(ti, k, !byp); m_modelDirty = true; };
            MenuItem b; b.label = "Remove"; b.action = [this, ti, k] { m_s.removeInsert(ti, k); m_modelDirty = true; };
            g.openMenu(g.mouseX(), g.mouseY(), {a, b});
        }
        const Rect x{row.right() - 16, row.y + 1, 15, 16};
        if (g.button(g.id("ins-x", t.id * 16 + k), x, "x", ButtonKind::Flat)) { m_s.removeInsert(ti, k); m_modelDirty = true; }
    }
    for (int k = shownInserts; k < slots; ++k) dl.rrectStroke({c.x + 5, y + float(k) * 20, c.w - 10, 18}, 4, 1, withAlpha(border, 120));
    y += float(slots) * 20;
    {
        const Rect add{c.x + 5, y, c.w - 10, 18};
        const std::string label = int(t.inserts.size()) > slots ? "+ Insert  (" + std::to_string(int(t.inserts.size()) - slots) + " more)" : "+ Insert";
        if (g.button(g.id("ins-add", t.id), add, narrow ? "+ FX" : label, ButtonKind::Flat)) {
            std::vector<MenuItem> items;
            for (const auto& e : m_effectNames) { MenuItem m; m.label = e.second; const std::string fxId = e.first; m.action = [this, ti, fxId] { addInsertTo(ti, fxId); }; items.push_back(m); }
            if (m_host.plugins) for (const PluginEntry& p : m_host.plugins()) if (!p.instrument) { MenuItem m; m.label = p.name; const std::string pid = p.id; m.action = [this, ti, pid] { addInsertTo(ti, pid); }; items.push_back(m); }
            g.openMenu(add.x, add.bottom(), std::move(items));
        }
        y += 22;
    }
    // sends (Ableton: A / B) and pan; the narrow strip keeps only the pan knob
    {
        float a = t.sendA, b = t.sendB, p = t.pan;
        char pb[16];
        std::snprintf(pb, sizeof pb, p == 0 ? "C" : p < 0 ? "L%d" : "R%d", int(std::fabs(p) * 100));
        if (narrow) {
            if (g.knob(g.id("pan", t.id), {c.x + 8, y, c.w - 16, 40}, p, -1.f, 1.f, 0.f, "Pan", pb, accent)) m_s.setPan(ti, p);
        } else {
            const float kw = (c.w - 16) / 3.f;
            if (g.knob(g.id("sa", t.id), {c.x + 8, y, kw, 40}, a, 0.f, 1.f, 0.f, "Rev", {}, rgb(0x4f, 0xc3, 0xe8))) m_s.setSendA(ti, a);
            if (g.knob(g.id("sb", t.id), {c.x + 8 + kw, y, kw, 40}, b, 0.f, 1.f, 0.f, "Echo", {}, rgb(0xb9, 0xd9, 0x4a))) m_s.setSendB(ti, b);
            if (g.knob(g.id("pan", t.id), {c.x + 8 + 2 * kw, y, kw, 40}, p, -1.f, 1.f, 0.f, "Pan", pb, accent)) m_s.setPan(ti, p);
        }
        y += 44;
    }
    // buttons
    {
        bool arm = t.armed, mu = t.mute, so = t.solo;
        const float bw = (c.w - 6) / 3.f;
        if (g.toggle(g.id("c-arm", t.id), {c.x + 3, y, bw - 2, 20}, "R", arm, record)) m_s.setArmed(ti, arm);
        if (g.toggle(g.id("c-mute", t.id), {c.x + 3 + bw, y, bw - 2, 20}, "M", mu, mute)) m_s.setMute(ti, mu);
        if (g.toggle(g.id("c-solo", t.id), {c.x + 3 + 2 * bw, y, bw - 2, 20}, "S", so, solo)) m_s.setSolo(ti, so);
        y += 24;
    }
    // fader with scale, and a segmented stereo meter
    const float footer = 46;   // volume and peak readouts + the coloured name plate (Logic)
    const Rect fr{c.x + 6, y, 36, std::max(40.f, c.bottom() - y - footer)};
    float pos = gainToPos(t.volume);
    if (g.fader(g.id("fader", t.id), fr, pos, 0.f, 1.f, gainToPos(0.8f), col)) m_s.setVolume(ti, posToGain(pos));
    faderScale(dl, fr, fr.right() + 1, !narrow && fr.h >= 80);
    const float mx = c.right() - (narrow ? 20.f : 24.f);
    segMeter(dl, {mx, fr.y + 8, 7, fr.h - 16}, t.peakL);
    segMeter(dl, {mx + 9, fr.y + 8, 7, fr.h - 16}, t.peakR);
    {   // Ableton / Pro Tools: the volume as a number, next to the peak hold; click the peak to reset it
        const float hold = peakHold(t.id);
        const Rect vb{c.x + 4, c.bottom() - footer + 2, (c.w - 12) / 2.f, 16}, pbx{vb.right() + 4, vb.y, vb.w, 16};
        dl.rrect(vb, 3, bg0);
        dl.textIn(vb, narrow ? dbText(t.volume).substr(0, 4) : dbText(t.volume).substr(0, 5), textDim, small, Align::Center);
        const Id pid = g.id("peak", t.id);
        const Interact pit = g.interact(pid, pbx, false);
        dl.rrect(pbx, 3, bg0);
        dl.textIn(pbx, peakText(hold), hold >= 1.f ? record : textDim, small, Align::Center);
        if (pit.clicked) m_peakHold[t.id] = 0;
        if (pit.hovered) { g.setCursor(Cursor::Hand); setHint("Peak hold - click to reset"); }
        // Logic: the channel name on a plate in the track colour at the bottom of the strip
        const Rect plate{c.x + 4, c.bottom() - 24, c.w - 8, 20};
        dl.rrect(plate, 4, mix(bg3, col, 0.8f));
        dl.textIn({plate.x + 3, plate.y, plate.w - 6, plate.h}, t.name, rgb(255, 255, 255), small, Align::Center);
    }
}

// ------------------------------------------------------------------------------------------------------------ devices

void Studio::drawDevices(const Rect& r) {
    Gui& g = *m_g;
    DrawList& dl = *m_dl;
    if (m_selTrack < 0 || m_selTrack >= int(m_tracks.size())) { dl.textIn(r, tr("Select a track to see its instrument and effects."), textMute, normal, Align::Center); return; }
    const core::Track& t = m_tracks[size_t(m_selTrack)];
    float content = 12;
    auto cardW = [&](const dsp::ParamSet& ps) { return std::max(150.f, std::ceil(float(ps.params.size()) / 2.f) * 62.f + 22.f); };
    if (t.instrument) content += cardW(*t.instrument) + 10;
    for (const auto& fx : t.inserts) content += cardW(*fx) + 10;
    content += 90;
    if (g.hovering(r) && (g.wheelY() != 0 || g.wheelX() != 0)) { m_mixScroll = std::clamp(m_mixScroll - (g.wheelX() + g.wheelY()) * 50.f, 0.f, std::max(0.f, content - r.w)); g.consumeWheel(); }
    float x = r.x + 10 - m_mixScroll;
    const float top = r.y + 8, h = r.h - 22;
    auto card = [&](const std::string& title, const std::shared_ptr<dsp::ParamSet>& ps, int cardId, bool removable, bool bypassable, bool* bypass, Color accentCol, std::function<void()> onRemove, std::function<void(Rect)> extraHeader) {
        const float w = cardW(*ps);
        const Rect c{x, top, w, h};
        x += w + 10;
        if (c.right() < r.x || c.x > r.right()) return;
        dl.rrect(c, 8, bg2);
        dl.rrectStroke(c, 8, 1, cardId == m_devSel ? accentCol : border);
        dl.rrect({c.x, c.y, c.w, 24}, 8, mix(bg3, accentCol, 0.35f));
        dl.rect({c.x, c.y + 14, c.w, 10}, mix(bg3, accentCol, 0.35f));
        dl.textIn({c.x + 10, c.y, c.w - 70, 24}, title, text, bold);
        Rect hx{c.right() - 24, c.y + 3, 20, 18};
        if (removable && g.button(g.id("dev-x", cardId + 1000 * m_selTrack), hx, "x", ButtonKind::Flat)) { if (onRemove) onRemove(); }
        if (bypassable && bypass) {
            bool b = *bypass;
            if (g.toggle(g.id("dev-bp", cardId + 1000 * m_selTrack), {c.right() - 24 - 40, c.y + 3, 38, 18}, "On", b, play)) { m_s.setInsertBypass(m_selTrack, cardId, !b); m_modelDirty = true; }
        }
        if (extraHeader) extraHeader(c);
        const int n = int(ps->params.size());
        const int perCol = 2;
        for (int i = 0; i < n; ++i) {
            dsp::Param& p = *ps->params[size_t(i)];
            const int col = i / perCol, row = i % perCol;
            const Rect k{c.x + 12 + float(col) * 62.f, c.y + 34 + float(row) * (h - 40) / 2.f, 56, (h - 40) / 2.f - 2};
            const Id kid = g.id("dp", cardId * 64 + i + 100000 * m_selTrack);
            if (p.stepped && !p.choices.empty()) {
                int idx = int(p.get());
                dl.textIn({k.x, k.y, k.w, 12}, p.name, textDim, small, Align::Center);
                if (g.dropdown(kid, {k.x, k.y + 16, k.w, 22}, idx, p.choices)) { p.set(float(idx)); m_s.markDirty(); }
                continue;
            }
            float v01 = 0;
            if (p.log && p.min > 0) v01 = std::log(std::max(p.get(), p.min) / p.min) / std::log(p.max / p.min);
            else v01 = (p.get() - p.min) / std::max(1e-9f, p.max - p.min);
            float vv = v01;
            const float defT = p.log && p.min > 0 ? std::log(std::max(p.def, p.min) / p.min) / std::log(p.max / p.min) : (p.def - p.min) / std::max(1e-9f, p.max - p.min);
            if (g.knob(kid, k, vv, 0.f, 1.f, defT, p.name, paramText(p), accentCol)) {
                const float nv = p.log && p.min > 0 ? p.min * std::pow(p.max / p.min, vv) : p.min + vv * (p.max - p.min);
                p.set(p.stepped ? std::round(nv) : nv);
                m_s.markDirty();
            }
        }
    };
    if (t.instrument) {
        const auto ps = std::static_pointer_cast<dsp::ParamSet>(t.instrument);
        const std::string title = t.instrumentId == "drumkit" ? "Drum Kit" : t.instrumentId == "synth" ? "Synth" : t.instrumentId;
        card(title, ps, -1, false, false, nullptr, rgb(0x6f, 0xa6, 0x6a), nullptr, [&](Rect c) {
            const int count = t.instrument->presetCount();
            if (count > 1) {
                std::vector<std::string> names;
                for (int i = 0; i < count; ++i) names.push_back(t.instrument->presetName(i));
                int idx = t.preset;
                if (g.dropdown(g.id("preset", t.id), {c.x + c.w - 150, c.y + 2, 100, 20}, idx, names)) { m_s.setInstrumentPreset(m_selTrack, idx); m_modelDirty = true; }
            }
        });
    }
    for (int i = 0; i < int(t.inserts.size()); ++i) {
        bool byp = !t.inserts[size_t(i)]->bypass.load();
        const int slot = i;
        card(t.inserts[size_t(i)]->displayName(), std::static_pointer_cast<dsp::ParamSet>(t.inserts[size_t(i)]), i, true, true, &byp, rgb(0xe8, 0x9a, 0x3c), [this, slot] { m_s.removeInsert(m_selTrack, slot); m_devSel = 0; m_modelDirty = true; }, nullptr);
    }
    {   // add an effect
        const Rect add{x, top, 120, 34};
        if (g.button(id("dev-add"), add, "+ Add Effect", ButtonKind::Accent)) {
            std::vector<MenuItem> items;
            const int tr_ = m_selTrack;
            for (const auto& e : m_effectNames) { MenuItem m; m.label = e.second; const std::string fxId = e.first; m.action = [this, tr_, fxId] { addInsertTo(tr_, fxId); }; items.push_back(m); }
            g.openMenu(add.x, add.bottom() + 2, std::move(items));
        }
        // the shared aux effects
        const Rect a{x, top + 44, 120, 26}, b{x, top + 76, 120, 26};
        if (g.button(id("aux-a"), a, "Aux A: Reverb...", ButtonKind::Normal)) dlgAux(0);
        if (g.button(id("aux-b"), b, "Aux B: Echo...", ButtonKind::Normal)) dlgAux(1);
    }
}

// ---------------------------------------------------------------------------------------------------------- snapshots

void Studio::drawSnapshots(const Rect& r) {
    Gui& g = *m_g;
    DrawList& dl = *m_dl;
    const auto snaps = m_s.mixSnapshots();
    dl.text(r.x + 14, r.y + 10, tr("MixConsole Snapshots"), text, title);
    dl.text(r.x + 14, r.y + 28, tr("Store levels, pans, sends, mutes and solos under a name and bring them back with one click or Alt+1 ... 9."), textMute, small);
    if (g.button(id("snap-save"), {r.x + 14, r.y + 50, 150, 28}, "+ Save Snapshot", ButtonKind::Accent)) {
        const int n = m_s.saveMixSnapshot();
        message("Saved snapshot " + std::to_string(n + 1));
    }
    float x = r.x + 14;
    const float cw = 178, top = r.y + 90;
    for (int i = 0; i < int(snaps.size()); ++i) {
        const Rect c{x, top, cw, r.h - 100};
        x += cw + 10;
        if (c.right() < r.x || c.x > r.right()) continue;
        dl.rrect(c, 8, bg2);
        dl.rrectStroke(c, 8, 1, border);
        dl.textIn({c.x + 10, c.y + 4, c.w - 44, 22}, std::to_string(i + 1) + "  " + snaps[size_t(i)].name, text, bold);
        // mini mixer: one bar per channel
        const Rect mini{c.x + 10, c.y + 32, c.w - 20, c.h - 76};
        dl.rrect(mini, 4, bg0);
        const int n = int(snaps[size_t(i)].channels.size());
        const float bw = n ? std::min(14.f, (mini.w - 6) / float(n)) : 0;
        for (int k = 0; k < n; ++k) {
            const auto& ch = snaps[size_t(i)].channels[size_t(k)];
            const float hgt = (mini.h - 8) * std::clamp(gainToPos(ch.volume), 0.02f, 1.f);
            Color col = rgb(0x5b, 0x8c, 0xff);
            for (const core::Track& t : m_tracks) if (t.id == ch.trackId) col = trackCol(t);
            dl.rect({mini.x + 4 + float(k) * bw, mini.bottom() - 4 - hgt, std::max(2.f, bw - 2), hgt}, ch.mute ? withAlpha(col, 70) : col);
        }
        if (g.button(g.id("snap-recall", i), {c.x + 10, c.bottom() - 36, 80, 26}, "Recall", ButtonKind::Normal)) { if (m_s.recallMixSnapshot(i)) message(fmt("Recalled %1", snaps[size_t(i)].name)); m_modelDirty = true; }
        if (g.button(g.id("snap-name", i), {c.x + 94, c.bottom() - 36, 36, 26}, "Aa", ButtonKind::Flat)) dlgSnapshotName(i);
        if (g.button(g.id("snap-del", i), {c.x + 134, c.bottom() - 36, 34, 26}, "x", ButtonKind::Flat)) { m_s.removeMixSnapshot(i); m_modelDirty = true; }
        g.tip(g.id("snap-name", i), "Rename");
        g.tip(g.id("snap-del", i), "Delete");
    }
    if (snaps.empty()) dl.text(r.x + 180, r.y + 57, tr("No snapshots yet - set up a balance you like, then save it."), textMute, normal);
}

// -------------------------------------------------------------------------------------------------------------- inspector

void Studio::drawInspector(const Rect& r) {
    Gui& g = *m_g;
    DrawList& dl = *m_dl;
    dl.rect(r, bg1);
    dl.vline(r.x, r.y, r.bottom(), line);
    dl.text(r.x + 14, r.y + 10, tr("INSPECTOR"), textMute, {10, true});
    const Rect view{r.x, r.y + 30, r.w, r.h - 30};
    dl.pushClip(view);
    if (g.hovering(view) && g.wheelY() != 0) { m_inspScroll = std::max(0.f, m_inspScroll - g.wheelY() * 40.f); g.consumeWheel(); }
    float y = view.y + 4 - m_inspScroll;
    const float x = r.x + 12, w = r.w - 24;
    auto header = [&](const std::string& s) { dl.text(x, y, tr(s), accent, bold); y += 22; };
    auto row = [&](float h) { Rect q{x, y, w, h}; y += h + 6; return q; };
    if (m_selTrack < 0 || m_selTrack >= int(m_tracks.size())) { dl.textIn(view, tr("No track selected"), textMute, normal, Align::Center); dl.popClip(); return; }
    const core::Track& t = m_tracks[size_t(m_selTrack)];
    const int ti = m_selTrack;
    header("Track");
    {
        std::string name = t.name;
        bool changed = false;
        if (g.textField(g.id("insp-name", t.id), row(26), name, "Name", &changed)) m_s.renameTrack(ti, name);
        // colour swatches
        const Rect sw = row(18);
        for (int i = 0; i < 12; ++i) {
            const Rect q{sw.x + float(i) * (sw.w / 12.f), sw.y, sw.w / 12.f - 3, 18};
            const Id cid = g.id("swatch", t.id * 16 + i);
            const Interact it = g.interact(cid, q, false);
            const Color c = trackColor(i);
            dl.rrect(q, 4, c);
            if (it.hovered) dl.rrectStroke(q, 4, 1.5f, rgb(255, 255, 255));
            if (it.clicked) { core::Color cc; cc.r = std::uint8_t((c >> 16) & 255); cc.g = std::uint8_t((c >> 8) & 255); cc.b = std::uint8_t(c & 255); m_s.setTrackColor(ti, cc); m_modelDirty = true; }
        }
        float vol = t.volume, pan = t.pan;
        Rect q = row(22);
        dl.textIn({q.x, q.y, 54, q.h}, tr("Volume"), textDim, small);
        if (g.slider(g.id("insp-vol", t.id), {q.x + 54, q.y, q.w - 54, q.h}, vol, 0.f, 1.5f, 0.8f, dbText(vol))) m_s.setVolume(ti, vol);
        q = row(22);
        dl.textIn({q.x, q.y, 54, q.h}, tr("Pan"), textDim, small);
        char pb[16]; std::snprintf(pb, sizeof pb, pan == 0 ? "C" : pan < 0 ? "L%d" : "R%d", int(std::fabs(pan) * 100));
        if (g.slider(g.id("insp-pan", t.id), {q.x + 54, q.y, q.w - 54, q.h}, pan, -1.f, 1.f, 0.f, pb)) m_s.setPan(ti, pan);
        if (t.instrument) {
            q = row(22);
            dl.textIn({q.x, q.y, 54, q.h}, tr("Sound"), textDim, small);
            const int count = t.instrument->presetCount();
            std::vector<std::string> names;
            for (int i = 0; i < count; ++i) names.push_back(t.instrument->presetName(i));
            int idx = t.preset;
            if (count > 0 && g.dropdown(g.id("insp-preset", t.id), {q.x + 54, q.y, q.w - 54, q.h}, idx, names)) { m_s.setInstrumentPreset(ti, idx); m_modelDirty = true; }
        }
        if (t.type == core::TrackType::Instrument) {   // the channel of the track in an exported MIDI file
            q = row(22);
            dl.textIn({q.x, q.y, 54, q.h}, tr("MIDI Ch"), textDim, small);
            std::vector<std::string> chans{tr("Auto")};
            for (int i = 1; i <= 16; ++i) chans.push_back(std::to_string(i));
            int ch = t.midiChannel;
            if (g.dropdown(g.id("insp-midich", t.id), {q.x + 54, q.y, q.w - 54, q.h}, ch, chans)) { m_s.setTrackMidiChannel(ti, ch); m_modelDirty = true; }
        }
    }
    y += 6;
    if (hasRegion()) {
        const core::Region& rg = *region();
        const int ri = m_selRegion;
        header(rg.midi ? "MIDI Region" : "Audio Region");
        std::string name = rg.name;
        bool changed = false;
        if (g.textField(g.id("insp-rname", ti * 4096 + ri), row(26), name, "Region name", &changed)) m_s.renameRegion(ti, ri, name);
        {
            char b[96];
            const double beats = m_s.regionBeats(rg);
            std::snprintf(b, sizeof b, "Start %.2f   Length %.2f beats", rg.start, beats);
            dl.textIn(row(16), b, textDim, small);
        }
        bool mu = rg.mute;
        if (g.toggle(g.id("insp-rmute", ti * 4096 + ri), row(24), "Mute Region", mu, mute)) m_s.setRegionMute(ti, ri, mu);
        if (!rg.midi) {
            float gain = rg.gainDb;
            const float fiMs = float(rg.fadeIn) * 1000.f / float(::kSampleRate), foMs = float(rg.fadeOut) * 1000.f / float(::kSampleRate);
            float fi = fiMs, fo = foMs;
            char b[24];
            Rect q = row(22);
            dl.textIn({q.x, q.y, 60, q.h}, tr("Gain"), textDim, small);
            std::snprintf(b, sizeof b, "%.1f dB", gain);
            bool ch = g.slider(g.id("insp-gain", ti * 4096 + ri), {q.x + 60, q.y, q.w - 60, q.h}, gain, -24.f, 12.f, 0.f, b);
            q = row(22);
            dl.textIn({q.x, q.y, 60, q.h}, tr("Fade In"), textDim, small);
            std::snprintf(b, sizeof b, "%.0f ms", fi);
            ch |= g.slider(g.id("insp-fi", ti * 4096 + ri), {q.x + 60, q.y, q.w - 60, q.h}, fi, 0.f, 2000.f, 0.f, b);
            q = row(22);
            dl.textIn({q.x, q.y, 60, q.h}, tr("Fade Out"), textDim, small);
            std::snprintf(b, sizeof b, "%.0f ms", fo);
            ch |= g.slider(g.id("insp-fo", ti * 4096 + ri), {q.x + 60, q.y, q.w - 60, q.h}, fo, 0.f, 2000.f, 0.f, b);
            if (ch) m_s.setRegionAudio(ti, ri, gain, core::i64(fi * ::kSampleRate / 1000.f), core::i64(fo * ::kSampleRate / 1000.f));
            bool follow = rg.follow;
            if (g.toggle(g.id("insp-follow", ti * 4096 + ri), row(24), "Follow Tempo", follow, accent)) m_s.setRegionFlex(ti, ri, follow, rg.sourceBpm > 0 ? rg.sourceBpm : m_s.bpm(), rg.semitones);
            Rect two = row(26);
            if (g.button(g.id("insp-norm", ti), {two.x, two.y, two.w / 2 - 2, 26}, "Normalize")) m_s.normalizeRegion(ti, ri);
            if (g.button(g.id("insp-rev", ti), {two.x + two.w / 2 + 2, two.y, two.w / 2 - 2, 26}, "Reverse")) m_s.reverseRegion(ti, ri);
            two = row(26);
            if (g.button(g.id("insp-bd", ti), {two.x, two.y, two.w / 2 - 2, 26}, "Beat Detective...")) dlgBeatDetective();
            if (g.button(g.id("insp-adapt", ti), {two.x + two.w / 2 + 2, two.y, two.w / 2 - 2, 26}, "Adapt Tempo...")) dlgAdaptTempo();
        } else {
            Rect two = row(26);
            if (g.button(g.id("insp-q", ti), {two.x, two.y, two.w / 2 - 2, 26}, "Quantize...")) dlgQuantize();
            if (g.button(g.id("insp-t", ti), {two.x + two.w / 2 + 2, two.y, two.w / 2 - 2, 26}, "Transpose...")) dlgTranspose();
            two = row(26);
            if (g.button(g.id("insp-riff", ti), {two.x, two.y, two.w / 2 - 2, 26}, "Riff Machine...")) dlgRiff();
            if (g.button(g.id("insp-log", ti), {two.x + two.w / 2 + 2, two.y, two.w / 2 - 2, 26}, "Logical Editor...")) dlgLogical();
            two = row(26);
            if (g.button(g.id("insp-mk", ti), {two.x, two.y, two.w / 2 - 2, 26}, "Make Groove")) { m_s.makeGrooveTemplate(ti, ri, 0.25); }
            if (g.button(g.id("insp-ap", ti), {two.x + two.w / 2 + 2, two.y, two.w / 2 - 2, 26}, "Apply Groove...", ButtonKind::Normal, m_s.hasGrooveTemplate())) dlgGroove();
        }
    } else {
        dl.textIn(row(32), tr("Select a region to see its properties."), textMute, small);
    }
    const float total = y - (view.y + 4 - m_inspScroll) + 20;
    m_inspScroll = std::clamp(m_inspScroll, 0.f, std::max(0.f, total - view.h));
    dl.popClip();
}

// --------------------------------------------------------------------------------------------------------------- browser

void Studio::reloadLoops() {
    namespace fs = std::filesystem;
    m_loops.clear();
    m_loopsLoaded = true;
    std::error_code ec;
    static const std::regex bpmRe("(\\d{2,3})\\s*[-_ ]?\\s*bpm", std::regex::icase);
    std::vector<std::string> files;
    for (fs::recursive_directory_iterator it(m_host.loopFolder, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string ext = lowerS(it->path().extension().string());
        if (ext == ".wav" || ext == ".mp3" || ext == ".flac" || ext == ".ogg" || ext == ".m4a" || ext == ".aif" || ext == ".aiff") files.push_back(it->path().string());
    }
    std::sort(files.begin(), files.end());
    for (const std::string& f : files) {
        LoopEntry e;
        e.path = f;
        std::string base = fs::path(f).stem().string();
        std::smatch m;
        if (std::regex_search(base, m, bpmRe)) { e.bpm = std::stod(m[1].str()); base = base.substr(0, size_t(m.position(0))); while (!base.empty() && base.back() == ' ') base.pop_back(); }
        e.name = base;
        m_loops.push_back(std::move(e));
    }
}

std::vector<Studio::BrowserItem> Studio::browserItems() {
    if (!m_loopsLoaded) reloadLoops();
    std::vector<BrowserItem> all;
    for (const LoopEntry& l : m_loops) all.push_back({0, "", l.name, l.bpm > 0 ? std::to_string(int(l.bpm)) + " BPM" : "", l.path, l.bpm});
    all.push_back({1, "synth", "Synth", "built in", "", 0});
    all.push_back({1, "drumkit", "Drum Kit", "built in", "", 0});
    const auto plugins = m_host.plugins ? m_host.plugins() : std::vector<PluginEntry>{};
    for (const PluginEntry& p : plugins) if (p.instrument) all.push_back({1, p.id, p.name, p.vendor, "", 0});
    for (const auto& e : m_effectNames) all.push_back({2, e.first, e.second, "built in", "", 0});
    for (const PluginEntry& p : plugins) if (!p.instrument) all.push_back({2, p.id, p.name, p.vendor, "", 0});
    if (m_browserTab >= 1 && m_browserTab <= 3) all.erase(std::remove_if(all.begin(), all.end(), [&](const BrowserItem& it) { return it.kind != m_browserTab - 1; }), all.end());
    const std::string q = lowerS(m_search);
    if (q.empty()) return all;
    std::vector<BrowserItem> out;
    for (const BrowserItem& it : all) if (lowerS(it.name).find(q) != std::string::npos || lowerS(it.sub).find(q) != std::string::npos) out.push_back(it);
    return out;
}

bool Studio::previewOnClick() const {
    auto it = m_host.settings.find("previewOnClick");
    return it == m_host.settings.end() || it->second != "0";
}

void Studio::drawBrowser(const Rect& r) {
    Gui& g = *m_g;
    DrawList& dl = *m_dl;
    dl.rect(r, bg1);
    dl.vline(r.right() - 1, r.y, r.bottom(), line);
    dl.text(r.x + 14, r.y + 10, tr("BROWSER"), textMute, {10, true});
    {
        bool pv = previewOnClick();
        if (g.checkbox(id("br-preview"), {r.right() - 108, r.y + 6, 98, 20}, "Preview", pv)) { m_host.settings["previewOnClick"] = pv ? "1" : "0"; if (m_host.applySettings) m_host.applySettings(m_host.settings); }
        g.tip(id("br-preview"), "Play a loop when you click it");
    }
    bool changed = false;
    g.textField(id("br-search"), {r.x + 12, r.y + 32, r.w - 24, 26}, m_search, "Search loops, instruments, effects", &changed);
    if (changed) m_browserScroll = 0;
    // FL Studio style tabs: what the browser lists
    {
        const char* names[] = {"All", "Loops", "Inst", "FX", "Project"};
        float x = r.x + 12;
        const float gap = 3, total = r.w - 24 - gap * 4;
        float wsum = 0, ws[5];
        for (int i = 0; i < 5; ++i) { ws[i] = dl.textWidth(tr(names[i]), small) + 12; wsum += ws[i]; }
        const float sc = std::min(1.35f, total / wsum);
        for (int i = 0; i < 5; ++i) {
            const Rect q{x, r.y + 64, ws[i] * sc, 20};
            bool on = m_browserTab == i;
            if (g.toggle(id("br-tab", i), q, tr(names[i]), on, accent)) { m_browserTab = i; m_browserScroll = 0; }
            x += q.w + gap;
        }
    }
    if (m_browserTab == 4) { drawProjectList(r); return; }
    const auto items = browserItems();
    const Rect list{r.x, r.y + 92, r.w - 1, r.h - 92};
    float contentH = 0;
    for (int k = 0; k < 3; ++k) { int n = 0; for (const BrowserItem& it : items) n += it.kind == k; if (n) contentH += 26 + (m_sectionClosed[k] ? 0 : float(n) * 24); }
    if (g.hovering(list) && g.wheelY() != 0) { m_browserScroll = std::clamp(m_browserScroll - g.wheelY() * 40.f, 0.f, std::max(0.f, contentH - list.h)); g.consumeWheel(); }
    dl.pushClip(list);
    float y = list.y - m_browserScroll;
    int last = -1;
    const char* const sections[3] = {"Loops", "Instruments", "Effects"};
    const Color kindColor[3] = {rgb(0x5b, 0x8c, 0xff), rgb(0x6f, 0xc3, 0x7a), rgb(0xe8, 0x9a, 0x3c)};
    for (int i = 0; i < int(items.size()); ++i) {
        const BrowserItem& it = items[size_t(i)];
        if (it.kind != last) {
            last = it.kind;
            int n = 0;
            for (const BrowserItem& o : items) n += o.kind == it.kind;
            const Rect hdr{r.x, y, r.w - 1, 24};
            const Id hid = id("br-sec", it.kind);
            const Interact hi = g.interact(hid, hdr, false);
            dl.rect(hdr, mix(bg2, bg3, g.anim(hid, hi.hovered ? 1.f : 0.f)));
            if (!icons::glyph(dl, hdr.x + 14, hdr.cy(), 18, text, m_sectionClosed[it.kind] ? Icon::ChevronRight : Icon::ExpandMore))
                dl.triangle(m_sectionClosed[it.kind] ? Vec2{hdr.x + 12, hdr.cy() - 4} : Vec2{hdr.x + 10, hdr.cy() - 2}, m_sectionClosed[it.kind] ? Vec2{hdr.x + 12, hdr.cy() + 4} : Vec2{hdr.x + 18, hdr.cy() - 2}, m_sectionClosed[it.kind] ? Vec2{hdr.x + 17, hdr.cy()} : Vec2{hdr.x + 14, hdr.cy() + 4}, textDim);
            dl.text(hdr.x + 26, hdr.y + 5, tr(sections[it.kind]) + "  (" + std::to_string(n) + ")", text, bold);
            if (hi.clicked) m_sectionClosed[it.kind] = !m_sectionClosed[it.kind];
            y += 26;
        }
        if (m_sectionClosed[it.kind]) continue;
        const Rect row{r.x + 6, y, r.w - 13, 22};
        y += 24;
        if (row.bottom() < list.y || row.y > list.bottom()) continue;
        const Id rid = id("br-item", i);
        const Interact it2 = g.interact(rid, row);
        const bool sel = i == m_browserSel;
        const float hv = g.anim(rid, it2.hovered ? 1.f : 0.f);
        if (sel || hv > 0.02f) dl.rrect(row, 5, sel ? accentDim : withAlpha(bg3, int(255 * hv)));
        dl.circle(row.x + 10, row.cy(), 4, kindColor[it.kind]);
        const float subW = it.sub.empty() ? 0.f : std::min(86.f, dl.textWidth(it.sub, small) + 8);
        dl.textIn({row.x + 22, row.y, row.w - 28 - subW, row.h}, it.name, text, normal);
        if (subW > 0) dl.textIn({row.right() - subW - 4, row.y, subW, row.h}, it.sub, it.kind == 0 ? warn : textMute, small, Align::Right);
        if (it2.pressed) {
            m_browserSel = i;
            m_payKind = it.kind; m_payId = it.id; m_payName = it.name; m_payPath = it.path;
            m_payX = g.mouseX(); m_payY = g.mouseY();
            if (it.kind == 0 && previewOnClick()) m_s.audition(it.path);
        }
        if (it2.held && !m_payActive && (std::fabs(g.mouseX() - it2.startX) > 5 || std::fabs(g.mouseY() - it2.startY) > 5)) m_payActive = true;
        if (m_payActive && it2.held) g.setCursor(Cursor::Move);
        if (it2.released) {
            if (m_payActive && m_rLanes.contains(g.mouseX(), g.mouseY())) dropPayload();
            m_payActive = false;
        }
        if (it2.doubleClicked) {
            if (it.kind == 0) { const bool audio = m_selTrack >= 0 && m_tracks[size_t(m_selTrack)].type == core::TrackType::Audio; m_s.importAudio(it.path, audio ? m_selTrack : -1, m_s.positionBeats()); message("Importing " + it.name); }
            else if (it.kind == 1) { const int n = m_s.addTrack(core::TrackType::Instrument, it.name); m_s.setInstrument(n, it.id); m_selTrack = n; }
            else if (m_selTrack >= 0) addInsertTo(m_selTrack, it.id);
            m_modelDirty = true;
        }
        if (it2.hovered) g.setCursor(Cursor::Hand);
    }
    if (items.empty()) dl.textIn({list.x, list.y + 20, list.w, 24}, tr("Nothing found."), textMute, normal, Align::Center);
    dl.popClip();
}

Color Studio::groupColor(int group) {
    static const Color cols[8] = {rgb(0xff, 0x7a, 0x7a), rgb(0xff, 0xb4, 0x5c), rgb(0xf2, 0xe0, 0x5a), rgb(0x7a, 0xe0, 0x8a),
                                  rgb(0x5c, 0xd6, 0xe6), rgb(0x6c, 0x9c, 0xff), rgb(0xb2, 0x8c, 0xff), rgb(0xff, 0x8c, 0xd4)};
    return cols[std::clamp(group, 1, 8) - 1];
}

std::vector<MenuItem> Studio::groupMenuItems(int ti) {
    std::vector<MenuItem> items;
    const int cur = ti >= 0 && ti < int(m_tracks.size()) ? m_tracks[size_t(ti)].group : 0;
    MenuItem none; none.label = "No Group"; none.checked = cur == 0;
    none.action = [this, ti] { m_s.setTrackGroup(ti, 0); m_modelDirty = true; };
    items.push_back(none);
    items.push_back(MenuItem::sep());
    for (int gnum = 1; gnum <= 8; ++gnum) {
        MenuItem m; m.label = std::string("Group ") + char('A' + gnum - 1); m.checked = cur == gnum;
        m.action = [this, ti, gnum] { m_s.setTrackGroup(ti, gnum); m_modelDirty = true; message(fmt("Track %1 is in this group now.", std::to_string(ti + 1))); };
        items.push_back(m);
    }
    return items;
}

}  // namespace vkui

namespace vkui {
using namespace theme;

// Pro Tools clip list: every region of the project, grouped by track; click selects it and moves the playhead there.
void Studio::drawProjectList(const Rect& r) {
    Gui& g = *m_g;
    DrawList& dl = *m_dl;
    const Rect list{r.x, r.y + 92, r.w - 1, r.h - 92};
    const std::string q = lowerS(m_search);
    struct Row { int track, region; };
    std::vector<Row> rows;
    float contentH = 0;
    for (int ti = 0; ti < int(m_tracks.size()); ++ti) {
        int n = 0;
        for (int ri = 0; ri < int(m_tracks[size_t(ti)].regions.size()); ++ri)
            if (q.empty() || lowerS(m_tracks[size_t(ti)].regions[size_t(ri)].name).find(q) != std::string::npos || lowerS(m_tracks[size_t(ti)].name).find(q) != std::string::npos) { rows.push_back({ti, ri}); ++n; }
        if (n) contentH += 26 + float(n) * 24;
    }
    if (g.hovering(list) && g.wheelY() != 0) { m_browserScroll = std::clamp(m_browserScroll - g.wheelY() * 40.f, 0.f, std::max(0.f, contentH - list.h)); g.consumeWheel(); }
    dl.pushClip(list);
    float y = list.y - m_browserScroll;
    int last = -1;
    const auto tm = m_tempo ? m_tempo : m_s.tempoMap();
    for (size_t k = 0; k < rows.size(); ++k) {
        const core::Track& t = m_tracks[size_t(rows[k].track)];
        const core::Region& rg = t.regions[size_t(rows[k].region)];
        const Color col = trackCol(t);
        if (rows[k].track != last) {
            last = rows[k].track;
            const Rect hdr{r.x, y, r.w - 1, 24};
            dl.rect(hdr, bg2);
            dl.rect({hdr.x, hdr.y, 4, hdr.h}, col);
            dl.text(hdr.x + 14, hdr.y + 5, std::to_string(last + 1) + "  " + t.name, text, bold);
            y += 26;
        }
        const Rect row{r.x + 6, y, r.w - 13, 22};
        y += 24;
        if (row.bottom() < list.y || row.y > list.bottom()) continue;
        const Id rid = id("br-clip", int(k));
        const Interact it = g.interact(rid, row, false);
        const bool sel = rows[k].track == m_selTrack && rows[k].region == m_selRegion;
        const float hv = g.anim(rid, it.hovered ? 1.f : 0.f);
        if (sel || hv > 0.02f) dl.rrect(row, 5, sel ? accentDim : withAlpha(bg3, int(255 * hv)));
        dl.rrect({row.x + 4, row.y + 5, 8, 12}, 2, rg.mute ? withAlpha(col, 90) : col);
        char at[24];
        std::snprintf(at, sizeof at, "bar %d", tm->barAt(rg.start));
        const float subW = dl.textWidth(at, small) + 8;
        dl.textIn({row.x + 20, row.y, row.w - 26 - subW, row.h}, rg.name.empty() ? std::string("Region") : rg.name, rg.mute ? textMute : text, normal);
        dl.textIn({row.right() - subW - 4, row.y, subW, row.h}, at, textMute, small, Align::Right);
        if (it.clicked) { m_selTrack = rows[k].track; m_selRegion = rows[k].region; m_s.seekBeats(rg.start); ensureVisible(rg.start); }
        if (it.doubleClicked) { m_selTrack = rows[k].track; m_selRegion = rows[k].region; zoomAt(m_rLanes.cx(), 1.0); m_scrollX = std::max(0.0, rg.start - 1.0); }
        if (it.hovered) g.setCursor(Cursor::Hand);
    }
    if (rows.empty()) dl.textIn({list.x, list.y + 20, list.w, 24}, tr("No regions yet."), textMute, normal, Align::Center);
    dl.popClip();
}

}  // namespace vkui
