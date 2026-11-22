// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Vulkan UI: the arrangement - ruler with cycle strip and markers, track headers, lanes with regions, tools, zoom.
#include <algorithm>
#include <cstdio>
#include "Icons.h"
#include "Studio.h"
#include "Theme.h"

namespace vkui {
using namespace theme;

namespace {
constexpr float kStripH = 12, kBarsH = 24, kRulerH = kStripH + kBarsH, kScroll = 10, kOverH = 18;
bool isDark(Color c) { return (((c >> 16) & 255) * 299 + ((c >> 8) & 255) * 587 + (c & 255) * 114) / 1000 < 150; }
}  // namespace

bool Studio::hasRegion() const { return m_selTrack >= 0 && m_selTrack < int(m_tracks.size()) && m_selRegion >= 0 && m_selRegion < int(m_tracks[size_t(m_selTrack)].regions.size()); }
const core::Region* Studio::region() const { return hasRegion() ? &m_tracks[size_t(m_selTrack)].regions[size_t(m_selRegion)] : nullptr; }
bool Studio::hasMidiRegion() const { const core::Region* r = region(); return r && r->midi; }
bool Studio::hasAudioRegion() const { const core::Region* r = region(); return r && !r->midi; }

int Studio::trackAtY(float y) const {
    const int i = int(std::floor((y - m_rLanes.y + m_scrollY) / m_trackH));
    return i >= 0 && i < int(m_tracks.size()) ? i : -1;
}

void Studio::ensureVisible(double beat) {
    const double view = double(m_rLanes.w) / m_ppb;
    if (beat < m_scrollX || beat > m_scrollX + view * 0.94) m_scrollX = std::max(0.0, beat - view * 0.08);
}

void Studio::zoomAt(double mx, double factor) {
    const double b = xToBeat(mx);
    m_ppb = std::clamp(m_ppb * factor, 3.0, 900.0);
    m_scrollX = std::max(0.0, b - (mx - double(m_rLanes.x)) / m_ppb);
}

void Studio::zoomToFit() {
    const double end = std::max(8.0, m_s.projectEndBeats());
    m_scrollX = 0;
    m_ppb = std::clamp(double(m_rLanes.w) / (end + 2.0), 3.0, 900.0);
    message("Zoomed to fit the project.");
}

void Studio::addTrack(core::TrackType type) {
    static const char* names[] = {"Audio", "Instrument", "Drum Machine", "Drummer"};
    m_selTrack = m_s.addTrack(type, names[int(type)]);
    m_selRegion = -1;
    m_modelDirty = true;
    m_scrollY = 1e9f;   // scroll to the new track
}

void Studio::rippleDelete() {   // Pro Tools Shuffle: the regions behind the deleted one close the gap
    const auto sel = selectionList();
    if (sel.empty()) return;
    const double len = m_s.rippleDeleteRegions(sel);
    setMulti({}, {m_selTrack, -1});
    message(fmt("Ripple delete: the following regions moved %1 beats earlier.", std::to_string(len)));
}

void Studio::applyWorkspace(int which) {   // Logic Pro screensets / Cubase workspaces: one key for the whole layout
    m_dockMax = false;
    m_showDock = true;
    if (which == 0) { m_showBrowser = true; m_showInspector = true; m_dock = DockTab::Mixer; m_dockH = 360; message("Workspace: Arrange"); }
    else if (which == 1) { m_showBrowser = false; m_showInspector = false; m_dock = DockTab::Mixer; m_dockH = 560; message("Workspace: Mix - the mixer gets most of the window"); }
    else { m_showBrowser = false; m_showInspector = true; m_dock = DockTab::Editor; m_dockH = 520; message("Workspace: Edit - the editor gets most of the window"); }
}

void Studio::setMulti(const std::vector<std::pair<int, int>>& list, std::pair<int, int> primary) {
    m_multi = list;
    m_selTrack = primary.first;
    m_selRegion = list.empty() ? -1 : primary.second;
    m_multiT = m_selTrack; m_multiR = m_selRegion;
}

bool Studio::regionSelected(int t, int r) const {
    if (t == m_selTrack && r == m_selRegion) return true;
    if (m_multi.empty() || m_selTrack != m_multiT || m_selRegion != m_multiR) return false;
    return std::find(m_multi.begin(), m_multi.end(), std::make_pair(t, r)) != m_multi.end();
}

std::vector<std::pair<int, int>> Studio::selectionList() const {
    if (!m_multi.empty() && m_selTrack == m_multiT && m_selRegion == m_multiR) return m_multi;
    if (hasRegion()) return {{m_selTrack, m_selRegion}};
    return {};
}

void Studio::deleteSelectedRegion() {
    const auto sel = selectionList();
    if (sel.empty()) return;
    m_s.removeRegions(sel);
    setMulti({}, {m_selTrack, -1});
}

void Studio::splitAtPlayhead() {
    const double pos = m_s.positionBeats();
    if (hasRegion()) { m_s.splitRegion(m_selTrack, m_selRegion, pos); return; }
    if (m_selTrack < 0) return;
    const auto& regs = m_tracks[size_t(m_selTrack)].regions;
    for (int i = int(regs.size()) - 1; i >= 0; --i)
        if (pos > regs[size_t(i)].start + 0.01 && pos < regs[size_t(i)].start + m_s.regionBeats(regs[size_t(i)]) - 0.01) { m_s.splitRegion(m_selTrack, i, pos); return; }
    message("Put the playhead inside a region to split it.");
}

void Studio::tabTransient(bool forward) {
    int t = m_selTrack, r = m_selRegion;
    const double pos = m_s.positionBeats();
    if (!hasAudioRegion()) {   // like Pro Tools: use the audio region under the playhead on the selected track
        r = -1;
        if (t >= 0)
            for (int i = 0; i < int(m_tracks[size_t(t)].regions.size()); ++i) {
                const auto& rg = m_tracks[size_t(t)].regions[size_t(i)];
                if (!rg.midi && pos >= rg.start - 1e-6 && pos <= rg.start + m_s.regionBeats(rg) + 1e-6) { r = i; break; }
            }
    }
    if (t < 0 || r < 0) { message("Select an audio region (or put the playhead inside one) to tab to its transients."); return; }
    const double beat = m_s.nextTransient(t, r, pos, forward);
    if (beat < 0) { message(forward ? "No further transient in this region." : "No earlier transient in this region."); return; }
    m_selTrack = t; m_selRegion = r;
    m_s.seekBeats(beat);
    ensureVisible(beat);
}

bool Studio::hitRegion(float mx, float my, int& track, int& region, bool& rightEdge) const {
    track = trackAtY(my);
    region = -1;
    rightEdge = false;
    if (track < 0 || !m_rLanes.contains(mx, my)) return false;
    const auto& regs = m_tracks[size_t(track)].regions;
    for (int i = int(regs.size()) - 1; i >= 0; --i) {
        const double x0 = beatToX(regs[size_t(i)].start), x1 = beatToX(regs[size_t(i)].start + m_s.regionBeats(regs[size_t(i)]));
        if (mx >= x0 && mx <= x1) {
            region = i;
            rightEdge = mx >= x1 - 7 && x1 - x0 > 16;
            return true;
        }
    }
    return false;
}

// ------------------------------------------------------------------------------------------------------------ drawing

void Studio::drawArrangement() {
    Gui& g = *m_g;
    DrawList& dl = *m_dl;
    const Rect arr = m_rArr;
    const float hScroll = kScroll, vScroll = kScroll;
    m_rOverview = {arr.x, arr.y, arr.w, kOverH};
    m_rRuler = {arr.x + m_headerW, arr.y + kOverH, arr.w - m_headerW, kRulerH};
    m_rHeaders = {arr.x, arr.y + kOverH + kRulerH, m_headerW, arr.h - kOverH - kRulerH - hScroll};
    m_rLanes = {arr.x + m_headerW, arr.y + kOverH + kRulerH, arr.w - m_headerW - vScroll, arr.h - kOverH - kRulerH - hScroll};
    dl.rect(arr, bg1);

    // wheel: scroll / zoom / track height
    if (g.hovering(arr) && (g.wheelY() != 0 || g.wheelX() != 0)) {
        if (g.ctrl()) { zoomAt(g.mouseX(), g.wheelY() > 0 ? 1.18 : 1 / 1.18); }
        else if (g.alt()) { m_trackH = std::clamp(m_trackH + g.wheelY() * 8.f, 34.f, 220.f); }
        else if (g.shift() || g.wheelX() != 0) { m_scrollX = std::max(0.0, m_scrollX - double(g.shift() ? g.wheelY() : 0.f + g.wheelX()) * 80.0 / m_ppb); }
        else m_scrollY -= g.wheelY() * 48.f;
        g.consumeWheel();
    }
    const float contentH = float(m_tracks.size()) * m_trackH + 54.f;
    m_scrollY = std::clamp(m_scrollY, 0.f, std::max(0.f, contentH - m_rLanes.h));
    m_scrollX = std::max(0.0, m_scrollX);
    if (m_s.isPlaying() && m_follow && m_drag == Drag::None) ensureVisible(m_s.positionBeats());

    drawLanes();
    drawTrackHeaders();
    drawRuler();
    drawOverview();
    // corner above the headers: track count and add-track button
    {
        const Rect corner{arr.x, arr.y + kOverH, m_headerW, kRulerH};
        dl.rect(corner, bg2);
        dl.hline(corner.x, corner.right(), corner.bottom() - 1, line);
        dl.text(corner.x + 12, corner.y + 11, tr("Tracks"), textDim, bold);
        const Rect add{corner.right() - 86, corner.y + 7, 76, 22};
        if (g.button(id("add-track"), add, "+ Track", ButtonKind::Accent)) {
            std::vector<MenuItem> items;
            for (const char* l : {"New Audio Track", "New Software Instrument Track", "New Drum Machine Track", "New Drummer Track"}) items.push_back(itemsFor({l})[0]);
            g.openMenu(add.x, add.bottom() + 2, std::move(items));
        }
    }
    // scroll bars
    {
        float sx = float(m_scrollX);
        const float viewBeats = m_rLanes.w / float(m_ppb);
        const float contentBeats = std::max(float(m_s.projectEndBeats()) + 32.f, sx + viewBeats);
        g.scrollBar(id("hscroll"), {m_rLanes.x, arr.bottom() - hScroll, m_rLanes.w, hScroll}, sx, contentBeats, viewBeats, false);
        m_scrollX = sx;
        g.scrollBar(id("vscroll"), {arr.right() - vScroll, m_rLanes.y, vScroll, m_rLanes.h}, m_scrollY, contentH, m_rLanes.h, true);
    }
    laneInteraction();
}

void Studio::drawRuler() {
    Gui& g = *m_g;
    DrawList& dl = *m_dl;
    const Rect r = m_rRuler;
    dl.rect(r, bg2);
    dl.hline(r.x, r.right(), r.bottom() - 1, line);
    const auto tm = m_tempo ? m_tempo : m_s.tempoMap();
    dl.pushClip(r);
    // cycle strip
    const Rect strip{r.x, r.y, r.w, kStripH};
    dl.rect(strip, bg0);
    if (m_s.loopA() >= 0 && m_s.loopB() > m_s.loopA()) {
        const float x0 = float(beatToX(m_s.loopA())), x1 = float(beatToX(m_s.loopB()));
        dl.rrect({x0, strip.y + 1, std::max(2.f, x1 - x0), strip.h - 2}, 3, m_s.loopOn() ? withAlpha(loop, 200) : withAlpha(textMute, 120));
        if (x1 - x0 > 40) dl.textIn({x0, strip.y, x1 - x0, strip.h}, "CYCLE", m_s.loopOn() ? rgb(0x30, 0x20, 0x00) : textDim, {9, true}, Align::Center);
    }
    // bars and beats
    const double viewBeats = double(r.w) / m_ppb;
    const int bar0 = tm->barAt(m_scrollX), bar1 = tm->barAt(m_scrollX + viewBeats) + 1;
    const double barPx = tm->barLength(bar0) * m_ppb;
    int step = 1;
    while (barPx * step < 44) step *= 2;
    for (int bar = std::max(1, bar0); bar <= bar1; ++bar) {
        const double b = tm->barStart(bar);
        const float x = float(beatToX(b));
        if ((bar - 1) % step == 0) {
            dl.vline(x, r.y + kStripH + 4, r.bottom() - 1, textMute);
            dl.text(x + 4, r.y + kStripH + 3, std::to_string(bar), textDim, small);
        }
        if (m_ppb >= 14) {
            const double unit = 4.0 / std::max(1, tm->meterAtBeat(b).den);
            const double len = tm->barLength(bar);
            for (double k = unit; k < len - 1e-6; k += unit) dl.vline(float(beatToX(b + k)), r.bottom() - 7, r.bottom() - 1, withAlpha(textMute, 150));
        }
    }
    // markers
    for (size_t i = 0; i < m_markers.size(); ++i) {
        const float x = float(beatToX(m_markers[i].beat));
        if (x < r.x - 100 || x > r.right()) continue;
        const std::string name = m_markers[i].name.empty() ? "Marker " + std::to_string(i + 1) : m_markers[i].name;
        const float w = dl.textWidth(name, small) + 12;
        dl.triangle({x, r.bottom() - 2}, {x - 3, r.bottom() - 7}, {x + 3, r.bottom() - 7}, warn);
        dl.rrect({x + 4, r.y + kStripH + 1, w, 13}, 3, withAlpha(warn, 60));
        dl.textIn({x + 4, r.y + kStripH + 1, w, 13}, name, warn, small, Align::Center);
    }
    dl.popClip();

    // interaction
    const Id rid = id("ruler");
    const Interact it = g.interact(rid, r);
    const double mouseBeat = xToBeat(g.mouseX());
    if (it.pressed) {
        m_dragMoved = false;
        const bool inStrip = g.mouseY() < r.y + kStripH;
        int hitMarker = -1;
        for (size_t i = 0; i < m_markers.size(); ++i)
            if (std::fabs(beatToX(m_markers[i].beat) - g.mouseX()) < 7 && g.mouseY() > r.bottom() - 16) hitMarker = int(i);
        if (hitMarker >= 0) { m_drag = Drag::Marker; m_dragTrack = hitMarker; m_s.seekBeats(m_markers[size_t(hitMarker)].beat); }
        else if (inStrip) { m_drag = Drag::Cycle; m_dragStart = snapBeat(mouseBeat); m_dragLen = m_dragStart; }
        else { m_drag = Drag::Seek; m_s.seekBeats(snapBeat(mouseBeat)); }
    }
    if (it.held) {
        if (m_drag == Drag::Seek) m_s.seekBeats(snapBeat(std::max(0.0, mouseBeat)));
        else if (m_drag == Drag::Cycle) {
            const double b = snapBeat(mouseBeat);
            if (std::fabs(b - m_dragStart) > 1e-6) { m_dragMoved = true; m_s.setLoop(true, std::min(m_dragStart, b), std::max(m_dragStart, b)); }
        } else if (m_drag == Drag::Marker && m_dragTrack >= 0 && m_dragTrack < int(m_markers.size())) {
            if (!m_dragMoved && std::fabs(g.mouseX() - it.startX) > 3) m_dragMoved = true;
            if (m_dragMoved) m_dragTrack = m_s.moveMarker(m_dragTrack, snapBeat(std::max(0.0, mouseBeat)));
        }
        g.setCursor(Cursor::Hand);
    }
    if (it.released && m_drag == Drag::Cycle && !m_dragMoved) m_s.setLoop(!m_s.loopOn());   // a click toggles cycle
    if (it.released) { m_drag = Drag::None; m_modelDirty = true; }
    if (it.rightClicked) {
        int hitMarker = -1;
        for (size_t i = 0; i < m_markers.size(); ++i) if (std::fabs(beatToX(m_markers[i].beat) - g.mouseX()) < 7) hitMarker = int(i);
        std::vector<MenuItem> items;
        if (hitMarker >= 0) {
            const int mi = hitMarker;
            MenuItem a; a.label = "Rename Marker..."; a.action = [this, mi] { m_snapshotRename = -1; m_renameTrack = -2 - mi; m_renameText = m_markers[size_t(mi)].name; openDialog("Rename Marker", {DlgField::input("Name", m_markers[size_t(mi)].name)}, [this, mi](const std::vector<DlgField>& f) { m_s.renameMarker(mi, f[0].text); }); };
            MenuItem b; b.label = "Delete Marker"; b.action = [this, mi] { m_s.removeMarker(mi); };
            items = {a, b};
        } else {
            const double beat = snapBeat(mouseBeat);
            MenuItem a; a.label = "Add Marker Here"; a.action = [this, beat] { m_s.addMarker(beat); };
            MenuItem b; b.label = "Set Cycle to One Bar Here"; b.action = [this, beat] { const auto tm2 = m_s.tempoMap(); const int bar = tm2->barAt(beat); m_s.setLoop(true, tm2->barStart(bar), tm2->barStart(bar) + tm2->barLength(bar)); };
            MenuItem c; c.label = "Cycle On / Off"; c.action = [this] { m_s.setLoop(!m_s.loopOn()); };
            items = {a, b, c};
        }
        g.openMenu(g.mouseX(), g.mouseY(), std::move(items));
    }
    // playhead flag
    const float px = float(beatToX(m_s.positionBeats()));
    if (px >= r.x - 6 && px <= r.right() + 6) {
        dl.pushClip(r);
        dl.triangle({px - 6, r.y + kStripH}, {px + 6, r.y + kStripH}, {px, r.y + kStripH + 8}, playhead);
        dl.pushClip(r);
        dl.popClip();
        dl.popClip();
    }
}

void Studio::drawTrackHeaders() {
    Gui& g = *m_g;
    DrawList& dl = *m_dl;
    dl.pushClip(m_rHeaders);
    dl.rect(m_rHeaders, bg1);
    for (int i = 0; i < int(m_tracks.size()); ++i) {
        const core::Track& t = m_tracks[size_t(i)];
        const float y = laneY(i);
        if (y + m_trackH < m_rHeaders.y || y > m_rHeaders.bottom()) continue;
        const Rect h{m_rHeaders.x, y, m_headerW, m_trackH};
        const Color col = Color(0xFF000000u) | (Color(t.color.r) << 16) | (Color(t.color.g) << 8) | Color(t.color.b);
        const bool sel = i == m_selTrack;
        const Id hid = g.id("th", t.id);
        const Interact it = g.interact(hid, h, false);
        const float hv = g.anim(hid, it.hovered ? 1.f : 0.f);
        dl.rect(h, mix(sel ? bg3 : bg2, bg3, hv * 0.5f));
        dl.rect({h.x, h.y, 5, h.h}, col);
        dl.hline(h.x, h.right(), h.bottom() - 1, line);
        if (it.pressed) { m_selTrack = i; m_selRegion = -1; }
        if (it.doubleClicked) dlgRenameTrack(i);
        if (it.rightClicked) { m_selTrack = i; trackHeaderMenu(i); }
        // drag a header up or down to reorder the tracks
        if (it.held && std::fabs(g.mouseY() - it.startY) > 6) {
            const int target = std::clamp(int(std::floor((g.mouseY() - m_rLanes.y + m_scrollY) / m_trackH)), 0, int(m_tracks.size()) - 1);
            if (target != i) { m_s.moveTrack(i, target); m_selTrack = target; m_modelDirty = true; }
            g.setCursor(Cursor::Move);
        }
        const Rect inner = h.inset(10, 4);
        // row 1: type dot, name, R M S
        const float bw = 22;
        Rect row1{inner.x, inner.y, inner.w, 20};
        Rect sbtn = row1.takeRight(bw); row1.w -= 3;
        Rect mbtn = row1.takeRight(bw); row1.w -= 3;
        Rect rbtn = row1.takeRight(bw); row1.w -= 6;
        {   // Cubase / Pro Tools: the track number, in a chip of the track colour
            const Rect chip{row1.x, row1.y + 2, 18, row1.h - 4};
            dl.rrect(chip, 4, mix(bg3, col, 0.55f));
            dl.textIn(chip, std::to_string(i + 1), rgb(255, 255, 255), {10, true}, Align::Center);
        }
        dl.textIn({row1.x + 24, row1.y, row1.w - 24, row1.h}, t.name, text, bold);
        bool arm = t.armed, mu = t.mute, so = t.solo;
        if (g.toggle(g.id("arm", t.id), rbtn, "R", arm, record)) m_s.setArmed(i, arm);
        if (g.toggle(g.id("mute", t.id), mbtn, "M", mu, mute)) m_s.setMute(i, mu);
        if (g.toggle(g.id("solo", t.id), sbtn, "S", so, solo)) m_s.setSolo(i, so);
        g.tip(g.id("arm", t.id), "Record enable (Shift+R)");
        g.tip(g.id("mute", t.id), "Mute (Shift+M)");
        g.tip(g.id("solo", t.id), "Solo (Shift+S)");
        // row 2: volume and pan sliders (not on the compact height)
        if (m_trackH >= 58) {
            float vol = t.volume, pan = t.pan;
            const Rect r2{inner.x, inner.y + 26, inner.w, 18};
            if (g.slider(g.id("vol", t.id), {r2.x, r2.y, r2.w * 0.62f, r2.h}, vol, 0.f, 1.5f, 0.8f)) m_s.setVolume(i, vol);
            if (g.slider(g.id("pan", t.id), {r2.x + r2.w * 0.66f, r2.y, r2.w * 0.34f, r2.h}, pan, -1.f, 1.f, 0.f)) m_s.setPan(i, pan);
            g.tip(g.id("vol", t.id), "Volume - double-click to reset");
            g.tip(g.id("pan", t.id), "Pan - double-click to centre");
        }
        // meter
        const float peak = std::max(t.peakL, t.peakR);
        if (peak > 0.002f) {
            const float db = 20.f * std::log10(peak), tt = std::clamp((db + 54.f) / 54.f, 0.f, 1.f);
            dl.rect({h.x + 5, h.bottom() - 4, (h.w - 5) * tt, 3}, tt > 0.9f ? meterHigh : tt > 0.75f ? meterMid : meterLow);
        }
        if (m_trackH >= 100) {
            const char* tn[] = {"Audio", "Software Instrument", "Drum Machine", "Drummer"};
            dl.textIn({inner.x, inner.y + 48, inner.w, 14}, tr(tn[int(t.type)]), textMute, small);
        }
    }
    // empty area below the tracks
    const float endY = laneY(int(m_tracks.size()));
    if (endY < m_rHeaders.bottom()) {
        const Rect add{m_rHeaders.x + 10, endY + 10, m_headerW - 20, 28};
        if (g.button(g.id("add-track-bottom"), add, "+ Add Track", ButtonKind::Flat)) {
            std::vector<MenuItem> items;
            for (const char* l : {"New Audio Track", "New Software Instrument Track", "New Drum Machine Track", "New Drummer Track"}) items.push_back(itemsFor({l})[0]);
            g.openMenu(add.x, add.bottom() + 2, std::move(items));
        }
    }
    dl.popClip();
    dl.vline(m_rHeaders.right() - 1, m_rHeaders.y, m_rHeaders.bottom(), line);
}

void Studio::drawLanes() {
    Gui& g = *m_g;
    DrawList& dl = *m_dl;
    const Rect L = m_rLanes;
    dl.pushClip(L);
    const auto tm = m_tempo ? m_tempo : m_s.tempoMap();
    for (int i = 0; i < int(m_tracks.size()) + 6; ++i) {
        const float y = laneY(i);
        if (y > L.bottom()) break;
        if (y + m_trackH < L.y) continue;
        dl.rect({L.x, y, L.w, m_trackH}, i == m_selTrack ? rgb(0x20, 0x24, 0x2e) : (i % 2 ? rgb(0x1d, 0x20, 0x28) : rgb(0x1a, 0x1d, 0x24)));
        dl.hline(L.x, L.right(), y + m_trackH - 1, line);
    }
    // grid
    const double viewBeats = double(L.w) / m_ppb;
    const int bar0 = std::max(1, tm->barAt(m_scrollX)), bar1 = tm->barAt(m_scrollX + viewBeats) + 1;
    const double g1 = gridBeats();
    for (int bar = bar0; bar <= bar1; ++bar) {
        const double b = tm->barStart(bar), len = tm->barLength(bar);
        dl.vline(float(beatToX(b)), L.y, L.bottom(), withAlpha(rgb(0x0b, 0x0d, 0x10), 230));
        if (m_ppb >= 14) {
            const double unit = 4.0 / std::max(1, tm->meterAtBeat(b).den);
            for (double k = unit; k < len - 1e-6; k += unit) dl.vline(float(beatToX(b + k)), L.y, L.bottom(), withAlpha(rgb(0x0b, 0x0d, 0x10), 110));
        }
        if (m_snapOn && g1 < 1.0 && g1 * m_ppb >= 10)
            for (double k = g1; k < len - 1e-6; k += g1) if (std::fabs(k - std::round(k)) > 1e-6) dl.vline(float(beatToX(b + k)), L.y, L.bottom(), withAlpha(rgb(0x0b, 0x0d, 0x10), 50));
    }
    // cycle range
    if (m_s.loopA() >= 0 && m_s.loopB() > m_s.loopA() && m_s.loopOn()) {
        const float x0 = float(beatToX(m_s.loopA())), x1 = float(beatToX(m_s.loopB()));
        dl.rect({x0, L.y, x1 - x0, L.h}, withAlpha(loop, 18));
        dl.vline(x0, L.y, L.bottom(), withAlpha(loop, 160));
        dl.vline(x1, L.y, L.bottom(), withAlpha(loop, 160));
    }
    // regions
    for (int ti = 0; ti < int(m_tracks.size()); ++ti) {
        const float y = laneY(ti);
        if (y + m_trackH < L.y || y > L.bottom()) continue;
        const core::Track& t = m_tracks[size_t(ti)];
        const Rect lane{L.x, y + 3, L.w, m_trackH - 7};
        for (int ri = 0; ri < int(t.regions.size()); ++ri) {
            const double x0 = beatToX(t.regions[size_t(ri)].start), x1 = beatToX(t.regions[size_t(ri)].start + m_s.regionBeats(t.regions[size_t(ri)]));
            if (x1 < L.x - 2 || x0 > L.right() + 2) continue;
            drawRegion(t, ti, ri, lane, regionSelected(ti, ri), ti == m_hoverTrack && ri == m_hoverRegion);
        }
    }
    // rubber band
    if (m_drag == Drag::Rubber) {
        const Rect rb{std::min(m_rubX0, m_rubX1), std::min(m_rubY0, m_rubY1), std::fabs(m_rubX1 - m_rubX0), std::fabs(m_rubY1 - m_rubY0)};
        dl.rect(rb, withAlpha(accent, 36));
        dl.rrectStroke(rb, 2, 1.2f, accent);
    }
    // pencil preview
    if (m_drag == Drag::Create && m_dragTrack >= 0) {
        const float y = laneY(m_dragTrack) + 3;
        const float x0 = float(beatToX(std::min(m_createStart, m_createEnd))), x1 = float(beatToX(std::max(m_createStart, m_createEnd)));
        dl.rrect({x0, y, std::max(4.f, x1 - x0), m_trackH - 7}, 4, withAlpha(accent, 90));
        dl.rrectStroke({x0, y, std::max(4.f, x1 - x0), m_trackH - 7}, 4, 1.5f, accent);
    }
    // scissors preview
    if (m_tool == Tool::Scissors && m_hoverRegion >= 0 && m_drag == Drag::None) {
        const float x = float(beatToX(snapBeat(m_hoverBeat)));
        dl.line(x, laneY(m_hoverTrack) + 2, x, laneY(m_hoverTrack) + m_trackH - 3, 1.5f, rgb(255, 255, 255));
    }
    // drop target of the browser drag
    if (m_payActive && g.hovering(L)) {
        const int t = trackAtY(g.mouseY());
        if (t >= 0) dl.rrectStroke({L.x + 2, laneY(t) + 2, L.w - 4, m_trackH - 5}, 5, 2, accent);
        else dl.rrectStroke({L.x + 2, laneY(int(m_tracks.size())) + 2, L.w - 4, 40}, 5, 2, withAlpha(accent, 160));
    }
    // playhead
    const float px = float(beatToX(m_s.positionBeats()));
    if (px >= L.x && px <= L.right()) {
        dl.vline(px, L.y, L.bottom(), withAlpha(playhead, 230));
        if (m_s.isPlaying()) dl.rect({px - 1, L.y, 3, L.h}, withAlpha(playhead, 24));
    }
    if (m_tracks.empty()) {
        dl.textIn(L, tr("Add a track to begin: Ctrl+Alt+A audio, Ctrl+Alt+S instrument - or drop an audio file here"), textMute, normal, Align::Center);
    }
    dl.popClip();
}

void Studio::drawWaveform(const core::Region& r, const Rect& body, Color c, double beats) {
    if (!r.audio || r.audio->peaks.empty() || r.frames <= 0) return;
    DrawList& dl = *m_dl;
    const float x0 = float(beatToX(r.start)), w = float(beats * m_ppb);
    const float from = std::max(body.x, x0), to = std::min(body.right(), x0 + w);
    const float cy = body.cy(), amp = body.h * 0.5f - 1;
    const auto& peaks = r.audio->peaks;
    const float step = std::max(1.f, 1.0f);
    const double framesPerDp = double(r.frames) / std::max(1.0, double(w));
    for (float x = from; x < to; x += step) {
        const double f0 = double(r.offset) + double(x - x0) * framesPerDp, f1 = f0 + double(step) * framesPerDp;
        size_t i0 = size_t(std::max(0.0, f0) / ::kPeakStride), i1 = size_t(std::max(0.0, f1) / ::kPeakStride);
        if (i0 >= peaks.size()) break;
        i1 = std::min(i1, peaks.size() - 1);
        float pk = 0;
        for (size_t i = i0; i <= i1; ++i) pk = std::max(pk, peaks[i]);
        const float hgt = std::min(amp, std::pow(std::min(pk, 1.f), 0.8f) * amp * (std::pow(10.f, r.gainDb / 20.f)));
        if (hgt > 0.3f) dl.rect({x, cy - hgt, step, std::max(1.f, hgt * 2)}, c);
    }
}

void Studio::drawRegion(const core::Track& t, int ti, int ri, const Rect& lane, bool selected, bool hovered) {
    DrawList& dl = *m_dl;
    const core::Region& r = t.regions[size_t(ri)];
    const double beats = m_s.regionBeats(r);
    const float x0 = float(beatToX(r.start)), x1 = float(beatToX(r.start + beats));
    Rect body{x0, lane.y, std::max(3.f, x1 - x0 - 1), lane.h};
    const Color col = Color(0xFF000000u) | (Color(t.color.r) << 16) | (Color(t.color.g) << 8) | Color(t.color.b);
    const float dim = r.mute ? 0.35f : 1.f;
    Color fill = mix(bg1, col, selected ? 0.55f : (hovered ? 0.42f : 0.34f));
    if (r.mute) fill = mix(fill, bg1, 0.5f);
    (void)ti;
    dl.rrect(body, 5, fill);
    const float hh = std::min(16.f, body.h * 0.4f);
    const Rect head{body.x, body.y, body.w, hh};
    dl.pushClip(body);
    dl.rect(head, scaleAlpha(mix(col, bg0, selected ? 0.1f : 0.28f), dim));
    dl.pushClip(head.intersect({std::max(body.x, m_rLanes.x), head.y, head.w, head.h}));
    const float tx = std::max(body.x, m_rLanes.x) + 5;
    dl.text(tx, head.y + (hh - dl.lineHeight(small)) * 0.5f, r.name.empty() ? (r.midi ? "MIDI Region" : "Audio") : r.name, isDark(col) ? rgb(0xf0, 0xf4, 0xfa) : rgb(0x12, 0x16, 0x1c), small);
    dl.popClip();
    Rect inner{body.x, body.y + hh, body.w, body.h - hh};
    if (inner.h > 8) {
        const Color wave = scaleAlpha(lighten(col, 0.45f), dim);
        if (!r.midi) drawWaveform(r, inner.inset(0, 2), wave, beats);
        else if (!r.notes.empty()) {
            int lo = 127, hi = 0;
            for (const core::Note& n : r.notes) { lo = std::min(lo, n.pitch); hi = std::max(hi, n.pitch); }
            if (hi - lo < 11) { const int mid = (hi + lo) / 2; lo = mid - 6; hi = mid + 6; }
            const float rowH = std::min(6.f, (inner.h - 6) / float(hi - lo + 1));
            for (const core::Note& n : r.notes) {
                const float nx = float(beatToX(r.start + n.start)), nw = std::max(2.f, float(n.length * m_ppb) - 1);
                if (nx > m_rLanes.right() || nx + nw < m_rLanes.x) continue;
                const float ny = inner.bottom() - 3 - float(n.pitch - lo + 1) * rowH;
                dl.rect({nx, ny, nw, std::max(1.5f, rowH - 0.5f)}, wave);
            }
        } else if (r.drummer) dl.textIn(inner, tr("Drummer"), scaleAlpha(lighten(col, 0.5f), dim), small, Align::Center);
    }
    dl.popClip();
    if (selected) dl.rrectStroke(body, 5, 1.6f, rgb(255, 255, 255));
    else dl.rrectStroke(body, 5, 1, withAlpha(rgb(0, 0, 0), 150));
    if (r.mute) dl.textIn(body, "MUTED", withAlpha(text, 110), {10, true}, Align::Center);
}

// ------------------------------------------------------------------------------------------------------------ interaction

void Studio::laneInteraction() {
    Gui& g = *m_g;
    const Id lid = id("lanes");
    const Interact it = g.interact(lid, m_rLanes);
    // middle mouse button: drag the view like a hand (Ableton Live, Logic Pro)
    {
        const bool mid = g.mouseDown(2);
        if (mid && !m_midPrev && g.hovering(m_rLanes)) m_panning = true;
        if (!mid) m_panning = false;
        m_midPrev = mid;
        if (m_panning) {
            g.setCursor(Cursor::Hand);
            m_scrollX = std::max(0.0, m_scrollX - double(g.mouseX() - m_panX) / m_ppb);
            m_scrollY = std::max(0.f, m_scrollY - (g.mouseY() - m_panY));
        }
        m_panX = g.mouseX(); m_panY = g.mouseY();
    }
    // dragging regions or a rubber band against the edge of the lanes scrolls the view
    m_autoScrolling = false;
    if (g.mouseDown(0) && (m_drag == Drag::MoveRegion || m_drag == Drag::Rubber || m_drag == Drag::Create)) {
        const float edgeZone = 36.f;
        auto frac = [&](float over) { return std::clamp(over / edgeZone, 0.f, 2.f); };
        const float fr = frac(g.mouseX() - (m_rLanes.right() - edgeZone));
        const float fl = m_scrollX > 0 ? frac((m_rLanes.x + edgeZone) - g.mouseX()) : 0.f;
        const float fd = m_drag == Drag::Create ? 0.f : frac(g.mouseY() - (m_rLanes.bottom() - edgeZone));
        const float fu = m_drag == Drag::Create || m_scrollY <= 0 ? 0.f : frac((m_rLanes.y + edgeZone) - g.mouseY());
        const float speed = 900.f * std::min(g.dt(), 0.05f);   // pixels per frame at full speed
        const float dxPx = (fr - fl) * speed, dyPx = (fd - fu) * speed;
        if (dxPx != 0.f || dyPx != 0.f) {
            m_autoScrolling = true;
            const double before = m_scrollX;
            m_scrollX = std::clamp(m_scrollX + double(dxPx) / m_ppb, 0.0, std::max(0.0, m_s.projectEndBeats() + 64.0));
            const float movedX = float((m_scrollX - before) * m_ppb);
            const float before_y = m_scrollY;
            m_scrollY = std::max(0.f, m_scrollY + dyPx);
            const float movedY = m_scrollY - before_y;   // the upper limit is applied by the layout on the next frame
            if (m_drag == Drag::MoveRegion) m_dragOrigX -= movedX;   // the regions follow the mouse, not the screen
            if (m_drag == Drag::Rubber) { m_rubX0 -= movedX; m_rubY0 -= movedY; }
        }
    }
    const double mouseBeat = xToBeat(g.mouseX());
    int ht = -1, hr = -1;
    bool edge = false;
    hitRegion(g.mouseX(), g.mouseY(), ht, hr, edge);
    if (m_drag == Drag::None) {
        m_hoverTrack = ht; m_hoverRegion = hr; m_hoverBeat = mouseBeat;
        if (it.hovered) {
            Cursor c = Cursor::Arrow;
            switch (m_tool) {
            case Tool::Pointer: c = hr >= 0 ? (edge ? Cursor::ResizeH : Cursor::Hand) : Cursor::Arrow; break;
            case Tool::Pencil: c = Cursor::Pencil; break;
            case Tool::Scissors: c = Cursor::Scissors; break;
            case Tool::Eraser: c = Cursor::Eraser; break;
            case Tool::Mute: c = Cursor::Hand; break;
            }
            g.setCursor(c);
            if (hr >= 0) setHint(m_tool == Tool::Pointer ? "Drag to move - drag the right edge to resize - Alt+drag duplicates - double-click opens the editor - right-click for more"
                                 : m_tool == Tool::Scissors ? "Click to split the region here" : m_tool == Tool::Eraser ? "Click to delete the region" : m_tool == Tool::Mute ? "Click to mute / unmute the region" : "Draw MIDI regions on instrument tracks");
            else if (ht >= 0) setHint("Double-click an instrument lane to create a MIDI region - right-click for more");
        }
    }
    const bool instrumentLane = ht >= 0 && (m_tracks[size_t(ht)].type == core::TrackType::Instrument || m_tracks[size_t(ht)].type == core::TrackType::Drummer);

    if (it.pressed) {
        m_dragMoved = false;
        m_dragMouseX = g.mouseX(); m_dragMouseY = g.mouseY();
        if (ht >= 0) m_selTrack = ht;
        switch (m_tool) {
        case Tool::Pointer:
            if (hr >= 0) {
                const bool additive = g.shift() || g.ctrl();   // Shift / Ctrl+click adds or removes a region (Logic Pro, Cubase, Pro Tools)
                const std::pair<int, int> me{ht, hr};
                if (additive && !it.doubleClicked) {
                    auto cur = selectionList();
                    const auto f = std::find(cur.begin(), cur.end(), me);
                    if (f != cur.end()) cur.erase(f); else cur.push_back(me);
                    setMulti(cur, cur.empty() ? std::make_pair(ht, -1) : cur.back());
                    m_drag = Drag::None;
                    break;
                }
                if (regionSelected(ht, hr)) { if (!m_multi.empty() && m_selTrack == m_multiT && m_selRegion == m_multiR) setMulti(m_multi, me); else { m_selTrack = ht; m_selRegion = hr; } }
                else setMulti({me}, me);
                const core::Region& rg = m_tracks[size_t(ht)].regions[size_t(hr)];
                m_dragTrack = ht; m_dragRegion = hr; m_dragStart = rg.start; m_dragLen = m_s.regionBeats(rg); m_dragOrigX = g.mouseX();
                m_dragCurTrack = ht; m_dragCurStart = rg.start;
                if (it.doubleClicked) { if (rg.midi) { m_dock = rg.drummer ? DockTab::Drummer : DockTab::Editor; m_showDock = true; m_dockH = std::max(m_dockH, 430.f); } else { message("Audio region: use the Inspector for gain, fades and Follow Tempo."); } m_drag = Drag::None; break; }
                if (edge) m_drag = Drag::ResizeRegion;
                else {
                    m_drag = Drag::MoveRegion;
                    if (g.alt()) {   // Option-drag duplicates (Logic Pro, Pro Tools): the whole selection
                        const auto cur = selectionList();
                        const auto copies = m_s.duplicateRegions(cur);
                        m_modelDirty = true;
                        if (copies.size() == cur.size() && !copies.empty()) {
                            const size_t ai = size_t(std::find(cur.begin(), cur.end(), me) - cur.begin());
                            setMulti(copies, copies[ai]);
                            m_dragTrack = copies[ai].first; m_dragRegion = copies[ai].second; m_dragCurTrack = m_dragTrack;
                            m_dragCurStart = m_s.regionStartBeat(m_dragTrack, m_dragRegion);
                            m_dragMoved = true;
                        }
                    }
                }
            } else {
                const bool additive = g.shift() || g.ctrl();
                m_rubBase = additive ? selectionList() : std::vector<std::pair<int, int>>{};
                if (!additive) setMulti({}, {m_selTrack, -1});
                if (it.doubleClicked && instrumentLane) {
                    const double b = snapBeat(mouseBeat);
                    const int made = m_tracks[size_t(ht)].type == core::TrackType::Drummer ? m_s.addDrummerRegion(ht, b, 4) : m_s.addMidiRegion(ht, b, 4);
                    m_modelDirty = true;
                    if (made >= 0) m_selRegion = made;
                } else {   // drag on an empty part of the lanes: rubber band
                    m_drag = Drag::Rubber;
                    m_rubX0 = m_rubX1 = g.mouseX(); m_rubY0 = m_rubY1 = g.mouseY();
                }
            }
            break;
        case Tool::Pencil:
            if (hr >= 0) { m_selTrack = ht; m_selRegion = hr; m_drag = Drag::None; }
            else if (instrumentLane) { m_drag = Drag::Create; m_dragTrack = ht; m_createStart = m_createEnd = snapBeat(mouseBeat); }
            else if (ht >= 0) message("The pencil draws MIDI regions on instrument and Drummer tracks.");
            break;
        case Tool::Scissors:
            if (hr >= 0) { m_s.splitRegion(ht, hr, snapBeat(mouseBeat)); m_selRegion = -1; m_modelDirty = true; }
            break;
        case Tool::Eraser:
            if (hr >= 0) { m_s.removeRegion(ht, hr); m_selRegion = -1; m_drag = Drag::Erase; m_modelDirty = true; }
            else m_drag = Drag::Erase;
            break;
        case Tool::Mute:
            if (hr >= 0) { m_s.setRegionMute(ht, hr, !m_tracks[size_t(ht)].regions[size_t(hr)].mute); m_modelDirty = true; }
            break;
        }
    }
    if (it.held) {
        const double dx = double(g.mouseX() - m_dragOrigX) / m_ppb;
        if (m_drag == Drag::MoveRegion && m_dragTrack >= 0) {
            if (!m_dragMoved && std::fabs(g.mouseX() - m_dragMouseX) > 3) { m_s.checkpoint(); m_dragMoved = true; }
            if (m_dragMoved) {
                const double bd = snapBeat(m_dragStart + dx) - m_dragCurStart;
                const int ty = trackAtY(g.mouseY());
                const int td = ty >= 0 ? ty - m_dragCurTrack : 0;   // dragging over another track moves the regions there when they fit
                if (std::fabs(bd) > 1e-9 || td != 0) {
                    const auto cur = selectionList();
                    const auto moved = m_s.moveRegions(cur, bd, td);
                    if (moved.size() == cur.size() && !moved.empty()) {
                        const size_t ai = size_t(std::max<std::ptrdiff_t>(0, std::find(cur.begin(), cur.end(), std::make_pair(m_dragTrack, m_dragRegion)) - cur.begin()));
                        setMulti(moved, moved[std::min(ai, moved.size() - 1)]);
                        m_dragTrack = m_selTrack; m_dragRegion = m_selRegion; m_dragCurTrack = m_selTrack;
                        m_dragCurStart = m_s.regionStartBeat(m_selTrack, m_selRegion);
                    }
                }
                m_modelDirty = true;
            }
        } else if (m_drag == Drag::ResizeRegion) {
            if (!m_dragMoved && std::fabs(g.mouseX() - m_dragMouseX) > 3) { m_s.checkpoint(); m_dragMoved = true; }
            if (m_dragMoved) { m_s.resizeRegion(m_dragTrack, m_dragRegion, std::max(0.25, snapBeat(m_dragStart + m_dragLen + dx) - m_dragStart)); m_modelDirty = true; }
            g.setCursor(Cursor::ResizeH);
        } else if (m_drag == Drag::Rubber) {
            m_rubX1 = g.mouseX(); m_rubY1 = g.mouseY();
        } else if (m_drag == Drag::Create) {
            m_createEnd = snapBeat(mouseBeat);
        } else if (m_drag == Drag::Erase && hr >= 0 && !it.pressed && !m_modelDirty.load()) {   // m_tracks is stale until the next refresh
            m_s.removeRegion(ht, hr); m_selRegion = -1; m_modelDirty = true;
        }
    }
    if (it.released) {
        if (m_drag == Drag::Create && m_dragTrack >= 0) {
            const double a = std::min(m_createStart, m_createEnd), b = std::max(m_createStart, m_createEnd);
            const double len = std::max(b - a, gridBeats() > 1 ? gridBeats() : 1.0);
            const int made = m_tracks[size_t(m_dragTrack)].type == core::TrackType::Drummer ? m_s.addDrummerRegion(m_dragTrack, a, std::max(1, int(std::ceil(len / 4.0 - 1e-9)))) : m_s.addMidiRegion(m_dragTrack, a, len);
            m_modelDirty = true;
            if (made >= 0) { m_selTrack = m_dragTrack; m_selRegion = made; m_dock = DockTab::Editor; m_showDock = true; m_dockH = std::max(m_dockH, 430.f); }
        }
        if (m_drag == Drag::Rubber && (std::fabs(m_rubX1 - m_rubX0) > 3 || std::fabs(m_rubY1 - m_rubY0) > 3)) {
            const float rx0 = std::min(m_rubX0, m_rubX1), rx1 = std::max(m_rubX0, m_rubX1), ry0 = std::min(m_rubY0, m_rubY1), ry1 = std::max(m_rubY0, m_rubY1);
            auto list = m_rubBase;
            for (int ti = 0; ti < int(m_tracks.size()); ++ti) {
                const float y0 = laneY(ti), y1 = y0 + m_trackH;
                if (y1 < ry0 || y0 > ry1) continue;
                const auto& regs = m_tracks[size_t(ti)].regions;
                for (int ri = 0; ri < int(regs.size()); ++ri) {
                    const double x0 = beatToX(regs[size_t(ri)].start), x1 = beatToX(regs[size_t(ri)].start + m_s.regionBeats(regs[size_t(ri)]));
                    if (x1 < rx0 || x0 > rx1) continue;
                    if (std::find(list.begin(), list.end(), std::make_pair(ti, ri)) == list.end()) list.emplace_back(ti, ri);
                }
            }
            if (!list.empty()) { setMulti(list, list.back()); message(fmt("%1 regions selected.", std::to_string(list.size()))); }
        }
        if (m_payActive) dropPayload();
        m_drag = Drag::None;
        m_payActive = false;
        m_modelDirty = true;
    }
    if (it.rightClicked) {
        if (ht >= 0) m_selTrack = ht;
        if (hr >= 0) { m_selRegion = hr; regionMenu(ht, hr); }
        else emptyLaneMenu(ht, snapBeat(mouseBeat));
    }
}

void Studio::dropPayload() {
    Gui& g = *m_g;
    const int t = trackAtY(g.mouseY());
    const double beat = snapBeat(xToBeat(g.mouseX()));
    if (m_payKind == 0 && !m_payPath.empty()) {
        m_s.importAudio(m_payPath, t >= 0 && m_tracks[size_t(t)].type == core::TrackType::Audio ? t : -1, beat);
        message("Importing " + m_payName);
        if (!(m_loopsLoaded)) {}
        for (const LoopEntry& l : m_loops) if (l.path == m_payPath && l.bpm > 0 && !m_s.isPlaying()) { /* the loop keeps its own tempo; Follow Tempo is in the inspector */ }
    } else if (m_payKind == 1) {
        if (t >= 0 && (m_tracks[size_t(t)].type == core::TrackType::Instrument)) { m_s.setInstrument(t, m_payId); message("Instrument set: " + m_payName); }
        else { const int n = m_s.addTrack(core::TrackType::Instrument, m_payName); m_s.setInstrument(n, m_payId); m_selTrack = n; message("New instrument track: " + m_payName); }
    } else if (m_payKind == 2) {
        if (t >= 0) addInsertTo(t, m_payId); else message("Drop effects onto a track.");
    }
    m_modelDirty = true;
}

std::vector<MenuItem> Studio::itemsFor(const std::vector<std::string>& labels) {
    std::vector<MenuItem> items;
    for (const std::string& l : labels) {
        if (l == "-") { items.push_back(MenuItem::sep()); continue; }
        for (const Command& c : m_commands)
            if (c.label == l) {
                MenuItem m;
                m.label = c.label; m.shortcut = c.shortcut;
                m.enabled = !c.enabled || c.enabled();
                m.checked = c.checked && c.checked();
                m.action = [this, run = c.run] { run(); m_modelDirty = true; };
                items.push_back(std::move(m));
                break;
            }
    }
    return items;
}

void Studio::trackHeaderMenu(int track) {
    auto items = itemsFor({"Rename Track...", "Duplicate Track", "Delete Track", "-", "Move Track Up", "Move Track Down", "-", "Arm Record", "Mute Track", "Solo Track", "-", "Bounce in Place", "Track Color: Next"});
    MenuItem grp; grp.label = "Mixer Group"; grp.children = groupMenuItems(track);
    items.push_back(MenuItem::sep());
    items.push_back(grp);
    m_g->openMenu(m_g->mouseX(), m_g->mouseY(), std::move(items));
}

void Studio::regionMenu(int track, int region) {
    (void)track; (void)region;
    const core::Region* r = this->region();
    std::vector<std::string> labels = {"Cut Region", "Copy Region", "Paste Region", "Duplicate Region", "Delete Region", "-", "Split at Playhead", "Join Regions", "Mute Region", "Rename Region..."};
    if (r && r->midi) for (const char* l : {"-", "Quantize...", "Transpose...", "Legato", "Humanize...", "Riff Machine...", "Logical Editor...", "Make Groove Template", "Apply Groove Template..."}) labels.push_back(l);
    else if (r) for (const char* l : {"-", "Normalize...", "Reverse", "Strip Silence...", "Beat Detective...", "Adapt Project Tempo to Region...", "Follow Tempo and Pitch...", "Fades and Gain..."}) labels.push_back(l);
    m_g->openMenu(m_g->mouseX(), m_g->mouseY(), itemsFor(labels));
}

void Studio::emptyLaneMenu(int track, double beat) {
    std::vector<MenuItem> items;
    if (track >= 0 && (m_tracks[size_t(track)].type == core::TrackType::Instrument || m_tracks[size_t(track)].type == core::TrackType::Drummer)) {
        MenuItem a; a.label = "New Region Here (1 bar)"; a.action = [this, track, beat] { const int n = m_tracks[size_t(track)].type == core::TrackType::Drummer ? m_s.addDrummerRegion(track, beat, 1) : m_s.addMidiRegion(track, beat, 4); m_selTrack = track; m_selRegion = n; m_modelDirty = true; };
        items.push_back(a);
    }
    MenuItem p; p.label = "Paste Region"; p.shortcut = "Ctrl+V"; p.enabled = m_s.hasClipboard() && track >= 0; p.action = [this, track, beat] { m_s.pasteRegion(track, beat); m_modelDirty = true; };
    MenuItem m; m.label = "Add Marker Here"; m.action = [this, beat] { m_s.addMarker(beat); };
    MenuItem i; i.label = "Import Audio..."; i.shortcut = "Ctrl+I"; i.action = [this] { doFile("import"); };
    items.push_back(p); items.push_back(m); items.push_back(MenuItem::sep()); items.push_back(i);
    m_g->openMenu(m_g->mouseX(), m_g->mouseY(), std::move(items));
}


// ----------------------------------------------------------------------------------------------------------- overview

std::string Studio::selectionText() const {   // Pro Tools counter: Start, End, Length
    const auto tm = m_tempo ? m_tempo : m_s.tempoMap();
    double a = -1, b = -1;
    if (const core::Region* r = region()) { a = r->start; b = r->start + m_s.regionBeats(*r); }
    else if (m_s.loopA() >= 0 && m_s.loopB() > m_s.loopA()) { a = m_s.loopA(); b = m_s.loopB(); }
    if (a < 0) return "Start - End - Length -";
    auto pos = [&](double beat) {
        const int bar = tm->barAt(beat);
        const double inBar = beat - tm->barStart(bar);
        const double unit = 4.0 / std::max(1, tm->meterAtBeat(beat).den);
        char buf[24];
        std::snprintf(buf, sizeof buf, "%d|%d", bar, int(inBar / unit + 1e-9) + 1);
        return std::string(buf);
    };
    char buf[96];
    std::snprintf(buf, sizeof buf, "Start %s  End %s  Len %.4g", pos(a).c_str(), pos(b).c_str(), b - a);
    return buf;
}

void Studio::drawOverview() {
    Gui& g = *m_g;
    DrawList& dl = *m_dl;
    const Rect r = m_rOverview;
    dl.rect(r, bg0);
    dl.hline(r.x, r.right(), r.bottom() - 1, line);
    // left corner: the selection counter
    {
        const Rect c{r.x, r.y, m_headerW, r.h};
        dl.rect(c, bg2);
        dl.vline(c.right() - 1, c.y, c.bottom(), line);
        const bool any = hasRegion() || (m_s.loopA() >= 0 && m_s.loopB() > m_s.loopA());
        dl.textIn({c.x + 10, c.y, c.w - 16, c.h}, selectionText(), any ? lcdText : textMute, {10, false});
    }
    const Rect map{r.x + m_headerW + 4, r.y + 3, r.w - m_headerW - 8 - kScroll, r.h - 6};
    if (map.w < 20) return;
    const double view = double(m_rLanes.w) / m_ppb;
    const double total = std::max({m_s.projectEndBeats() + 8.0, m_scrollX + view, 32.0});
    const float k = float(map.w / total);
    dl.pushClip(map);
    const int n = int(m_tracks.size());
    const float rowH = n > 0 ? std::clamp((map.h - 1) / float(n), 1.5f, 6.f) : 2.f;
    for (int i = 0; i < n; ++i) {
        const core::Track& t = m_tracks[size_t(i)];
        const Color col = Color(0xFF000000u) | (Color(t.color.r) << 16) | (Color(t.color.g) << 8) | Color(t.color.b);
        const float y = map.y + 0.5f + rowH * float(i);
        if (y + rowH > map.bottom() + 0.5f) break;
        for (const core::Region& rg : t.regions) {
            const float x0 = map.x + float(rg.start) * k, w = std::max(1.5f, float(m_s.regionBeats(rg)) * k);
            dl.rect({x0, y, w, std::max(1.f, rowH - 0.5f)}, rg.mute ? withAlpha(col, 90) : col);
        }
    }
    // the visible part, the playhead and the cycle range
    if (m_s.loopA() >= 0 && m_s.loopB() > m_s.loopA()) dl.rect({map.x + float(m_s.loopA()) * k, map.y, std::max(2.f, float(m_s.loopB() - m_s.loopA()) * k), 2.f}, loop);
    const Rect vp{map.x + float(m_scrollX) * k, map.y - 1, std::max(6.f, float(view) * k), map.h + 2};
    dl.rect(vp, withAlpha(rgb(255, 255, 255), 28));
    dl.rrectStroke(vp, 2, 1, withAlpha(rgb(255, 255, 255), 150));
    const float px = map.x + float(m_s.positionBeats()) * k;
    dl.vline(px, map.y - 1, map.bottom() + 1, playhead);
    dl.popClip();
    const Id oid = id("overview");
    const Interact it = g.interact(oid, map);
    if (it.hovered) { g.setCursor(Cursor::Hand); setHint("Overview: click or drag to scroll the arrangement"); }
    if (it.pressed || it.held) m_scrollX = std::clamp(double((g.mouseX() - map.x) / k) - view * 0.5, 0.0, std::max(0.0, total - view));
}

}  // namespace vkui
