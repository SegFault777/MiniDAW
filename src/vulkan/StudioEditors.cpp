// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Vulkan UI: the piano roll (with Ableton style Fold and Scale Highlight, FL Studio style chord stamps), the drum step
// sequencer and the automation lane.
#include <algorithm>
#include <cstdio>
#include "../core/MidiOps.h"
#include "../dsp/AudioOps.h"
#include "Icons.h"
#include "Studio.h"
#include "Theme.h"

namespace vkui {
using namespace theme;

namespace {
bool isBlack(int pitch) { const int p = pitch % 12; return p == 1 || p == 3 || p == 6 || p == 8 || p == 10; }
std::string pitchName(int pitch) { return std::string(core::noteName(pitch)) + std::to_string(pitch / 12 - 1); }
Color trackCol(const core::Track& t) { return Color(0xFF000000u) | (Color(t.color.r) << 16) | (Color(t.color.g) << 8) | Color(t.color.b); }
}  // namespace

void Studio::auditionNote(int track, int pitch) {
    if (track < 0 || track >= int(m_tracks.size())) return;
    if (m_auditionPitch >= 0) m_s.noteOff(m_auditionTrack, m_auditionPitch);
    m_s.noteOn(track, pitch, 96);
    m_auditionTrack = track;
    m_auditionPitch = pitch;
    m_auditionUntil = m_time + 0.35;
}

// ----------------------------------------------------------------------------------------------------------- editor tab

void Studio::drawEditor(const Rect& r) {
    DrawList& dl = *m_dl;
    m_aeWave = Rect{};
    if (hasMidiRegion()) { drawPianoRoll(r); return; }
    if (m_selTrack >= 0 && m_selTrack < int(m_tracks.size()) && m_tracks[size_t(m_selTrack)].type == core::TrackType::Drums) { drawSteps(r); return; }
    if (hasRegion() && region()->drummer) { drawPianoRoll(r); return; }
    if (hasAudioRegion() && region()->audio && region()->frames > 0) { drawAudioEditor(r); return; }
    dl.textIn(r, tr("Select a MIDI region to edit its notes - or double-click an instrument lane to create one."), textMute, normal, Align::Center);
}

// ------------------------------------------------------------------------------------------------------ audio editor
// The whole selected audio region as a waveform: a time ruler, the fade-in and fade-out curves with a handle each (drag to change, double-click
// to remove), the playhead, a click moves the playhead there. Gain, Normalize and Reverse are the same as in the inspector.

void Studio::trimAtPlayhead(bool start) {
    if (!hasAudioRegion()) return;
    const bool ok = start ? m_s.trimRegionStart(m_selTrack, m_selRegion, m_s.positionBeats()) : m_s.trimRegionEnd(m_selTrack, m_selRegion, m_s.positionBeats());
    if (!ok) message(region()->flexed() ? "Stretched or transposed audio cannot be trimmed here." : "Put the playhead inside the audio region first.");
    else m_modelDirty = true;
}

void Studio::drawAudioEditor(const Rect& r) {
    Gui& g = *m_g;
    DrawList& dl = *m_dl;
    const core::Region& rg = *region();
    const int ti = m_selTrack, ri = m_selRegion;
    const core::Track& trk = m_tracks[size_t(ti)];
    const Color col = trackCol(trk);
    const double sr = double(::kSampleRate);
    const double frames = double(rg.frames);

    // ---- header: what it is, the numbers, two actions
    dl.rect({r.x, r.y, r.w, 32}, bg2);
    dl.hline(r.x, r.right(), r.y + 31, line);
    char b[160];
    std::snprintf(b, sizeof b, "%.2f s   %s %+.1f dB   %s %.0f ms   %s %.0f ms", frames / sr, tr("Gain").c_str(), double(rg.gainDb), tr("Fade In").c_str(),
                  double(rg.fadeIn) * 1000.0 / sr, tr("Fade Out").c_str(), double(rg.fadeOut) * 1000.0 / sr);
    dl.textIn({r.x + 12, r.y, r.w * 0.6f, 31}, rg.name.empty() ? tr("Audio") : rg.name, text, normal);
    dl.textIn({r.x + 12 + std::min(r.w * 0.25f, 220.f), r.y, r.w * 0.6f, 31}, b, textDim, small);
    if (g.button(id("ae-rev"), {r.right() - 80, r.y + 4, 72, 24}, "Reverse")) m_s.reverseRegion(ti, ri);
    if (g.button(id("ae-norm"), {r.right() - 156, r.y + 4, 72, 24}, "Normalize")) m_s.normalizeRegion(ti, ri);
    if (g.button(id("ae-tout"), {r.right() - 244, r.y + 4, 84, 24}, "Trim End")) trimAtPlayhead(false);
    if (g.button(id("ae-tin"), {r.right() - 332, r.y + 4, 84, 24}, "Trim Start")) trimAtPlayhead(true);
    g.tip(id("ae-tin"), "Cut the start of the region at the playhead");
    g.tip(id("ae-tout"), "Cut the end of the region at the playhead");

    // ---- layout
    const Rect ruler{r.x + 16, r.y + 38, r.w - 32, 16};
    const Rect wave{r.x + 16, ruler.bottom() + 2, r.w - 32, std::max(40.f, r.bottom() - ruler.bottom() - 14)};
    m_aeWave = wave;
    if (wave.w < 80 || wave.h < 40) return;
    // ---- zoom and scroll: m_aeScroll is the part of the region (0..1) at the left edge, 1 / m_aeZoom of it is visible
    if (const int key = ti * 100000 + ri; key != m_aeKey) { m_aeKey = key; m_aeZoom = 1.0; m_aeScroll = 0.0; }
    auto zoomTo = [&](double z, double anchorX) {   // keeps the point under anchorX where it is
        const double visOld = 1.0 / m_aeZoom, a = m_aeScroll + double(anchorX - wave.x) / double(wave.w) * visOld;
        m_aeZoom = std::clamp(z, 1.0, 64.0);
        const double visNew = 1.0 / m_aeZoom;
        m_aeScroll = a - double(anchorX - wave.x) / double(wave.w) * visNew;
    };
    if (g.button(id("ae-zi"), {r.right() - 392, r.y + 4, 24, 24}, "+")) zoomTo(m_aeZoom * 2, wave.cx());
    if (g.button(id("ae-zo"), {r.right() - 420, r.y + 4, 24, 24}, "-")) zoomTo(m_aeZoom / 2, wave.cx());
    g.tip(id("ae-zi"), "Zoom in (Ctrl + wheel)");
    g.tip(id("ae-zo"), "Zoom out (Ctrl + wheel)");
    if (wave.contains(g.mouseX(), g.mouseY()) && g.wheelY() != 0) {
        if (g.ctrl()) { zoomTo(m_aeZoom * (g.wheelY() > 0 ? 1.25 : 0.8), g.mouseX()); g.consumeWheel(); }
        else if (m_aeZoom > 1.0) { m_aeScroll -= double(g.wheelY()) * 0.1 / m_aeZoom; g.consumeWheel(); }
    }
    const double vis = 1.0 / m_aeZoom;
    m_aeScroll = std::clamp(m_aeScroll, 0.0, 1.0 - vis);
    const double s0 = m_aeScroll;
    dl.rrect(wave, 4, bg0);
    dl.pushClip(wave);
    const float cy = wave.cy(), amp = wave.h * 0.5f - 6;
    dl.hline(wave.x, wave.right(), cy, line);

    // ---- ruler: a tick every 0.1 / 0.2 / 0.5 / 1 / 2 / 5 / 10 ... seconds, at least 70 px apart
    const double secs = frames / sr;
    const double tSpan = secs * vis, t0 = secs * s0;   // the seconds that are visible
    double stepS = 0.1;
    for (const double cand : {0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 30.0, 60.0, 120.0, 300.0}) { stepS = cand; if (cand / tSpan * wave.w >= 70.0) break; }
    dl.popClip();
    for (double t = std::ceil(t0 / stepS - 1e-9) * stepS; t <= t0 + tSpan + 1e-9; t += stepS) {
        const float x = wave.x + float((t - t0) / tSpan) * wave.w;
        dl.vline(x, ruler.y + 10, ruler.bottom(), line);
        const double tl = std::fabs(t) < 1e-9 ? 0.0 : t;   // never "-0 s"
        std::snprintf(b, sizeof b, stepS < 0.1 ? "%.2f s" : stepS < 1 ? "%.1f s" : "%.0f s", tl);
        dl.text(x + 3, ruler.y, b, textMute, small);
    }
    dl.pushClip(wave);

    // ---- waveform: dim = the audio itself, bright = what is heard (gain and fades applied)
    const auto& peaks = rg.audio->peaks;
    const float gainLin = std::pow(10.f, rg.gainDb / 20.f);
    const Color dim = scaleAlpha(col, 0.27f), lit = lighten(col, 0.35f);
    for (int px = 0; px < int(wave.w); ++px) {
        const double fa = s0 + double(px) / wave.w * vis, fb = s0 + double(px + 1) / wave.w * vis;   // the part of the region under this pixel column
        const double f0 = double(rg.offset) + fa * frames, f1 = double(rg.offset) + fb * frames;
        size_t i0 = size_t(std::max(0.0, f0) / ::kPeakStride), i1 = size_t(std::max(0.0, f1) / ::kPeakStride);
        if (i0 >= peaks.size()) break;
        i1 = std::min(i1, peaks.size() - 1);
        float pk = 0;
        for (size_t i = i0; i <= i1; ++i) pk = std::max(pk, peaks[i]);
        const float shape = std::pow(std::min(pk, 1.f), 0.8f);
        const float env = dsp::fadeGain(std::int64_t((fa + fb) * 0.5 * frames), rg.frames, rg.fadeIn, rg.fadeOut);
        const float full = std::min(amp, shape * amp), heard = std::min(amp, shape * amp * gainLin * env);
        if (full > 0.3f) dl.rect({wave.x + float(px), cy - full, 1, std::max(1.f, full * 2)}, dim);
        if (heard > 0.3f) dl.rect({wave.x + float(px), cy - heard, 1, std::max(1.f, heard * 2)}, lit);
    }

    // ---- fade curves and their handles
    const float fiW = float(std::min(double(rg.fadeIn), frames) / frames / vis) * wave.w, foW = float(std::min(double(rg.fadeOut), frames) / frames / vis) * wave.w;
    const float x0 = wave.x - float(s0 / vis) * wave.w, x1 = wave.x + float((1.0 - s0) / vis) * wave.w;   // where the region starts and ends on the screen
    const float topY = wave.y + 4, botY = wave.bottom() - 4;
    auto curve = [&](float x0, float w, bool in) {
        if (w < 1.f) return;
        std::vector<Vec2> pts;
        for (int k = 0; k <= 24; ++k) {
            const float t = float(k) / 24.f;
            const float gn = in ? dsp::fadeGain(std::int64_t(t * 1000), 1000, 1000, 0) : dsp::fadeGain(std::int64_t((1.f - t) * 1000), 1000, 0, 1000);
            pts.push_back({x0 + t * w, botY - gn * (botY - topY)});
        }
        dl.polyline(pts, 1.5f, accent);
        for (int k = 0; k < 24; ++k) {   // the part of the level that the fade takes away
            const float xa = pts[size_t(k)].x, xb = pts[size_t(k) + 1].x;
            dl.rect({xa, topY, std::max(1.f, xb - xa), (pts[size_t(k)].y + pts[size_t(k) + 1].y) * 0.5f - topY}, scaleAlpha(accent, 0.13f));
        }
    };
    curve(x0, fiW, true);
    curve(x1 - foW, foW, false);
    const float inX = x0 + fiW, outX = x1 - foW;
    float gain = rg.gainDb;
    core::i64 fi = rg.fadeIn, fo = rg.fadeOut;
    bool drag = false, first = false;
    struct Handle { const char* name; float x; bool in; };
    for (const Handle& h : {Handle{"ae-fi", inX, true}, Handle{"ae-fo", outX, false}}) {
        if (h.x < wave.x - 1 || h.x > wave.right() + 1) continue;   // zoomed in: this end of the fade is out of view (scroll to it)
        const float hx = std::clamp(h.x, wave.x + 7, wave.right() - 7);   // a handle at the very edge stays fully visible and can be grabbed
        const Rect hit{hx - 9, topY - 7, 18, 18};
        const Interact it = g.interact(id(h.name), hit);
        const bool on = it.hovered || g.isActive(id(h.name));
        dl.circle(hx, topY + 2, on ? 6.5f : 5.5f, on ? accent : mix(accent, bg0, 0.25f));
        if (it.hovered || it.held) g.setCursor(Cursor::ResizeH);
        if (it.doubleClicked) { if (h.in) fi = 0; else fo = 0; drag = true; first = true; }
        else if (it.held) {
            if (it.pressed) m_aeFade = double(h.in ? rg.fadeIn : rg.fadeOut);   // the drag moves the fade by what the mouse moves: no jump when grabbing
            const core::i64 msF = core::i64(sr / 1000.0);
            m_aeFade += double(h.in ? it.dx : -it.dx) / double(wave.w) * frames * vis;
            m_aeFade = std::clamp(m_aeFade, 0.0, frames);
            const core::i64 want = core::i64(std::llround(m_aeFade / double(msF))) * msF;   // whole milliseconds
            if (h.in) fi = std::min<core::i64>(want, rg.frames - std::min<core::i64>(fo, rg.frames));
            else fo = std::min<core::i64>(want, rg.frames - std::min<core::i64>(fi, rg.frames));
            fi = std::max<core::i64>(fi, 0); fo = std::max<core::i64>(fo, 0);
            drag = true; first = it.pressed;
        }
        if (it.pressed) first = true;
    }
    if (drag && (fi != rg.fadeIn || fo != rg.fadeOut || first)) {
        if (first) m_s.checkpoint();   // every grab of a handle is an undo step of its own, even right after another one
        m_s.setRegionAudio(ti, ri, gain, fi, fo, false);
    }

    // ---- the playhead, and a click in the waveform moves it
    const double beats = m_s.regionBeats(rg);
    const double pos = m_s.positionBeats();
    if (pos >= rg.start && pos <= rg.start + beats && beats > 0) {
        const float px = wave.x + float(((pos - rg.start) / beats - s0) / vis) * wave.w;
        if (px >= wave.x && px <= wave.right()) dl.rect({px, wave.y, 1.5f, wave.h}, rgb(0xff, 0xd0, 0x40));
    }
    const Rect seekArea{wave.x, wave.y + 14, wave.w, wave.h - 14};
    const Interact sk = g.interact(id("ae-seek"), seekArea, false);
    if (sk.hovered) g.setCursor(Cursor::IBeam);
    if (sk.pressed && beats > 0) m_s.seekBeats(rg.start + std::clamp(s0 + double(sk.mx - wave.x) / double(wave.w) * vis, 0.0, 1.0) * beats);
    dl.popClip();
    dl.textIn({wave.x, wave.bottom() + 1, wave.w, 12}, tr("Drag the round handles to set the fades. Double-click a handle to remove its fade. Click the waveform to move the playhead. Ctrl + wheel zooms, the wheel scrolls."), textMute, small);
}

// --------------------------------------------------------------------------------------------------------- piano roll
// Modelled on the MIDI editors of Logic Pro and Ableton Live: a real piano on the left that plays when you click it, notes coloured
// by velocity (Logic), a region bar you can drag to resize the region, a Grid menu with Auto (Live), a Quantize button, a Functions
// menu with the Transform tools (Stretch, Strum, Double Pattern ...), the chord of the selected notes, a velocity lane you can paint,
// two zoom sliders, Fold and Scale highlighting (Live), chord stamps and ghost notes (FL Studio), and the usual keys:
// B draw mode, Q quantize, arrows nudge / transpose, Ctrl+D duplicate.

namespace {
Color velocityColor(int v) {   // Logic Pro: blue (soft) - green - yellow - red (hard)
    const float t = std::clamp(float(v - 1) / 126.f, 0.f, 1.f);
    const Color a = rgb(0x4a, 0xa3, 0xff), b = rgb(0x3d, 0xdc, 0x84), c = rgb(0xf2, 0xd0, 0x3c), d = rgb(0xff, 0x4d, 0x4d);
    if (t < 0.4f) return mix(a, b, t / 0.4f);
    if (t < 0.75f) return mix(b, c, (t - 0.4f) / 0.35f);
    return mix(c, d, (t - 0.75f) / 0.25f);
}
}  // namespace

void Studio::applyToNotes(const std::function<std::vector<core::Note>(const std::vector<core::Note>&)>& fn) {
    if (!hasMidiRegion()) return;
    const core::Region& rg = *region();
    std::vector<core::Note> picked, rest;
    for (size_t i = 0; i < rg.notes.size(); ++i) (m_prSel.empty() || m_prSel.count(int(i)) ? picked : rest).push_back(rg.notes[i]);
    std::vector<core::Note> out = rest;
    for (const core::Note& n : fn(picked)) out.push_back(n);
    std::stable_sort(out.begin(), out.end(), [](const core::Note& x, const core::Note& y) { return x.start < y.start; });
    m_prSel.clear();
    m_s.setRegionNotes(m_selTrack, m_selRegion, out);
}

void Studio::drawPianoRoll(const Rect& r) {
    Gui& g = *m_g;
    DrawList& dl = *m_dl;
    const core::Region& rg = *region();
    const int ti = m_selTrack, ri = m_selRegion;
    const core::Track& tr_ = m_tracks[size_t(ti)];
    const Color col = trackCol(tr_);
    const double beats = m_s.regionBeats(rg);
    const double grid1 = prGridBeats();
    if (g.hovering(r)) m_prFocus = true;

    // ------------------------------------------------------------------------------------------ toolbar row 1
    const Rect bar1{r.x, r.y, r.w, 30}, bar2{r.x, r.y + 30, r.w, 28};
    dl.rect({r.x, r.y, r.w, 58}, bg2);
    dl.hline(r.x, r.right(), bar2.bottom() - 1, line);
    float x = r.x + 10;
    auto slot = [&](float w, const Rect& bar) { Rect q{x, bar.y + 4, w, bar.h - 8}; x += w + 5; return q; };
    {
        const Gui::IconFn icons_[3] = {icons::pointer, icons::pencil, icons::eraser};
        const Tool tools[3] = {Tool::Pointer, Tool::Pencil, Tool::Eraser};
        const char* tips[3] = {"Pointer (1)", "Draw mode (2 or B): click and drag to write notes", "Eraser (4): drag over notes to delete them"};
        for (int i = 0; i < 3; ++i)
            if (g.iconButton(id("pr-tool", i), slot(28, bar1), icons_[i], tips[i], m_tool == tools[i] || (i == 0 && m_tool != Tool::Pencil && m_tool != Tool::Eraser), accent)) m_tool = tools[i];
        x += 6;
        std::vector<std::string> names(std::begin(kGridNames), std::end(kGridNames));
        int gi = m_prGridIdx;
        if (g.dropdown(id("pr-grid"), slot(92, bar1), gi, names)) m_prGridIdx = gi;
        g.tip(id("pr-grid"), "Grid of the piano roll. Auto follows the zoom (Ableton Live).");
        g.toggle(id("pr-snap"), slot(44, bar1), "Snap", m_prSnap, accent);
        x += 6;
        int qi = m_prQuantIdx;
        std::vector<std::string> qn = {"1/4", "1/8", "1/16", "1/32", "1/8 Triplet", "1/16 Triplet"};
        const double qv[] = {1, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0};
        // Quantize with the value next to it; the selection if there is one, else the whole region (Q)
        auto quantize = [&] {
            const double q = qv[std::clamp(m_prQuantIdx, 0, 5)];
            applyToNotes([q](const std::vector<core::Note>& n) { std::vector<core::Note> o = n; for (core::Note& k : o) k.start = std::max(0.0, std::round(k.start / q) * q); return o; });
            message("Quantized to " + qn[size_t(std::clamp(m_prQuantIdx, 0, 5))]);
        };
        if (g.button(id("pr-quant"), slot(74, bar1), "Quantize", ButtonKind::Normal)) quantize();
        g.tip(id("pr-quant"), "Snap the notes to the grid value beside it (Q)");
        if (g.dropdown(id("pr-qval"), slot(86, bar1), qi, qn)) m_prQuantIdx = qi;
        x += 6;
        if (g.button(id("pr-fn"), slot(92, bar1), "Functions v", ButtonKind::Normal)) {
            std::vector<MenuItem> items = itemsFor({"Scale Quantize...", "Humanize...", "Legato", "Fixed Note Length...", "Transpose...", "Crescendo / Decrescendo...", "Scale Velocity...", "-",
                                                    "Stretch Notes x2", "Stretch Notes /2", "Strum Notes Down", "Strum Notes Up", "Double Pattern", "Retrograde", "Invert Pitches", "Fix Overlaps", "Remove Duplicate Notes", "-",
                                                    "Chord Trigger...", "Arpeggiator...", "-", "Logical Editor...", "Make Groove Template", "Apply Groove Template..."});
            g.openMenu(x - 97, bar1.bottom() + 24, std::move(items));
        }
        g.tip(id("pr-fn"), "Transform tools: stretch, strum, double the pattern, legato, humanize ...");
        if (g.button(id("pr-riff"), slot(70, bar1), "Riff...", ButtonKind::Accent)) dlgRiff();
        g.tip(id("pr-riff"), "Riff Machine (Ctrl+R)");
        // the chord of the selection, or of what sounds at the playhead
        {
            std::vector<int> pitches;
            if (m_prSel.size() >= 2) { for (int s : m_prSel) if (s < int(rg.notes.size())) pitches.push_back(rg.notes[size_t(s)].pitch); }
            else { const double rel = m_s.positionBeats() - rg.start; for (const core::Note& n : rg.notes) if (rel >= n.start - 1e-6 && rel < n.start + n.length - 1e-6) pitches.push_back(n.pitch); }
            const std::string chord = core::detectChord(pitches);
            const Rect cr{x + 4, bar1.y + 4, 70, bar1.h - 8};
            if (!chord.empty()) {
                dl.rrect(cr, 5, rgb(0x14, 0x1c, 0x26));
                dl.rrectStroke(cr, 5, 1, withAlpha(accent, 150));
                dl.textIn(cr, chord, lcdText, bold, Align::Center);
            }
        }
        // zoom sliders (Logic Pro)
        float tz = float(std::log(m_prPpb / 16.0) / std::log(600.0 / 16.0)), tv = (m_prKeyH - 8.f) / 20.f;
        const Rect hz{r.right() - 220, bar1.y + 4, 100, bar1.h - 8}, vz{r.right() - 112, bar1.y + 4, 100, bar1.h - 8};
        if (g.slider(id("pr-zh"), hz, tz, 0.f, 1.f, 0.55f)) { const double b = m_prScroll + double(r.w * 0.4f) / m_prPpb; m_prPpb = 16.0 * std::pow(600.0 / 16.0, double(tz)); m_prScroll = std::max(0.0, b - double(r.w * 0.4f) / m_prPpb); }
        if (g.slider(id("pr-zv"), vz, tv, 0.f, 1.f, 0.3f)) m_prKeyH = 8.f + tv * 20.f;
        g.tip(id("pr-zh"), "Horizontal zoom (Ctrl + wheel)");
        g.tip(id("pr-zv"), "Vertical zoom");
        dl.textIn({hz.x - 14, bar1.y, 14, bar1.h}, "H", textMute, small, Align::Center);
        dl.textIn({vz.x - 14, bar1.y, 14, bar1.h}, "V", textMute, small, Align::Center);
        // ---- row 2: key and scale (Ableton Live), stamp (FL Studio), colours (Logic Pro)
        x = r.x + 10;
        std::vector<std::string> keys = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        std::vector<std::string> scales;
        for (int i = 0; i < int(core::ScaleParams::Count); ++i) scales.push_back(core::scaleName(i));
        dl.text(x, bar2.y + 8, tr("Key"), textMute, small); x += 26;
        int root = m_scaleRoot, type = m_scaleType;
        if (g.dropdown(id("pr-root"), slot(54, bar2), root, keys)) { m_scaleRoot = root; m_host.settings["scaleRoot"] = std::to_string(root); }
        if (g.dropdown(id("pr-scale"), slot(128, bar2), type, scales)) { m_scaleType = type; m_host.settings["scaleType"] = std::to_string(type); }
        g.toggle(id("pr-hl"), slot(68, bar2), "Highlight", m_scaleHighlight, accent);
        g.tip(id("pr-hl"), "Shade the keys of the scale; notes outside it get an orange outline (Ableton Live Scale Mode)");
        g.toggle(id("pr-fold"), slot(46, bar2), "Fold", m_foldNotes, accent);
        g.tip(id("pr-fold"), "Show only the rows with notes and the rows of the scale (Ctrl+F)");
        g.toggle(id("pr-snapscale"), slot(48, bar2), "Lock", m_scaleSnap, accent);
        g.tip(id("pr-snapscale"), "Lock new and moved notes to the scale");
        x += 6;
        dl.text(x, bar2.y + 8, tr("Stamp"), textMute, small); x += 38;
        std::vector<std::string> stamps = {"Off"};
        for (int i = 0; i < int(core::ChordParams::Count); ++i) stamps.push_back(core::chordName(i));
        stamps.push_back("Diatonic triad");
        stamps.push_back("Diatonic 7th");
        int si = m_stampType < 0 ? 0 : m_stampType >= 100 ? int(core::ChordParams::Count) + 1 + (m_stampType - 100) : m_stampType + 1;
        if (g.dropdown(id("pr-stamp"), slot(124, bar2), si, stamps)) m_stampType = si == 0 ? -1 : si <= int(core::ChordParams::Count) ? si - 1 : 100 + (si - int(core::ChordParams::Count) - 1);
        g.tip(id("pr-stamp"), "Click in the grid to stamp a whole chord (FL Studio)");
        if (m_stampType >= 0) { double d = m_stampInv; dl.text(x, bar2.y + 8, tr("Inv"), textMute, small); x += 22; if (g.numberField(id("pr-inv"), slot(40, bar2), d, 0, 3, 1, 0)) m_stampInv = int(d); }
        x += 6;
        g.toggle(id("pr-ghost"), slot(54, bar2), "Ghost", m_ghostNotes, accent);
        g.tip(id("pr-ghost"), "Show the notes of the other regions of this track faintly (FL Studio)");
        std::vector<std::string> modes = {"Colour: Velocity", "Colour: Track"};
        int cm = m_prColorMode;
        if (g.dropdown(id("pr-color"), slot(118, bar2), cm, modes)) m_prColorMode = cm;
        g.tip(id("pr-color"), "Notes coloured by velocity (Logic Pro) or in the colour of the track");
        // Q and B work from the keyboard
        if (m_prFocus && !g.textFocused()) {
            if (g.key('Q', 0, false, true)) quantize();
            if (g.key('B', 0, false, true)) m_tool = m_tool == Tool::Pencil ? Tool::Pointer : Tool::Pencil;
        }
    }

    // ------------------------------------------------------------------------------------------------ geometry
    const float keysW = 66, rulerH = 24, top = r.y + 58;
    const float velH = std::clamp(m_prVelH, 44.f, std::max(44.f, r.h - 58 - rulerH - 60));
    const Rect ruler{r.x + keysW, top, r.w - keysW - 12, rulerH};
    const Rect keys{r.x, top + rulerH, keysW, r.h - 58 - rulerH - velH - 2};
    const Rect grid{r.x + keysW, top + rulerH, r.w - keysW - 12, keys.h};
    const Rect vel{grid.x, grid.bottom() + 2, grid.w, velH};
    dl.rect({r.x, top, r.w, r.h - 58}, rgb(0x16, 0x19, 0x1f));
    if (grid.h < 36) { dl.textIn(grid, tr("Enlarge the bottom panel (drag its top edge, or press Shift+X) to edit notes."), textMute, normal, Align::Center); return; }

    std::vector<int> rows;
    std::set<int> used;
    for (const core::Note& n : rg.notes) used.insert(n.pitch);
    for (int p = 127; p >= 0; --p) {
        if (m_foldNotes && !used.count(p) && !core::inScale(p, m_scaleRoot, m_scaleType)) continue;
        rows.push_back(p);
    }
    std::vector<int> rowOf(128, -1);
    for (size_t i = 0; i < rows.size(); ++i) rowOf[size_t(rows[i])] = int(i);
    const float kh = m_prKeyH;
    const float totalH = float(rows.size()) * kh;
    if (g.hovering(grid) || g.hovering(keys)) {
        if (g.wheelY() != 0 && g.ctrl()) { const double b = m_prScroll + double(g.mouseX() - grid.x) / m_prPpb; m_prPpb = std::clamp(m_prPpb * (g.wheelY() > 0 ? 1.2 : 1 / 1.2), 16.0, 600.0); m_prScroll = std::max(0.0, b - double(g.mouseX() - grid.x) / m_prPpb); g.consumeWheel(); }
        else if (g.wheelY() != 0 && g.shift()) { m_prScroll = std::max(0.0, m_prScroll - double(g.wheelY()) * 60.0 / m_prPpb); g.consumeWheel(); }
        else if (g.wheelY() != 0 && g.alt()) { m_prKeyH = std::clamp(m_prKeyH + g.wheelY() * 2.f, 8.f, 28.f); g.consumeWheel(); }
        else if (g.wheelY() != 0) { m_prScrollY = std::clamp(m_prScrollY - g.wheelY() * 36.f, 0.f, std::max(0.f, totalH - grid.h)); g.consumeWheel(); }
        else if (g.wheelX() != 0) { m_prScroll = std::max(0.0, m_prScroll - double(g.wheelX()) * 60.0 / m_prPpb); g.consumeWheel(); }
    }
    if (m_prScrollY == 0 && !rg.notes.empty() && m_prDrag == PrDrag::None && !m_prMoved) {   // first time: bring the notes into view
        int hi = 0; for (const core::Note& n : rg.notes) hi = std::max(hi, n.pitch);
        if (rowOf[size_t(hi)] >= 0) m_prScrollY = std::clamp(float(rowOf[size_t(hi)]) * kh - 30.f, 0.f, std::max(0.f, totalH - grid.h));
    }
    m_prScrollY = std::clamp(m_prScrollY, 0.f, std::max(0.f, totalH - grid.h));
    auto yOfRow = [&](int row) { return grid.y + float(row) * kh - m_prScrollY; };
    auto xOfBeat = [&](double b) { return grid.x + float((b - m_prScroll) * m_prPpb); };
    auto beatOfX = [&](float x_) { return m_prScroll + double(x_ - grid.x) / m_prPpb; };
    if (m_s.isPlaying() && m_follow && m_prDrag == PrDrag::None) {   // follow the playhead
        const double rel = m_s.positionBeats() - rg.start, view = double(grid.w) / m_prPpb;
        if (rel >= 0 && rel < beats && (rel < m_prScroll || rel > m_prScroll + view * 0.94)) m_prScroll = std::max(0.0, rel - view * 0.08);
    }
    auto noteRect = [&](const core::Note& n) { const int row = rowOf[size_t(n.pitch)]; return Rect{xOfBeat(n.start), row < 0 ? -100.f : yOfRow(row) + 1, std::max(4.f, float(n.length * m_prPpb) - 1), kh - 2}; };
    auto pitchAtY = [&](float y) { const int row = int(std::floor((y - grid.y + m_prScrollY) / kh)); return row >= 0 && row < int(rows.size()) ? rows[size_t(row)] : -1; };
    auto snapPitch = [&](int p) {
        if (!m_scaleSnap || p < 0) return p;
        for (int d = 0; d < 7; ++d) { if (core::inScale(p - d, m_scaleRoot, m_scaleType)) return p - d; if (core::inScale(p + d, m_scaleRoot, m_scaleType)) return p + d; }
        return p;
    };
    auto snapB = [&](double b) { return grid1 > 0 && !g.alt() ? std::max(0.0, std::floor(b / grid1 + 1e-9) * grid1) : std::max(0.0, b); };
    auto snapNear = [&](double b) { return grid1 > 0 && !g.alt() ? std::max(0.0, std::round(b / grid1) * grid1) : std::max(0.0, b); };

    // ---------------------------------------------------------------------------------------------- grid background
    const int hoverPitch = g.hovering(grid) ? pitchAtY(g.mouseY()) : -1;
    dl.pushClip(grid);
    for (size_t i = 0; i < rows.size(); ++i) {
        const float y = yOfRow(int(i));
        if (y + kh < grid.y || y > grid.bottom()) continue;
        const int p = rows[i];
        const bool inKey = core::inScale(p, m_scaleRoot, m_scaleType);
        Color c = isBlack(p) ? rgb(0x14, 0x16, 0x1c) : rgb(0x1c, 0x1f, 0x27);
        if (m_scaleHighlight && inKey) c = mix(c, accent, p % 12 == m_scaleRoot ? 0.22f : 0.08f);
        if (p == hoverPitch) c = lighten(c, 0.06f);
        dl.rect({grid.x, y, grid.w, kh}, c);
        dl.hline(grid.x, grid.right(), y + kh - 1, p % 12 == 0 ? rgb(0x3a, 0x41, 0x50) : withAlpha(line, 150));
    }
    const double g1 = std::max(0.0625, grid1 > 0 ? grid1 : 0.25);
    for (double b = std::floor(m_prScroll / g1) * g1; xOfBeat(b) < grid.right(); b += g1) {
        const float x_ = xOfBeat(b);
        if (x_ < grid.x) continue;
        const bool bar = std::fabs(std::fmod(b, 4.0)) < 1e-6, beat = std::fabs(b - std::round(b)) < 1e-6;
        if (!bar && !beat && g1 * m_prPpb < 9) continue;
        dl.vline(x_, grid.y, grid.bottom(), bar ? rgb(0x48, 0x50, 0x62) : beat ? rgb(0x2c, 0x31, 0x3d) : rgb(0x22, 0x26, 0x30));
    }
    const float endX = xOfBeat(beats);
    dl.rect({endX, grid.y, grid.right() - endX, grid.h}, withAlpha(rgb(0, 0, 0), 130));
    if (m_ghostNotes)
        for (int k = 0; k < int(tr_.regions.size()); ++k) {
            if (k == ri || !tr_.regions[size_t(k)].midi) continue;
            const core::Region& o = tr_.regions[size_t(k)];
            for (const core::Note& n : o.notes) {
                if (rowOf[size_t(n.pitch)] < 0) continue;
                const float nx = xOfBeat(o.start + n.start - rg.start), ny = yOfRow(rowOf[size_t(n.pitch)]);
                if (nx + 4 < grid.x || nx > grid.right()) continue;
                dl.rect({nx, ny + 2, std::max(2.f, float(n.length * m_prPpb)), kh - 4}, withAlpha(col, 42));
            }
        }
    // notes
    for (size_t i = 0; i < rg.notes.size(); ++i) {
        const core::Note& n = rg.notes[i];
        if (rowOf[size_t(n.pitch)] < 0) continue;
        const Rect q = noteRect(n);
        if (q.right() < grid.x || q.x > grid.right()) continue;
        const bool sel = m_prSel.count(int(i));
        Color fill = m_prColorMode == 0 ? velocityColor(n.velocity) : mix(bg1, lighten(col, 0.1f), 0.45f + 0.55f * float(n.velocity) / 127.f);
        if (sel) fill = lighten(fill, 0.35f);
        dl.rrectGradient(q, 3, lighten(fill, 0.15f), darken(fill, 0.1f));
        dl.rrectStroke(q, 3, 1, sel ? rgb(255, 255, 255) : darken(fill, 0.5f));
        if (m_scaleHighlight && !core::inScale(n.pitch, m_scaleRoot, m_scaleType)) dl.rrectStroke(q.inset(-1), 4, 1.5f, rgb(0xff, 0x9a, 0x2e));
        if (q.w > 30 && kh >= 11) {
            dl.pushClip(q);
            dl.text(q.x + 4, q.y + (q.h - dl.lineHeight({10, false})) * 0.5f, pitchName(n.pitch), withAlpha(rgb(0, 0, 0), 190), {10, true});
            dl.popClip();
        }
    }
    if (m_prDrag == PrDrag::Rubber) {
        const Rect rb{std::min(float(m_prRubberX), g.mouseX()), std::min(float(m_prRubberY), g.mouseY()), std::fabs(g.mouseX() - float(m_prRubberX)), std::fabs(g.mouseY() - float(m_prRubberY))};
        dl.rect(rb, withAlpha(accent, 40));
        dl.rrectStroke(rb, 0, 1, accent);
    }
    {
        const double rel = m_s.positionBeats() - rg.start;
        const float px_ = xOfBeat(rel);
        if (rel >= 0 && px_ < grid.right()) dl.vline(px_, grid.y, grid.bottom(), withAlpha(playhead, 230));
    }
    dl.popClip();

    // -------------------------------------------------------------------------------------- ruler and region bar
    dl.rect(ruler, bg2);
    dl.pushClip(ruler);
    {
        const float x0 = std::max(ruler.x, xOfBeat(0)), x1 = std::min(ruler.right(), xOfBeat(beats));
        dl.rect({x0, ruler.y + 1, std::max(0.f, x1 - x0), 7}, mix(bg2, col, 0.8f));   // the region itself (Logic Pro region bar)
        for (const core::Note& n : rg.notes) { const float nx = xOfBeat(n.start); if (nx >= x0 && nx < x1) dl.rect({nx, ruler.y + 2, std::max(1.f, float(n.length * m_prPpb)), 5}, withAlpha(rgb(255, 255, 255), 90)); }
    }
    for (double b = std::floor(m_prScroll); xOfBeat(b) < ruler.right(); b += 1.0) {
        const float x_ = xOfBeat(b);
        if (x_ < ruler.x) continue;
        const bool bar = std::fabs(std::fmod(b, 4.0)) < 1e-6;
        dl.vline(x_, ruler.y + (bar ? 9 : 16), ruler.bottom(), bar ? textMute : withAlpha(textMute, 120));
        if (bar) dl.text(x_ + 4, ruler.y + 8, std::to_string(int(b / 4) + 1), textDim, small);
    }
    dl.popClip();
    {   // drag the end of the region bar to resize the region
        const float ex = xOfBeat(beats);
        const Rect handle{ex - 5, ruler.y, 10, 9};
        const Interact hi = g.interact(id("pr-region"), handle);
        if (ex > ruler.x && ex < ruler.right()) { dl.rrect({ex - 3, ruler.y + 1, 4, 7}, 2, hi.hovered || hi.held ? rgb(255, 255, 255) : lighten(col, 0.3f)); }
        if (hi.hovered || hi.held) g.setCursor(Cursor::ResizeH);
        if (hi.pressed) m_s.checkpoint();
        if (hi.held) { const double nb = std::max(1.0, std::max(1.0, snapNear(beatOfX(g.mouseX())))); m_s.resizeRegion(ti, ri, nb); m_modelDirty = true; }
        if (hi.released) m_modelDirty = true;
    }

    // ------------------------------------------------------------------------------------------- piano keys
    const Interact keyIt = g.interact(id("pr-keys"), keys, false);
    dl.pushClip(keys);
    for (size_t i = 0; i < rows.size(); ++i) {
        const float y = yOfRow(int(i));
        if (y + kh < keys.y || y > keys.bottom()) continue;
        const int p = rows[i];
        const bool inKey = core::inScale(p, m_scaleRoot, m_scaleType), black = isBlack(p);
        Color white = rgb(0xdc, 0xe0, 0xea);
        if (m_scaleHighlight && inKey && !black) white = mix(white, accent, p % 12 == m_scaleRoot ? 0.45f : 0.16f);
        const bool down = m_auditionPitch == p;
        dl.rect({keys.x, y, keys.w - 1, kh - 1}, down ? accent : white);
        if (black) {
            const Rect b{keys.x, y, keys.w * 0.62f, kh - 1};
            dl.rect(b, down ? accentDim : rgb(0x1f, 0x22, 0x2a));
            dl.hline(keys.x + b.w, keys.right() - 1, y + kh * 0.5f, withAlpha(rgb(0, 0, 0), 60));
        }
        if (p % 12 == 0 || (kh >= 15 && !black)) dl.textIn({keys.x + 3, y, keys.w - 7, kh}, pitchName(p), p % 12 == 0 ? rgb(0x30, 0x36, 0x44) : rgb(0x70, 0x78, 0x8c), {10, p % 12 == 0}, Align::Right);
    }
    dl.popClip();
    if (keyIt.pressed || keyIt.held) {   // click or glide over the keys to hear them
        const int p = pitchAtY(g.mouseY());
        if (p >= 0 && p != m_auditionPitch) auditionNote(ti, p);
        m_auditionUntil = m_time + 5.0;
    }
    if (keyIt.released && m_auditionPitch >= 0) { m_s.noteOff(m_auditionTrack, m_auditionPitch); m_auditionPitch = -1; }

    // ------------------------------------------------------------------------------------ interaction in the grid
    const Interact it = g.interact(id("pr-grid"), grid);
    const double mb = beatOfX(g.mouseX());
    int hitNote = -1;
    int edge = 0;   // 1 = right edge, 2 = left edge
    for (int i = int(rg.notes.size()) - 1; i >= 0; --i) {
        if (rowOf[size_t(rg.notes[size_t(i)].pitch)] < 0) continue;
        const Rect q = noteRect(rg.notes[size_t(i)]);
        if (q.contains(g.mouseX(), g.mouseY())) { hitNote = i; if (q.w > 14) { if (g.mouseX() >= q.right() - 6) edge = 1; else if (g.mouseX() <= q.x + 5) edge = 2; } break; }
    }
    if (it.hovered) {
        g.setCursor(hitNote >= 0 ? (edge ? Cursor::ResizeH : Cursor::Hand) : m_tool == Tool::Eraser ? Cursor::Eraser : (m_tool == Tool::Pencil || m_stampType >= 0 ? Cursor::Pencil : Cursor::Cross));
        if (hitNote >= 0) {   // the info line (Ableton Live Info View)
            const core::Note& n = rg.notes[size_t(hitNote)];
            char b[160];
            std::snprintf(b, sizeof b, "%s  -  start %.3f  -  length %.3f  -  velocity %d%s   |   drag to move, edges resize, Delete removes, Alt+drag copies", pitchName(n.pitch).c_str(), n.start, n.length, n.velocity, core::inScale(n.pitch, m_scaleRoot, m_scaleType) ? "" : "  -  outside the scale");
            setHint(b);
        } else setHint(m_stampType >= 0 ? "Click to stamp a chord - drag sideways to set its length" : m_tool == Tool::Pencil ? "Draw mode (B): click and drag to write a note - right-click deletes" : "Double-click or press B to draw notes - drag on empty space to select - Ctrl+wheel zooms");
    }
    if (it.pressed) {
        m_prMoved = false;
        if (m_tool == Tool::Eraser) { m_prDrag = PrDrag::Erase; if (hitNote >= 0) { std::vector<core::Note> notes = rg.notes; notes.erase(notes.begin() + hitNote); m_s.checkpoint(); m_s.setRegionNotes(ti, ri, notes); m_prSel.clear(); } }
        else if (hitNote >= 0) {
            if (g.shift()) { if (m_prSel.count(hitNote)) m_prSel.erase(hitNote); else m_prSel.insert(hitNote); }
            else if (!m_prSel.count(hitNote)) m_prSel = {hitNote};
            m_prOrig = rg.notes;
            m_prDragStart = mb;
            m_prDragPitch = rg.notes[size_t(hitNote)].pitch;
            m_prAnchor = hitNote;
            m_prDrag = edge == 1 ? PrDrag::Resize : edge == 2 ? PrDrag::ResizeL : PrDrag::Move;
            auditionNote(ti, rg.notes[size_t(hitNote)].pitch);
            if (g.alt() && !edge) {   // Alt-drag copies the notes (Logic Pro, Ableton Live)
                std::vector<core::Note> notes = rg.notes;
                std::set<int> add;
                for (int s : m_prSel) { notes.push_back(rg.notes[size_t(s)]); add.insert(int(notes.size()) - 1); }
                m_s.checkpoint();
                m_s.setRegionNotes(ti, ri, notes);
                m_prSel = add; m_prOrig = notes; m_prMoved = true;
            }
        } else if (it.doubleClicked || m_tool == Tool::Pencil || m_stampType >= 0) {
            const int p = snapPitch(pitchAtY(g.mouseY()));
            if (p >= 0 && mb >= 0 && mb < beats) {
                std::vector<core::Note> notes = rg.notes;
                const double start = snapB(mb), len = grid1 > 0 ? std::max(0.0625, grid1) : 0.5;
                const size_t first = notes.size();
                if (m_stampType >= 0) {
                    core::StampParams sp;
                    sp.inversion = m_stampInv; sp.keyRoot = m_scaleRoot; sp.scale = m_scaleType;
                    if (m_stampType >= 100) { sp.diatonic = true; sp.size = m_stampType == 101 ? 4 : 3; } else sp.type = m_stampType;
                    for (const core::Note& n : core::stampChord(p, start, len, 96, sp)) notes.push_back(n);
                } else notes.push_back({p, start, len, 96});
                m_s.checkpoint();
                m_s.setRegionNotes(ti, ri, notes);
                m_prSel.clear();
                for (size_t k = first; k < notes.size(); ++k) m_prSel.insert(int(k));
                m_prOrig = notes; m_prDragStart = mb; m_prDrag = PrDrag::Resize; m_prAnchor = int(first); m_prMoved = true;
                auditionNote(ti, p);
            }
        } else {
            if (!g.shift()) m_prSel.clear();
            m_prDrag = PrDrag::Rubber;
            m_prRubberX = g.mouseX(); m_prRubberY = g.mouseY();
        }
    }
    if (it.held && !rg.notes.empty()) {
        const bool valid = m_prAnchor >= 0 && m_prAnchor < int(m_prOrig.size());
        if (m_prDrag == PrDrag::Move && valid) {
            const double db = snapNear(m_prOrig[size_t(m_prAnchor)].start + (mb - m_prDragStart)) - m_prOrig[size_t(m_prAnchor)].start;
            int dp = 0;
            const int py = pitchAtY(g.mouseY());
            if (py >= 0) dp = snapPitch(py) - m_prDragPitch;
            if (!m_prMoved && (std::fabs(db) > 1e-6 || dp != 0)) { m_s.checkpoint(); m_prMoved = true; }
            if (m_prMoved) {
                std::vector<core::Note> notes = m_prOrig;
                for (int s : m_prSel) if (s >= 0 && s < int(notes.size())) { notes[size_t(s)].start = std::max(0.0, m_prOrig[size_t(s)].start + db); notes[size_t(s)].pitch = std::clamp(m_prOrig[size_t(s)].pitch + dp, 0, 127); }
                const int np = notes[size_t(m_prAnchor)].pitch;
                if (np != m_auditionPitch && dp != 0) auditionNote(ti, np);   // hear the new pitch while dragging
                m_s.setRegionNotes(ti, ri, notes);
            }
        } else if (m_prDrag == PrDrag::Resize && valid) {
            const double end = snapB(mb) + (grid1 > 0 ? grid1 : 0.0);
            const double newLen = std::max(grid1 > 0 ? grid1 : 0.0625, end - m_prOrig[size_t(m_prAnchor)].start);
            const double d = newLen - m_prOrig[size_t(m_prAnchor)].length;
            if (!m_prMoved && std::fabs(d) > 1e-6) { m_s.checkpoint(); m_prMoved = true; }
            if (m_prMoved) { std::vector<core::Note> notes = m_prOrig; for (int s : m_prSel) if (s >= 0 && s < int(notes.size())) notes[size_t(s)].length = std::max(0.0625, m_prOrig[size_t(s)].length + d); m_s.setRegionNotes(ti, ri, notes); }
            g.setCursor(Cursor::ResizeH);
        } else if (m_prDrag == PrDrag::ResizeL && valid) {
            const double ns = std::min(snapNear(mb), m_prOrig[size_t(m_prAnchor)].start + m_prOrig[size_t(m_prAnchor)].length - 0.0625);
            const double d = ns - m_prOrig[size_t(m_prAnchor)].start;
            if (!m_prMoved && std::fabs(d) > 1e-6) { m_s.checkpoint(); m_prMoved = true; }
            if (m_prMoved) { std::vector<core::Note> notes = m_prOrig; for (int s : m_prSel) if (s >= 0 && s < int(notes.size())) { notes[size_t(s)].start = std::max(0.0, m_prOrig[size_t(s)].start + d); notes[size_t(s)].length = std::max(0.0625, m_prOrig[size_t(s)].length - d); } m_s.setRegionNotes(ti, ri, notes); }
            g.setCursor(Cursor::ResizeH);
        } else if (m_prDrag == PrDrag::Erase && hitNote >= 0 && !m_modelDirty.load()) {
            std::vector<core::Note> notes = rg.notes; notes.erase(notes.begin() + hitNote); m_s.setRegionNotes(ti, ri, notes); m_modelDirty = true;
        } else if (m_prDrag == PrDrag::Rubber) {
            const Rect rb{std::min(float(m_prRubberX), g.mouseX()), std::min(float(m_prRubberY), g.mouseY()), std::fabs(g.mouseX() - float(m_prRubberX)), std::fabs(g.mouseY() - float(m_prRubberY))};
            if (!g.shift()) m_prSel.clear();
            for (size_t i = 0; i < rg.notes.size(); ++i) if (rowOf[size_t(rg.notes[i].pitch)] >= 0) { const Rect q = noteRect(rg.notes[i]).intersect(rb); if (q.w > 0 && q.h > 0) m_prSel.insert(int(i)); }
        }
    }
    if (it.released) {
        if (m_prDrag != PrDrag::None && m_prDrag != PrDrag::Rubber) m_modelDirty = true;
        m_prDrag = PrDrag::None;
        if (m_auditionPitch >= 0 && !keyIt.held) { m_s.noteOff(m_auditionTrack, m_auditionPitch); m_auditionPitch = -1; }
    }
    if (it.rightClicked && hitNote >= 0) {   // right-click deletes in every tool
        std::vector<core::Note> notes = rg.notes;
        notes.erase(notes.begin() + hitNote);
        m_s.checkpoint();
        m_s.setRegionNotes(ti, ri, notes);
        m_prSel.clear();
    }

    // ----------------------------------------------------------------------------------------------- keyboard
    if (m_prFocus && !g.textFocused()) {
        const double nudge = grid1 > 0 ? grid1 : 0.25;
        if ((g.key(KeyDelete, 0, false, true) || g.key(KeyBackspace, 0, false, true)) && !m_prSel.empty()) {
            std::vector<core::Note> notes;
            for (size_t i = 0; i < rg.notes.size(); ++i) if (!m_prSel.count(int(i))) notes.push_back(rg.notes[i]);
            m_s.checkpoint(); m_s.setRegionNotes(ti, ri, notes); m_prSel.clear();
        }
        if (g.key('A', ModCtrl, false, true)) { m_prSel.clear(); for (size_t i = 0; i < rg.notes.size(); ++i) m_prSel.insert(int(i)); }
        const bool copy = g.key('C', ModCtrl, false, true), cut = g.key('X', ModCtrl, false, true);
        if (copy || cut) {
            m_noteClip.clear();
            for (int s : m_prSel) if (s < int(rg.notes.size())) m_noteClip.push_back(rg.notes[size_t(s)]);
            message(std::to_string(m_noteClip.size()) + (cut ? " notes cut" : " notes copied"));
            if (cut && !m_prSel.empty()) { std::vector<core::Note> notes; for (size_t i = 0; i < rg.notes.size(); ++i) if (!m_prSel.count(int(i))) notes.push_back(rg.notes[i]); m_s.checkpoint(); m_s.setRegionNotes(ti, ri, notes); m_prSel.clear(); }
        }
        if (g.key('V', ModCtrl, false, true) && !m_noteClip.empty()) {
            double lo = 1e9; for (const core::Note& n : m_noteClip) lo = std::min(lo, n.start);
            const double at = std::max(0.0, std::max(m_s.positionBeats() - rg.start, 0.0));
            std::vector<core::Note> notes = rg.notes;
            m_prSel.clear();
            for (core::Note n : m_noteClip) { n.start = n.start - lo + at; m_prSel.insert(int(notes.size())); notes.push_back(n); }
            m_s.checkpoint(); m_s.setRegionNotes(ti, ri, notes);
        }
        if (g.key('D', ModCtrl, false, true) && !m_prSel.empty()) {   // duplicate the selection right behind itself (Logic Pro, Ableton Live)
            double lo = 1e9, hi = 0;
            for (int s : m_prSel) if (s < int(rg.notes.size())) { lo = std::min(lo, rg.notes[size_t(s)].start); hi = std::max(hi, rg.notes[size_t(s)].start + rg.notes[size_t(s)].length); }
            std::vector<core::Note> notes = rg.notes;
            std::set<int> sel;
            for (int s : m_prSel) if (s < int(rg.notes.size())) { core::Note n = rg.notes[size_t(s)]; n.start += hi - lo; sel.insert(int(notes.size())); notes.push_back(n); }
            m_s.checkpoint(); m_s.setRegionNotes(ti, ri, notes); m_prSel = sel;
        }
        if (g.key(KeyEscape, 0, false, true)) m_prSel.clear();
        if (!m_prSel.empty()) {
            auto move = [&](double dt, int dp) {
                std::vector<core::Note> notes = rg.notes;
                for (int s : m_prSel) if (s < int(notes.size())) { notes[size_t(s)].start = std::max(0.0, notes[size_t(s)].start + dt); notes[size_t(s)].pitch = std::clamp(notes[size_t(s)].pitch + dp, 0, 127); }
                m_s.checkpoint(); m_s.setRegionNotes(ti, ri, notes);
                if (dp) auditionNote(ti, notes[size_t(*m_prSel.begin())].pitch);
            };
            if (g.key(KeyLeft, 0, true, true)) move(-nudge, 0);
            if (g.key(KeyRight, 0, true, true)) move(nudge, 0);
            if (g.key(KeyUp, 0, true, true)) move(0, 1);
            if (g.key(KeyDown, 0, true, true)) move(0, -1);
            if (g.key(KeyUp, ModShift, true, true)) move(0, 12);
            if (g.key(KeyDown, ModShift, true, true)) move(0, -12);
        }
    }

    // ------------------------------------------------------------------------------------------ velocity lane
    splitter("prvel", {grid.x, grid.bottom() - 1, grid.w, 5}, m_prVelH, false, 44, 220, -1);
    dl.rect({r.x, vel.y, r.w, vel.h}, rgb(0x14, 0x17, 0x1c));
    dl.hline(r.x, r.right(), vel.y - 1, line);
    dl.text(r.x + 8, vel.y + 6, tr("Velocity"), textMute, small);
    dl.text(r.x + 8, vel.y + 20, std::to_string(m_prSel.empty() ? int(rg.notes.size()) : int(m_prSel.size())) + (m_prSel.empty() ? " notes" : " selected"), textMute, {10, false});
    dl.pushClip(vel);
    for (int q : {32, 64, 96, 127}) dl.hline(vel.x, vel.right(), vel.bottom() - 4 - (vel.h - 10) * float(q) / 127.f, rgb(0x24, 0x28, 0x32));
    for (size_t i = 0; i < rg.notes.size(); ++i) {
        const core::Note& n = rg.notes[i];
        const float x_ = xOfBeat(n.start);
        if (x_ < vel.x - 4 || x_ > vel.right()) continue;
        const float h = (vel.h - 10) * float(n.velocity) / 127.f;
        const bool sel = m_prSel.count(int(i));
        const Color c = m_prColorMode == 0 ? velocityColor(n.velocity) : lighten(col, 0.15f);
        dl.rect({x_, vel.bottom() - 4 - h, 3, h}, sel ? rgb(255, 255, 255) : c);
        dl.circle(x_ + 1.5f, vel.bottom() - 4 - h, 3.5f, sel ? rgb(255, 255, 255) : lighten(c, 0.25f));
    }
    dl.popClip();
    const Interact vi = g.interact(id("pr-vel"), vel);
    if (vi.pressed) { m_s.checkpoint(); m_prLastVelX = g.mouseX(); m_stepVelDrag = 1; }
    if (vi.held && m_stepVelDrag >= 0) {   // paint velocities: every note the pointer passes over takes the height of the pointer (Ableton Live, FL Studio)
        const float a = std::min(m_prLastVelX, g.mouseX()) - 4, b = std::max(m_prLastVelX, g.mouseX()) + 4;
        const int v = std::clamp(int(std::lround((vel.bottom() - 4 - g.mouseY()) / (vel.h - 10) * 127.f)), 1, 127);
        std::vector<core::Note> notes = rg.notes;
        bool changed = false;
        for (size_t i = 0; i < notes.size(); ++i) {
            const float x_ = xOfBeat(notes[i].start) + 1.5f;
            const bool inSpan = x_ >= a && x_ <= b, selHit = m_prSel.count(int(i)) && std::fabs(x_ - g.mouseX()) < 6;
            if ((inSpan && (m_prSel.empty() || m_prSel.count(int(i)) || true)) || selHit) { if (notes[i].velocity != v) { notes[i].velocity = v; changed = true; } }
        }
        if (changed) m_s.setRegionNotes(ti, ri, notes);
        m_prLastVelX = g.mouseX();
        g.setCursor(Cursor::Pencil);
    }
    if (vi.released) { m_stepVelDrag = -1; m_modelDirty = true; }
    if (vi.hovered) setHint("Drag across the notes to paint their velocity");

    float sx = float(m_prScroll);
    g.scrollBar(id("pr-hs"), {grid.x, r.bottom() - 8, grid.w, 8}, sx, float(beats + 4), float(grid.w / m_prPpb), false);
    m_prScroll = sx;
    g.scrollBar(id("pr-vs"), {grid.right(), grid.y, 10, grid.h}, m_prScrollY, totalH, grid.h, true);
}

// ------------------------------------------------------------------------------------------------------ step sequencer

void Studio::drawSteps(const Rect& r) {
    Gui& g = *m_g;
    DrawList& dl = *m_dl;
    if (m_selTrack < 0 || m_selTrack >= int(m_tracks.size()) || m_tracks[size_t(m_selTrack)].type != core::TrackType::Drums) {
        dl.textIn(r, tr("Select a Drum Machine track to program its steps."), textMute, normal, Align::Center);
        return;
    }
    const core::DrumPattern pat = m_s.drumPattern(m_selTrack);
    static const char* names[::kDrumRows] = {"Kick", "Snare", "Hi-Hat", "Clap"};
    static const Color colors[::kDrumRows] = {rgb(0x5b, 0x8c, 0xff), rgb(0xa5, 0x5a, 0xe1), rgb(0x28, 0xc8, 0xaa), rgb(0xe1, 0x50, 0xaa)};
    const float lw = 90, pad = 16, top = r.y + 30;
    const float cw = (r.w - lw - 2 * pad) / float(::kSteps), ch = std::min(52.f, (r.h - 50) / float(::kDrumRows));
    dl.text(r.x + pad, r.y + 8, tr("Drum Machine - click the steps, drag to paint"), textDim, small);
    // beat of the sequencer for the running light
    const double pos = m_s.positionBeats();
    const int cur = m_s.isPlaying() ? int(std::fmod(pos * 4.0, double(::kSteps))) : -1;
    const Id gid = id("steps");
    const Rect cells{r.x + pad + lw, top, cw * ::kSteps, ch * ::kDrumRows};
    const Interact it = g.interact(gid, cells);
    static bool paintValue = true;
    static int lastCell = -1;
    int hr = -1, hs = -1;
    if (cells.contains(g.mouseX(), g.mouseY())) { hs = int((g.mouseX() - cells.x) / cw); hr = int((g.mouseY() - cells.y) / ch); }
    if (it.pressed && hr >= 0) { paintValue = !pat[size_t(hr)][size_t(hs)]; lastCell = -1; }
    if ((it.pressed || it.held) && hr >= 0 && hs >= 0 && hr * 100 + hs != lastCell) {
        lastCell = hr * 100 + hs;
        if (pat[size_t(hr)][size_t(hs)] != paintValue) m_s.setStep(m_selTrack, hr, hs, paintValue);
    }
    if (it.released) lastCell = -1;
    for (int row = 0; row < ::kDrumRows; ++row) {
        const float y = top + float(row) * ch;
        dl.textIn({r.x + pad, y, lw - 8, ch}, tr(names[row]), text, bold);
        for (int s = 0; s < ::kSteps; ++s) {
            const Rect c{cells.x + float(s) * cw + 2, y + 2, cw - 4, ch - 4};
            const bool on = pat[size_t(row)][size_t(s)];
            const bool beatStart = s % 4 == 0;
            Color base = beatStart ? rgb(0x2a, 0x2f, 0x3a) : rgb(0x22, 0x26, 0x30);
            if (hr == row && hs == s) base = lighten(base, 0.1f);
            dl.rrect(c, 5, on ? colors[row] : base);
            if (on) dl.rrectGradient(c, 5, lighten(colors[row], 0.25f), colors[row]);
            if (s == cur) dl.rrectStroke(c, 5, 2, rgb(255, 255, 255));
        }
    }
    if (it.hovered) g.setCursor(Cursor::Pencil);
}

// -------------------------------------------------------------------------------------------------------- automation

void Studio::drawAutomation(const Rect& r) {
    Gui& g = *m_g;
    DrawList& dl = *m_dl;
    const bool tempoLane = m_autoParam == 4;   // the project tempo curve (Logic Pro / Cubase tempo track)
    if (!tempoLane && (m_selTrack < 0 || m_selTrack >= int(m_tracks.size()))) {
        // the parameter menu is still needed to reach the tempo lane
        const Rect bar0{r.x, r.y, r.w, 34};
        dl.rect(bar0, bg2);
        dl.text(r.x + 12, r.y + 10, tr("Parameter"), textMute, small);
        int p0 = m_autoParam;
        if (g.dropdown(id("auto-param"), {r.x + 80, r.y + 6, 130, 22}, p0, {"Volume", "Pan", "Send A (Reverb)", "Send B (Echo)", "Tempo (BPM)"})) m_autoParam = p0;
        dl.textIn({r.x, r.y + 34, r.w, r.h - 34}, tr("Select a track to automate its volume, pan or sends."), textMute, normal, Align::Center);
        return;
    }
    const core::Track* trk = tempoLane ? nullptr : &m_tracks[size_t(m_selTrack)];
    const core::AutoParam param = core::AutoParam(std::clamp(m_autoParam, 0, 3));
    const std::pair<float, float> range = tempoLane ? std::pair<float, float>{20.f, 300.f} : core::autoRange(param);
    const Rect bar{r.x, r.y, r.w, 34};
    dl.rect(bar, bg2);
    dl.hline(bar.x, bar.right(), bar.bottom() - 1, line);
    int pi = m_autoParam;
    dl.text(r.x + 12, r.y + 10, tr("Parameter"), textMute, small);
    if (g.dropdown(id("auto-param"), {r.x + 80, r.y + 6, 130, 22}, pi, {"Volume", "Pan", "Send A (Reverb)", "Send B (Echo)", "Tempo (BPM)"})) m_autoParam = pi;
    dl.text(r.x + 228, r.y + 10, tr(tempoLane ? "Click to add a tempo change - drag to move - double-click a point to ramp to the next - right-click to delete"
                                              : "Click to add a point - drag to move - right-click to delete - the line is followed during playback"), textMute, small);
    const auto tm = m_tempo ? m_tempo : m_s.tempoMap();
    // pts: (beat, value); tempoRamp: the 'ramp' flag of each tempo point
    std::vector<core::AutoPoint> pts;
    std::vector<char> tempoRamp;
    if (tempoLane) for (const auto& tp : tm->tempoPoints()) { pts.push_back({tp.beat, float(tp.bpm)}); tempoRamp.push_back(tp.ramp ? 1 : 0); }
    else pts = trk->autoOf(param);
    auto store = [&](const std::vector<core::AutoPoint>& np, const std::vector<char>& ramps) {
        if (!tempoLane) { m_s.setAuto(m_selTrack, param, np); return; }
        std::vector<dsp::TempoPoint> out;
        for (size_t k = 0; k < np.size(); ++k) out.push_back({np[k].beat, double(np[k].value), k < ramps.size() && ramps[k] != 0});
        m_s.setTempoPoints(out, false);
    };
    if (g.button(id("auto-clear"), {r.right() - 100, r.y + 6, 88, 22}, "Clear Lane")) {
        m_s.checkpoint();
        if (tempoLane) m_s.setTempoPoints({{0, tm->bpmAt(0), false}}, false); else m_s.setAuto(m_selTrack, param, {});
        m_modelDirty = true;
        return;
    }
    const Rect lane{r.x + 40, bar.bottom() + 8, r.w - 54, r.h - bar.h - 28};
    m_autoLane = lane;
    dl.rrect(lane, 6, rgb(0x14, 0x17, 0x1c));
    dl.pushClip(lane);
    const double view = double(m_rLanes.w) / m_ppb;
    auto xOf = [&](double b) { return lane.x + float((b - m_scrollX) / view * lane.w); };
    auto yOf = [&](float v) { return lane.bottom() - 6 - (lane.h - 12) * (v - range.first) / (range.second - range.first); };
    auto valOf = [&](float y) { return std::clamp(range.first + (lane.bottom() - 6 - y) / (lane.h - 12) * (range.second - range.first), range.first, range.second); };
    for (int i = 0; i <= 4; ++i) dl.hline(lane.x, lane.right(), lane.y + 6 + (lane.h - 12) * float(i) / 4.f, rgb(0x24, 0x28, 0x32));
    for (int bar_ = std::max(1, tm->barAt(m_scrollX)); xOf(tm->barStart(bar_)) < lane.right(); ++bar_) dl.vline(xOf(tm->barStart(bar_)), lane.y, lane.bottom(), rgb(0x2a, 0x2f, 0x3a));
    const float base = trk ? trk->baseValue(param) : 0.f;
    if (pts.empty()) dl.hline(lane.x, lane.right(), yOf(base), withAlpha(accent, 120));
    else {
        std::vector<Vec2> poly;
        poly.push_back({lane.x, yOf(pts.front().value)});
        for (size_t k = 0; k < pts.size(); ++k) {
            // a tempo curve is a staircase unless the previous point ramps to this one
            if (tempoLane && k > 0 && !tempoRamp[k - 1]) poly.push_back({xOf(pts[k].beat), yOf(pts[k - 1].value)});
            poly.push_back({xOf(pts[k].beat), yOf(pts[k].value)});
        }
        poly.push_back({lane.right(), yOf(pts.back().value)});
        dl.polyline(poly, 2.f, accent);
        for (const auto& p : pts) { dl.circle(xOf(p.beat), yOf(p.value), 5, rgb(255, 255, 255)); dl.circle(xOf(p.beat), yOf(p.value), 3.5f, accent); }
    }
    if (tempoLane) for (size_t k = 0; k < pts.size(); ++k) {   // the value next to each point
        char buf[16]; std::snprintf(buf, sizeof buf, "%.1f", pts[k].value);
        dl.text(xOf(pts[k].beat) + 8, yOf(pts[k].value) - 16, buf, textDim, small);
    }
    const float px = xOf(m_s.positionBeats());
    if (px >= lane.x && px <= lane.right()) dl.vline(px, lane.y, lane.bottom(), withAlpha(playhead, 200));
    dl.popClip();
    // interaction
    const Interact it = g.interact(id("auto-lane"), lane);
    auto beatAt = [&](float x) { return snapBeat(m_scrollX + double(x - lane.x) / lane.w * view); };
    auto valueAt = [&](float y) { const float v = valOf(y); return tempoLane ? std::round(v * 10.f) / 10.f : v; };
    int hit = -1;
    for (size_t i = 0; i < pts.size(); ++i) if (std::fabs(xOf(pts[i].beat) - g.mouseX()) < 8 && std::fabs(yOf(pts[i].value) - g.mouseY()) < 8) hit = int(i);
    if (it.hovered) g.setCursor(hit >= 0 ? Cursor::Move : Cursor::Pencil);
    if (it.rightClicked && hit >= 0) {
        if (!(tempoLane && (hit == 0 || pts.size() < 2))) {   // the first tempo point always stays
            m_s.checkpoint();
            pts.erase(pts.begin() + hit);
            if (tempoLane) tempoRamp.erase(tempoRamp.begin() + hit);
            store(pts, tempoRamp);
            m_modelDirty = true;
        }
    } else if (it.doubleClicked && tempoLane && hit >= 0 && size_t(hit) + 1 < pts.size()) {
        m_s.checkpoint();
        tempoRamp[size_t(hit)] = tempoRamp[size_t(hit)] ? 0 : 1;
        store(pts, tempoRamp);
        m_modelDirty = true;
        m_autoDragPoint = -1;
    } else if (it.pressed) {
        if (hit >= 0) { m_autoDragPoint = hit; m_s.checkpoint(); }
        else {
            core::AutoPoint np{std::max(0.0, beatAt(g.mouseX())), valueAt(g.mouseY())};
            if (tempoLane) {   // never two tempo points at the same place
                bool clash = false;
                for (const auto& p : pts) if (std::fabs(p.beat - np.beat) < 1e-6) clash = true;
                if (clash) return;
            }
            std::vector<std::pair<core::AutoPoint, char>> tmp;
            for (size_t k = 0; k < pts.size(); ++k) tmp.push_back({pts[k], tempoLane ? tempoRamp[k] : char(0)});
            tmp.push_back({np, 0});
            std::stable_sort(tmp.begin(), tmp.end(), [](const auto& a, const auto& b) { return a.first.beat < b.first.beat; });
            pts.clear(); tempoRamp.clear();
            for (const auto& q : tmp) { pts.push_back(q.first); tempoRamp.push_back(q.second); }
            m_s.checkpoint();
            store(pts, tempoRamp);
            for (size_t i = 0; i < pts.size(); ++i) if (pts[i].beat == np.beat && pts[i].value == np.value) m_autoDragPoint = int(i);
            m_modelDirty = true;
        }
    }
    if (it.held && m_autoDragPoint >= 0 && m_autoDragPoint < int(pts.size())) {
        double b = std::max(0.0, beatAt(g.mouseX()));
        if (tempoLane) {   // keep the order of the tempo points: the first stays at beat 0
            const size_t k = size_t(m_autoDragPoint);
            if (k == 0) b = 0;
            else {
                b = std::max(b, pts[k - 1].beat + 0.05);
                if (k + 1 < pts.size()) b = std::min(b, pts[k + 1].beat - 0.05);
            }
        }
        pts[size_t(m_autoDragPoint)] = {b, valueAt(g.mouseY())};
        store(pts, tempoRamp);
        m_modelDirty = true;
    }
    if (it.released) m_autoDragPoint = -1;
}

// ------------------------------------------------------------------------------------------------------------ Drummer

namespace {
const char* drumName(int pitch) {
    switch (pitch) {
        case 35: case 36: return "Kick";
        case 37: return "Side Stick";
        case 38: case 40: return "Snare";
        case 39: return "Clap";
        case 41: case 43: case 45: case 47: case 48: case 50: return "Tom";
        case 42: return "Closed Hat";
        case 44: return "Pedal Hat";
        case 46: return "Open Hat";
        case 49: case 57: return "Crash";
        case 51: case 59: return "Ride";
        case 53: return "Ride Bell";
        case 54: return "Tambourine";
        case 56: return "Cowbell";
        default: return "Percussion";
    }
}
}  // namespace

void Studio::drawDrummer(const Rect& r) {
    Gui& g = *m_g;
    DrawList& dl = *m_dl;
    if (!hasRegion() || !region()->drummer) {
        dl.textIn(r, tr("Select a Drummer region to shape its groove - or double-click a Drummer track's lane to create one."), textMute, normal, Align::Center);
        return;
    }
    const core::Region& rg = *region();
    dsp::DrummerParams p = rg.drum;
    // ---- controls (left)
    const float pw = 310;
    const Rect panel{r.x, r.y, pw, r.h};
    dl.rect(panel, bg2);
    dl.vline(panel.right(), r.y, r.bottom(), line);
    bool changed = false;
    float y = r.y + 12;
    const float lx = r.x + 14, cx = r.x + 108, cw = pw - 108 - 16;
    auto label = [&](const char* t) { dl.text(lx, y + 4, tr(t), textDim, small); };
    {
        std::vector<std::string> styles;
        for (int i = 0; i < dsp::drummerStyleCount(); ++i) styles.push_back(dsp::drummerStyleName(i));
        label("Style");
        int si = p.style;
        if (g.dropdown(id("drm-style"), {cx, y, cw, 22}, si, styles)) { p.style = si; changed = true; }
        y += 32;
    }
    auto slide = [&](const char* name, const char* key, double& v) {
        label(name);
        float f = float(v);
        char buf[16]; std::snprintf(buf, sizeof buf, "%d%%", int(std::lround(v * 100)));
        if (g.slider(id(key), {cx, y, cw, 22}, f, 0.f, 1.f, 0.5f, buf)) { v = f; changed = true; }
        y += 32;
    };
    slide("Complexity", "drm-cx", p.complexity);
    slide("Loudness", "drm-loud", p.loudness);
    slide("Swing", "drm-swing", p.swing);
    {
        label("Fills");
        bool f = p.fills;
        if (g.checkbox(id("drm-fills"), {cx, y, cw, 22}, tr("Add fills at the end"), f)) { p.fills = f; changed = true; }
        y += 32;
        label("Bars");
        double bars = p.bars;
        if (g.numberField(id("drm-bars"), {cx, y, 70, 22}, bars, 1, 16, 1, 0, "", 4)) { p.bars = int(bars); changed = true; }
        y += 32;
        label("Seed");
        double seed = p.seed;
        if (g.numberField(id("drm-seed"), {cx, y, 70, 22}, seed, 1, 9999, 1, 0, "", 1)) { p.seed = int(seed); changed = true; }
        if (g.button(id("drm-roll"), {cx + 78, y, cw - 78, 22}, tr("Roll New Variation"))) { p.seed = p.seed % 9999 + 1; changed = true; }
        y += 32;
    }
    if (!g.mouseDown()) m_drumCkpt = false;
    if (changed) {
        if (!m_drumCkpt) { m_s.checkpoint(); m_drumCkpt = true; }
        m_s.setDrummerParams(m_selTrack, m_selRegion, p);
        m_modelDirty = true;
    }
    // ---- pattern preview (right): one row per drum, the beats across
    const Rect pv{panel.right() + 12, r.y + 12, r.w - pw - 24, r.h - 24};
    if (pv.w < 80 || pv.h < 40) return;
    std::vector<int> pitches;
    for (const auto& n : rg.notes) if (std::find(pitches.begin(), pitches.end(), n.pitch) == pitches.end()) pitches.push_back(n.pitch);
    std::sort(pitches.begin(), pitches.end(), [](int a, int b) { return a > b; });
    const double beats = std::max(1.0, m_s.regionBeats(rg));
    const float labelW = 86;
    const Rect grid{pv.x + labelW, pv.y, pv.w - labelW, pv.h};
    dl.rrect(grid, 6, rgb(0x14, 0x17, 0x1c));
    const auto tm = m_tempo ? m_tempo : m_s.tempoMap();
    const int perBar = std::max(1, tm->meterAtBeat(rg.start).num);
    dl.pushClip(grid);
    for (int b = 0; b <= int(beats); ++b) {
        const float x = grid.x + float(b / beats) * grid.w;
        dl.vline(x, grid.y, grid.bottom(), (b % perBar == 0) ? rgb(0x3a, 0x40, 0x4c) : rgb(0x24, 0x28, 0x32));
    }
    const float rowH = pitches.empty() ? 20.f : std::min(26.f, grid.h / float(pitches.size()));
    for (size_t i = 0; i < pitches.size(); ++i) {
        const float ry = grid.y + float(i) * rowH;
        dl.hline(grid.x, grid.right(), ry, rgb(0x24, 0x28, 0x32));
        for (const auto& n : rg.notes) {
            if (n.pitch != pitches[i]) continue;
            const float nx = grid.x + float(n.start / beats) * grid.w;
            const float nw = std::max(4.f, float(n.length / beats) * grid.w - 1.f);
            dl.rrect({nx, ry + 3, nw, rowH - 6}, 3, withAlpha(accent, uint8_t(90 + std::clamp(n.velocity, 0, 127) * 165 / 127)));
        }
    }
    dl.popClip();
    for (size_t i = 0; i < pitches.size(); ++i) {
        const float ry = grid.y + float(i) * rowH;
        if (ry + rowH > pv.bottom()) break;
        dl.textIn({pv.x, ry, labelW - 8, rowH}, tr(drumName(pitches[i])), textDim, small, Align::Right);
    }
    if (pitches.empty()) dl.textIn(grid, tr("This region has no notes yet."), textMute, normal, Align::Center);
}

}  // namespace vkui
