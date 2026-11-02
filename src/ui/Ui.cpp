// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Ui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>

namespace ui {

namespace {
// Palette: deep slate with a teal accent (deliberately its own look, not a copy of any other DAW).
constexpr Rgba kBg = rgba(24, 26, 31), kPanel = rgba(32, 35, 42), kPanel2 = rgba(41, 45, 54), kPanel3 = rgba(52, 57, 68), kLine = rgba(14, 15, 18);
constexpr Rgba kText = rgba(226, 230, 238), kDim = rgba(137, 146, 163), kAccent = rgba(38, 222, 196), kAmber = rgba(255, 176, 46), kRec = rgba(255, 77, 94);
constexpr Rgba kLaneA = rgba(27, 29, 35), kLaneB = rgba(31, 34, 41);

Rgba trackColor(const core::Track& t) { return rgba(t.color.r, t.color.g, t.color.b); }
bool isDark(Rgba c) { return ((c >> 16 & 255) * 299 + (c >> 8 & 255) * 587 + (c & 255) * 114) / 1000 < 140; }

std::string typeName(core::TrackType t) {
    switch (t) {
    case core::TrackType::Audio: return "Audio";
    case core::TrackType::Instrument: return "Instrument";
    case core::TrackType::Drums: return "Drum Machine";
    default: return "Drummer";
    }
}

std::string num(const char* fmt, double v) { char b[64]; std::snprintf(b, sizeof b, fmt, v); return b; }

const double kSnapBeats[] = {4, 2, 1, 0.5, 0.25};
const char* const kSnapNames[] = {"1 Bar", "1/2", "1/4", "1/8", "1/16"};
}  // namespace

float NativeUi::faderToGain(double pos) {
    if (pos <= 0.0) return 0.f;
    return std::min(1.5f, float(std::pow(10.0, (-60.0 + 66.0 * std::min(1.0, pos)) / 20.0)));
}
double NativeUi::gainToFader(float gain) {
    if (gain <= 0.0009f) return 0.0;
    return std::clamp((20.0 * std::log10(double(gain)) + 60.0) / 66.0, 0.0, 1.0);
}

NativeUi::NativeUi(core::Session& s, Host host) : m_s(s), m_host(std::move(host)) {}

Rect NativeUi::arrangeArea() const {
    const int y = kControlH + kToolbarH;
    return {0, y, m_w, std::max(0, m_h - y - kStatusH - (m_mixer ? kMixerH : 0))};
}
Rect NativeUi::bottomArea() const { return m_mixer ? Rect{0, m_h - kStatusH - kMixerH, m_w, kMixerH} : Rect{}; }
Rect NativeUi::paneContent() const { const Rect b = bottomArea(); return {b.x, b.y + 30, b.w, b.h - 30}; }

double NativeUi::snapStep() const { return m_snapOn ? kSnapBeats[m_snapIdx] : 0.0; }
double NativeUi::snapBeat(double b) const {
    const double st = snapStep();
    return std::max(0.0, st > 0 ? std::round(b / st) * st : b);
}

void NativeUi::message(const std::string& text) { m_message = text; m_dirty = true; }

bool NativeUi::hasRegion() const {
    return m_selTrack >= 0 && m_selTrack < int(m_tracks.size()) && m_selRegion >= 0 && m_selRegion < int(m_tracks[size_t(m_selTrack)].regions.size());
}
bool NativeUi::regionIsMidi() const { return hasRegion() && m_tracks[size_t(m_selTrack)].regions[size_t(m_selRegion)].midi; }

// ---------------------------------------------------------------- painting
void NativeUi::button(Canvas& c, const Rect& r, const std::string& label, bool on, Rgba onColor, Hit kind, int a, int b, Font f) {
    const bool hover = r.contains(m_hoverX, m_hoverY);
    c.fillRoundRect(r, 5, on ? onColor : (hover ? kPanel3 : kPanel2));
    if (!on) c.strokeRoundRect(r, 5, withAlpha(kLine, 160));
    c.textIn(r, label, on ? (isDark(onColor) ? kText : rgba(16, 18, 22)) : kText, f, Align::Center);
    addHit(r, kind, a, b);
}

void NativeUi::paint(Canvas& c) {
    m_tracks = m_s.snapshot();
    if (m_selTrack >= int(m_tracks.size())) m_selTrack = m_selRegion = -1;
    m_hits.clear();
    m_sliderFns.clear();
    m_actionFns.clear();
    c.clear(kBg);
    drawArrange(c);
    if (m_mixer) drawBottom(c);
    drawToolbar(c);
    drawControlBar(c);
    drawStatus(c);
    drawMenu(c);
    if (m_dialog && m_dialog->finished()) m_dialog.reset();
    if (m_dialog) { m_dialog->setScreen(m_w, m_h); m_dialog->paint(c); }
    m_dirty = false;
}

void NativeUi::drawControlBar(Canvas& c) {
    const Rect bar = controlBar();
    c.fillGradient(bar, rgba(50, 55, 66), rgba(36, 39, 47));
    c.hline(0, m_w - 1, bar.bottom() - 1, kLine);
    // application mark
    c.fillRoundRect({14, 13, 28, 28}, 8, kAccent);
    c.text(28, 20, "M", rgba(12, 40, 36), Font::Bold13, Align::Center);
    c.text(52, 14, "MiniDAW", kText, Font::Bold13);
    c.text(52, 30, m_s.projectPath().empty() ? "Untitled" : m_s.projectPath().substr(m_s.projectPath().find_last_of("/\\") + 1), kDim, Font::Regular11);

    // transport cluster, centred
    const bool playing = m_s.isPlaying(), rec = m_s.isRecording();
    const int cy = bar.h / 2;
    int x = m_w / 2 - 290;
    const Rect rew{x, cy - 15, 34, 30};
    button(c, rew, "", false, kAccent, Hit::Rewind);
    c.fillRect({rew.x + 11, rew.y + 9, 2, 12}, kText);
    c.fillTriangle(rew.x + 23, rew.y + 8, rew.x + 23, rew.y + 22, rew.x + 14, rew.y + 15, kText);
    x += 40;
    const Rect stop{x, cy - 15, 34, 30};
    button(c, stop, "", false, kAccent, Hit::Stop);
    c.fillRoundRect({stop.x + 11, stop.y + 9, 12, 12}, 2, kText);
    x += 40;
    const Rect play{x, cy - 18, 44, 36};
    c.fillRoundRect(play, 18, playing ? kAccent : kPanel3);
    c.strokeRoundRect(play, 18, withAlpha(kLine, 200));
    if (playing) { c.fillRect({play.x + 14, play.y + 11, 5, 14}, rgba(10, 36, 32)); c.fillRect({play.x + 25, play.y + 11, 5, 14}, rgba(10, 36, 32)); }
    else c.fillTriangle(play.x + 17, play.y + 10, play.x + 17, play.y + 26, play.x + 31, play.y + 18, kText);
    addHit(play, Hit::Play);
    x += 52;
    const Rect rc{x, cy - 15, 34, 30};
    button(c, rc, "", rec, kRec, Hit::Record);
    c.fillCircle(rc.x + 17, rc.y + 15, 6, rec ? rgba(255, 255, 255) : kRec);
    x += 40;
    const Rect cyc{x, cy - 15, 34, 30};
    button(c, cyc, "", m_s.loopOn(), kAmber, Hit::Cycle);
    c.arc(cyc.x + 17, cyc.y + 15, 6.5, 0.6, 5.5, 2.0, m_s.loopOn() ? rgba(40, 28, 0) : kText);
    c.fillTriangle(cyc.x + 22, cyc.y + 6, cyc.x + 25, cyc.y + 13, cyc.x + 17, cyc.y + 11, m_s.loopOn() ? rgba(40, 28, 0) : kText);
    x += 52;

    // LCD pill: bar.beat.tick, clock, tempo and meter
    const double pos = m_s.positionBeats();
    const auto tm = m_s.tempoMap();
    const int barNo = tm->barAt(pos);
    const double inBar = std::max(0.0, pos - tm->barStart(barNo));
    const int beat = int(inBar) + 1, tick = int((inBar - std::floor(inBar)) * 960.0);
    const double secs = tm->beatToFrame(pos) / 44100.0;
    const Rect lcd{x, 8, 246, bar.h - 16};
    c.fillRoundRect(lcd, 14, rgba(13, 15, 18));
    c.strokeRoundRect(lcd, 14, rgba(70, 76, 90, 180));
    c.text(lcd.x + 16, lcd.y + 4, num("%03.0f", barNo) + "." + std::to_string(beat) + "." + num("%03.0f", tick / 10), kAccent, Font::Bold13);
    c.text(lcd.x + 16, lcd.y + 21, num("%d", int(secs) / 60) + ":" + (int(secs) % 60 < 10 ? "0" : "") + std::to_string(int(secs) % 60) + "." + num("%03.0f", (secs - std::floor(secs)) * 1000), kDim, Font::Regular11);
    c.vline(lcd.x + 124, lcd.y + 7, lcd.bottom() - 8, rgba(60, 66, 80));
    const auto meter = tm->meterAtBeat(pos);
    c.text(lcd.x + 138, lcd.y + 4, num("%.1f", tm->bpmAt(pos)), kAmber, Font::Bold13);
    c.text(lcd.x + 138, lcd.y + 21, "BPM   " + std::to_string(meter.num) + "/" + std::to_string(meter.den), kDim, Font::Regular11);
    x += 258;
    button(c, {x, cy - 13, 30, 26}, "", m_s.metronome(), kAccent, Hit::Metronome);
    c.fillTriangle(x + 15, cy - 8, x + 21, cy + 8, x + 9, cy + 8, m_s.metronome() ? rgba(10, 36, 32) : kText);
    button(c, {x + 36, cy - 13, 30, 26}, "1 2", m_s.countIn(), kAccent, Hit::CountIn, 0, 0, Font::Regular11);

    // master meter on the right
    float l = 0, r = 0;
    m_s.masterPeaks(l, r);
    m_meterL = std::max(l, m_meterL * 0.86f);
    m_meterR = std::max(r, m_meterR * 0.86f);
    const Rect mm{m_w - 170, 14, 150, 10};
    c.text(mm.x, 32, "OUT", kDim, Font::Regular11);
    for (int k = 0; k < 2; ++k) {
        const Rect lane{mm.x + 28, 15 + k * 12, 122, 8};
        c.fillRoundRect(lane, 4, rgba(13, 15, 18));
        const float v = std::min(1.f, (k == 0 ? m_meterL : m_meterR));
        const int w = int(lane.w * std::pow(v, 0.5f));
        c.pushClip(lane);
        c.fillRoundRect({lane.x, lane.y, std::max(w, 0), lane.h}, 4, v > 0.9f ? kRec : v > 0.6f ? kAmber : kAccent);
        c.popClip();
    }
}

void NativeUi::drawToolbar(Canvas& c) {
    const Rect bar = toolbar();
    c.fillRect(bar, kPanel);
    c.hline(0, m_w - 1, bar.bottom() - 1, kLine);
    int x = 10;
    const struct { const char* label; Hit kind; int w; } menus[] = {{"File", Hit::MenuFile, 44}, {"Edit", Hit::MenuEdit, 44}, {"Track", Hit::MenuTrack, 50}, {"Functions", Hit::MenuFunctions, 74}, {"View", Hit::MenuView, 44}};
    for (const auto& m : menus) {
        const Rect r{x, bar.y + 5, m.w, 22};
        const bool hover = r.contains(m_hoverX, m_hoverY);
        if (hover) c.fillRoundRect(r, 5, kPanel3);
        c.textIn(r, m.label, kText, Font::Regular11, Align::Center);
        addHit(r, m.kind);
        x += m.w + 2;
    }
    c.vline(x + 6, bar.y + 8, bar.y + 24, kLine);
    x += 16;
    button(c, {x, bar.y + 5, 92, 22}, "+  Add Track", false, kAccent, Hit::AddTrack, 0, 0, Font::Bold11);
    x += 104;
    const Rect snap{x, bar.y + 5, 104, 22};
    button(c, snap, std::string("Snap  ") + (m_snapOn ? kSnapNames[m_snapIdx] : "Off"), m_snapOn, rgba(52, 96, 124), Hit::Snap, 0, 0, Font::Regular11);
    // right side: zoom and mixer
    button(c, {m_w - 122, bar.y + 5, 26, 22}, "-", false, kAccent, Hit::ZoomOut, 0, 0, Font::Bold13);
    button(c, {m_w - 92, bar.y + 5, 26, 22}, "+", false, kAccent, Hit::ZoomIn, 0, 0, Font::Bold13);
    button(c, {m_w - 62, bar.y + 5, 52, 22}, "Mixer", m_mixer, kAccent, Hit::MixerToggle, 0, 0, Font::Regular11);
}

void NativeUi::drawArrange(Canvas& c) {
    const Rect area = arrangeArea();
    if (area.empty()) return;
    const int laneY = area.y + kRulerH;
    const Rect lanes{kHeaderW, laneY, area.w - kHeaderW, area.h - kRulerH};
    const Rect heads{0, laneY, kHeaderW, area.h - kRulerH};
    c.fillRect({0, area.y, area.w, area.h}, kLaneA);
    const double endBeat = xToBeat(m_w);

    // ---- ruler ----
    c.fillRect({kHeaderW, area.y, area.w - kHeaderW, kRulerH}, kPanel);
    c.fillRect({0, area.y, kHeaderW, kRulerH}, kPanel);
    c.text(14, area.y + 7, "Tracks", kDim, Font::Bold11);
    c.hline(0, m_w - 1, area.y + kRulerH - 1, kLine);
    const auto tm = m_s.tempoMap();
    if (m_s.loopOn() && m_s.loopB() > m_s.loopA()) {
        const double x0 = beatToX(m_s.loopA()), x1 = beatToX(m_s.loopB());
        c.pushClip({kHeaderW, area.y, area.w - kHeaderW, kRulerH});
        c.fillRect({int(x0), area.y + 2, int(x1 - x0), kRulerH - 4}, withAlpha(kAmber, 90));
        c.popClip();
    }
    c.pushClip({kHeaderW, area.y, area.w - kHeaderW, area.h});
    const int firstBar = std::max(1, tm->barAt(std::max(0.0, xToBeat(kHeaderW))));
    const int labelEvery = m_ppb * 4 >= 60 ? 1 : m_ppb * 4 >= 30 ? 2 : m_ppb * 4 >= 16 ? 4 : 8;
    for (int bar = firstBar; tm->barStart(bar) <= endBeat + 1; ++bar) {
        const double b0 = tm->barStart(bar), len = tm->barLength(bar);
        const int x = int(beatToX(b0));
        if (x > m_w) break;
        c.vline(x, area.y + 14, area.y + kRulerH - 2, kDim);
        if ((bar - 1) % labelEvery == 0) c.text(x + 4, area.y + 4, std::to_string(bar), kText, Font::Regular11);
        c.vline(x, laneY, area.bottom() - 1, withAlpha(rgba(255, 255, 255), 26));
        if (m_ppb >= 14)
            for (double b = 1; b < len; b += 1) {
                const int bx = int(beatToX(b0 + b));
                c.vline(bx, area.y + 19, area.y + kRulerH - 2, rgba(90, 98, 114));
                if (m_ppb >= 24) c.vline(bx, laneY, area.bottom() - 1, withAlpha(rgba(255, 255, 255), 10));
            }
    }
    {   // markers
        const auto marks = m_s.markers();
        for (int i = 0; i < int(marks.size()); ++i) {
            const int mx = int(beatToX(marks[size_t(i)].beat));
            if (mx < kHeaderW - 4 || mx > m_w) continue;
            const std::string nm = marks[size_t(i)].name.empty() ? "Marker " + std::to_string(i + 1) : marks[size_t(i)].name;
            const int w = std::min(120, c.textWidth(nm, Font::Regular11) + 12);
            c.fillRoundRect({mx, area.y + 1, w, 12}, 3, rgba(120, 82, 230));
            c.textIn({mx + 4, area.y + 1, w - 6, 12}, nm, rgba(240, 236, 255), Font::Regular11);
            c.vline(mx, area.y + 1, area.bottom() - 1, withAlpha(rgba(150, 110, 255), 70));
            addHit({mx - 2, area.y, w + 4, 14}, Hit::Marker, i);
        }
    }
    c.popClip();
    addHit({kHeaderW, area.y, area.w - kHeaderW, kRulerH}, Hit::Ruler);

    // ---- tracks ----
    c.pushClip(lanes);
    for (int i = 0; i < int(m_tracks.size()); ++i) {
        const int y = laneY + i * kTrackH - int(m_scrollY);
        if (y + kTrackH < laneY || y > area.bottom()) continue;
        const Rect lane{kHeaderW, y, lanes.w, kTrackH};
        c.fillRect(lane, i % 2 ? kLaneB : kLaneA);
        c.hline(kHeaderW, m_w - 1, y + kTrackH - 1, withAlpha(kLine, 200));
        addHit(lane.intersect(lanes), Hit::Lane, i);
    }
    // bar lines on top of the lane backgrounds
    for (int bar = firstBar; tm->barStart(bar) <= endBeat + 1; ++bar) {
        const double b0 = tm->barStart(bar);
        const int x = int(beatToX(b0));
        if (x > m_w) break;
        c.vline(x, laneY, area.bottom() - 1, withAlpha(rgba(255, 255, 255), 22));
    }
    for (int i = 0; i < int(m_tracks.size()); ++i) {
        const core::Track& t = m_tracks[size_t(i)];
        const int y = laneY + i * kTrackH - int(m_scrollY);
        if (y + kTrackH < laneY || y > area.bottom()) continue;
        for (int ri = 0; ri < int(t.regions.size()); ++ri) {
            const core::Region& rg = t.regions[size_t(ri)];
            const double x0 = beatToX(rg.start), x1 = beatToX(rg.start + m_s.regionBeats(rg));
            if (x1 < kHeaderW || x0 > m_w) continue;
            const Rect r{int(std::lround(x0)), y + 4, std::max(6, int(std::lround(x1 - x0))), kTrackH - 8};
            drawRegion(c, t, i, ri, r, trackColor(t));
            addHit(r.intersect(lanes), Hit::Region, i, ri);
            addHit({r.right() - 6, r.y, 6, r.h}, Hit::RegionEdge, i, ri);
        }
    }
    c.popClip();

    // ---- headers ----
    c.pushClip(heads);
    c.fillRect(heads, kPanel);
    for (int i = 0; i < int(m_tracks.size()); ++i) {
        const core::Track& t = m_tracks[size_t(i)];
        const int y = laneY + i * kTrackH - int(m_scrollY);
        if (y + kTrackH < laneY || y > area.bottom()) continue;
        const Rect h{0, y, kHeaderW, kTrackH};
        const Rgba col = trackColor(t);
        const bool sel = i == m_selTrack;
        c.fillRect(h, sel ? kPanel3 : (i % 2 ? kPanel : rgba(35, 38, 46)));
        c.fillRect({0, y, 5, kTrackH - 1}, col);
        c.hline(0, kHeaderW - 1, y + kTrackH - 1, kLine);
        addHit(h, Hit::TrackHeader, i);
        c.textIn({16, y + 7, 112, 16}, t.name, kText, Font::Bold11);
        c.textIn({16, y + 23, 112, 14}, typeName(t.type), kDim, Font::Regular11);
        const int bx = kHeaderW - 3 * 27 - 6;
        button(c, {bx, y + 7, 24, 20}, "M", t.mute, kAmber, Hit::TrackMute, i);
        button(c, {bx + 27, y + 7, 24, 20}, "S", t.solo, rgba(250, 214, 60), Hit::TrackSolo, i);
        button(c, {bx + 54, y + 7, 24, 20}, "R", t.armed, kRec, Hit::TrackArm, i);
        // volume slider
        const Rect groove{16, y + 42, kHeaderW - 36, 6};
        c.fillRoundRect(groove, 3, rgba(14, 15, 18));
        const int fill = int(groove.w * std::clamp(t.volume / 1.5f, 0.f, 1.f));
        c.pushClip(groove);
        c.fillRoundRect({groove.x, groove.y, std::max(fill, 0), groove.h}, 3, withAlpha(col, 230));
        c.popClip();
        c.fillCircle(groove.x + fill, groove.y + 3, 5.5, rgba(236, 240, 247));
        addHit({groove.x - 6, groove.y - 6, groove.w + 12, groove.h + 12}, Hit::TrackVol, i);
    }
    if (m_tracks.empty()) c.textIn({0, laneY + 20, kHeaderW, 20}, "No tracks - use  + Add Track", kDim, Font::Regular11, Align::Center);
    c.popClip();
    c.vline(kHeaderW - 1, area.y, area.bottom() - 1, kLine);

    // ---- playhead ----
    const double px = beatToX(m_s.positionBeats());
    if (px >= kHeaderW - 1 && px < m_w) {
        c.pushClip({kHeaderW, area.y, area.w - kHeaderW, area.h});
        c.vline(int(px), area.y + 2, area.bottom() - 1, withAlpha(kAccent, 235));
        c.fillTriangle(px - 6, area.y + 2, px + 7, area.y + 2, px + 0.5, area.y + 12, kAccent);
        c.popClip();
    }
}

void NativeUi::drawRegion(Canvas& c, const core::Track& t, int ti, int ri, const Rect& r, Rgba color) {
    const core::Region& rg = t.regions[size_t(ri)];
    const bool sel = ti == m_selTrack && ri == m_selRegion;
    const Rgba body = rg.mute ? rgba(58, 62, 72) : mix(rgba(24, 26, 31), color, sel ? 0.62 : 0.46);
    const Rgba head = rg.mute ? rgba(84, 90, 102) : (sel ? lighten(color, 0.12) : mix(color, rgba(24, 26, 31), 0.12));
    c.fillRoundRect(r, 4, body);
    c.pushClip(r);
    c.fillRect({r.x, r.y, r.w, 15}, head);
    c.popClip();
    const Rgba ink = rg.mute ? rgba(190, 196, 208) : lighten(color, 0.72);
    const Rect content{r.x + 1, r.y + 16, r.w - 2, r.h - 18};
    c.pushClip(content);
    if (rg.midi && !rg.notes.empty()) {
        int lo = 127, hi = 0;
        for (const auto& n : rg.notes) { lo = std::min(lo, n.pitch); hi = std::max(hi, n.pitch); }
        const int span = std::max(12, hi - lo + 1);
        const double rowH = double(content.h - 4) / span;
        for (const auto& n : rg.notes) {
            const double nx0 = beatToX(rg.start + n.start), nx1 = beatToX(rg.start + n.start + n.length);
            const double ny = content.y + 2 + (hi - n.pitch) * rowH;
            c.fillRect({int(nx0), int(ny), std::max(2, int(nx1 - nx0) - 1), std::max(2, int(rowH))}, withAlpha(ink, 120 + n.velocity));
        }
    } else if (!rg.midi && rg.audio && !rg.audio->peaks.empty() && rg.frames > 0) {
        const auto& pk = rg.audio->peaks;
        const int midY = content.y + content.h / 2, half = std::max(2, content.h / 2 - 1);
        const int xa = std::max(r.x, kHeaderW), xb = std::min(r.right(), m_w);
        for (int x = xa; x < xb; ++x) {
            const double f0 = double(rg.offset) + double(x - r.x) / r.w * double(rg.frames), f1 = double(rg.offset) + double(x + 1 - r.x) / r.w * double(rg.frames);
            size_t i0 = size_t(std::max(0.0, f0) / kPeakStride), i1 = size_t(std::max(0.0, f1) / kPeakStride);
            i1 = std::min(std::max(i1, i0), pk.size() - 1);
            float m = 0;
            for (size_t i = std::min(i0, pk.size() - 1); i <= i1; ++i) m = std::max(m, pk[i]);
            const int h = std::max(1, int(std::min(1.f, m) * half));
            c.vline(x, midY - h, midY + h, withAlpha(ink, 225));
        }
    } else if (!rg.midi && !rg.pendingPath.empty()) {
        c.textIn(content, "loading...", ink, Font::Regular11, Align::Center);
    }
    c.popClip();
    c.textIn({r.x + 6, r.y, std::max(0, r.w - 10), 15}, rg.name.empty() ? t.name : rg.name, isDark(head) ? rgba(240, 244, 250) : rgba(16, 18, 22), Font::Bold11);
    if (sel) c.strokeRoundRect(r, 4, rgba(245, 248, 252));
    else c.strokeRoundRect(r, 4, withAlpha(rgba(0, 0, 0), 110));
}

float stripPeak(const core::Track& t) { return std::max(t.peakL, t.peakR); }

void NativeUi::drawStrip(Canvas& c, const Rect& r, const core::Track* t, int ti) {
    const bool master = t == nullptr;
    const bool sel = !master && ti == m_selTrack;
    const Rgba col = master ? rgba(188, 196, 212) : trackColor(*t);
    c.fillRect(r, sel ? rgba(40, 44, 54) : kPanel);
    c.vline(r.right() - 1, r.y, r.bottom() - 1, kLine);
    if (!master) addHit(r, Hit::MixerStrip, ti);
    // name chip on top
    const Rect chip{r.x + 6, r.y + 6, r.w - 12, 20};
    c.fillRoundRect(chip, 10, withAlpha(col, 235));
    c.textIn(chip.inset(8, 0), master ? "Master" : t->name, isDark(col) ? rgba(240, 244, 250) : rgba(16, 18, 22), Font::Bold11, Align::Center);
    // insert slots
    for (int k = 0; k < 4; ++k) {
        const Rect slot{r.x + 8, r.y + 34 + k * 17, r.w - 16, 14};
        c.fillRoundRect(slot, 4, rgba(20, 22, 27));
        if (!master && k < int(t->inserts.size()) && t->inserts[size_t(k)]) {
            std::string id = t->inserts[size_t(k)]->id();
            if (!id.empty()) id[0] = char(std::toupper((unsigned char)id[0]));
            c.fillRoundRect(slot, 4, rgba(44, 86, 120));
            c.textIn(slot.inset(4, 0), id, rgba(214, 232, 250), Font::Regular11, Align::Center);
            addHit(slot, Hit::Insert, ti, k);
        }
    }
    // pan knob
    const float pan = master ? 0.f : t->pan;
    const double kx = r.x + r.w / 2.0, ky = r.y + 118;
    c.fillCircle(kx, ky, 13, rgba(20, 22, 27));
    c.arc(kx, ky, 11, 2.35, 2.35 + 4.71, 2.5, rgba(60, 66, 80));
    const double pa = (double(pan) * 2.2) - 1.5708;   // pointing up at centre
    if (std::fabs(pan) > 0.01f) c.arc(kx, ky, 11, std::min(pa, -1.5708), std::max(pa, -1.5708), 2.5, col);
    c.line(kx, ky, kx + std::cos(pa) * 9, ky + std::sin(pa) * 9, rgba(236, 240, 247));
    if (!master) addHit({int(kx) - 15, int(ky) - 15, 30, 30}, Hit::Pan, ti);
    c.text(int(kx), int(ky) + 16, master ? "" : (std::fabs(pan) < 0.02f ? "C" : num(pan < 0 ? "L%.0f" : "R%.0f", std::fabs(pan) * 100)), kDim, Font::Regular11, Align::Center);
    // fader + meter
    const Rect travel{r.x + 32, r.y + 152, 8, r.h - 152 - 66};
    const float vol = master ? m_s.masterVolume() : t->volume;
    c.fillRoundRect({travel.x + 2, travel.y, 4, travel.h}, 2, rgba(12, 13, 16));
    for (double db : {6.0, 0.0, -6.0, -12.0, -24.0, -48.0}) {
        const int y = travel.y + travel.h - int(travel.h * (db + 60.0) / 66.0);
        c.hline(travel.x - 10, travel.x - 7, y, rgba(100, 108, 124));
        if (db == 0.0 || db == -12.0 || db == -24.0 || db == -48.0) c.text(travel.x - 12, y - 6, num("%.0f", std::fabs(db)), rgba(100, 108, 124), Font::Regular11, Align::Right);
    }
    const int capY = travel.y + travel.h - int(travel.h * gainToFader(vol));
    c.fillRoundRect({travel.x - 6, capY - 7, 20, 14}, 4, rgba(210, 216, 228));
    c.hline(travel.x - 3, travel.x + 9, capY, rgba(40, 44, 54));
    addHit({travel.x - 12, travel.y - 8, 32, travel.h + 16}, master ? Hit::MasterFader : Hit::Fader, ti);
    // level meters (two thin bars)
    float pl = 0, pr = 0;
    if (master) m_s.masterPeaks(pl, pr); else { pl = t->peakL; pr = t->peakR; }
    for (int k = 0; k < 2; ++k) {
        const Rect lane{r.x + 54 + k * 10, travel.y, 7, travel.h};
        c.fillRoundRect(lane, 3, rgba(12, 13, 16));
        const float v = std::min(1.f, k == 0 ? pl : pr);
        const double pos = gainToFader(v);
        const int h = int(lane.h * pos);
        c.pushClip(lane);
        c.fillGradient({lane.x, lane.bottom() - h, lane.w, h}, pos > 0.9 ? kRec : kAmber, kAccent);
        c.popClip();
    }
    c.text(r.x + r.w / 2, travel.bottom() + 10, vol <= 0.0009f ? "-inf" : num("%.1f", 20.0 * std::log10(double(vol))), kText, Font::Regular11, Align::Center);
    if (!master) {
        button(c, {r.x + 8, r.bottom() - 30, 22, 20}, "M", t->mute, kAmber, Hit::TrackMute, ti);
        button(c, {r.x + 33, r.bottom() - 30, 22, 20}, "S", t->solo, rgba(250, 214, 60), Hit::TrackSolo, ti);
        button(c, {r.x + 58, r.bottom() - 30, 22, 20}, "R", t->armed, kRec, Hit::TrackArm, ti);
    }
}

void NativeUi::drawMixer(Canvas& c) {
    const Rect area = paneContent();
    c.fillRect(area, kPanel);
    const int masterW = kStripW + 8;
    c.pushClip({0, area.y + 2, m_w - masterW, area.h - 2});
    for (int i = 0; i < int(m_tracks.size()); ++i) {
        const Rect r{i * kStripW, area.y + 2, kStripW, area.h - 2};
        if (r.right() < 0 || r.x > m_w) continue;
        drawStrip(c, r, &m_tracks[size_t(i)], i);
    }
    c.popClip();
    drawStrip(c, {m_w - masterW, area.y + 2, masterW, area.h - 2}, nullptr, -1);
    c.vline(m_w - masterW - 1, area.y + 2, area.bottom() - 1, rgba(70, 76, 90));
}

void NativeUi::drawStatus(Canvas& c) {
    const Rect r{0, m_h - kStatusH, m_w, kStatusH};
    c.fillRect(r, rgba(18, 19, 23));
    c.hline(0, m_w - 1, r.y, kLine);
    c.textIn({12, r.y, m_w / 2, r.h}, m_message.empty() ? "Ready" : m_message, m_message.empty() ? kDim : kText, Font::Regular11);
    c.textIn({m_w / 2, r.y, m_w / 2 - 12, r.h}, m_device, kDim, Font::Regular11, Align::Right);
}

void NativeUi::drawMenu(Canvas& c) {
    if (m_menu.empty()) return;
    c.fillRoundRect(m_menuRect.inset(-2), 8, withAlpha(rgba(0, 0, 0), 90));
    c.fillRoundRect(m_menuRect, 7, rgba(46, 50, 61));
    c.strokeRoundRect(m_menuRect, 7, rgba(84, 91, 108));
    int y = m_menuRect.y + 6;
    for (int i = 0; i < int(m_menu.size()); ++i) {
        const MenuItem& it = m_menu[size_t(i)];
        if (it.separator) { c.hline(m_menuRect.x + 10, m_menuRect.right() - 11, y + 4, rgba(84, 91, 108)); y += 9; continue; }
        const Rect row{m_menuRect.x + 5, y, m_menuRect.w - 10, 22};
        if (i == m_menuHover && it.enabled) c.fillRoundRect(row, 5, kAccent);
        const Rgba ink = !it.enabled ? rgba(112, 120, 136) : (i == m_menuHover ? rgba(8, 34, 30) : kText);
        const int sw = c.textWidth(it.shortcut);
        c.textIn({row.x + 10, row.y, row.w - 24 - sw, row.h}, it.label, ink, Font::Regular11);
        c.textIn({row.right() - 10 - sw, row.y, sw, row.h}, it.shortcut, i == m_menuHover && it.enabled ? rgba(8, 34, 30) : kDim, Font::Regular11, Align::Right);
        y += 22;
    }
}

}  // namespace ui
