// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Bottom panel of the Qt-free UI: tabs for Mixer, Inspector, Smart Controls, Piano Roll, Step Sequencer, Drummer, Loops.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <regex>
#include "../dsp/Drummer.h"
#include "../dsp/Effects.h"
#include "Ui.h"

namespace ui {

namespace {
constexpr Rgba kBg = rgba(24, 26, 31), kPanel = rgba(32, 35, 42), kPanel2 = rgba(41, 45, 54), kPanel3 = rgba(52, 57, 68), kLine = rgba(14, 15, 18);
constexpr Rgba kText = rgba(226, 230, 238), kDim = rgba(137, 146, 163), kAccent = rgba(38, 222, 196), kAmber = rgba(255, 176, 46), kRec = rgba(255, 77, 94);
Rgba trackColor(const core::Track& t) { return rgba(t.color.r, t.color.g, t.color.b); }
bool isDark(Rgba c) { return ((c >> 16 & 255) * 299 + (c >> 8 & 255) * 587 + (c & 255) * 114) / 1000 < 140; }
std::string num(const char* fmt, double v) { char b[64]; std::snprintf(b, sizeof b, fmt, v); return b; }
std::string noteName(int p) { static const char* n[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}; return std::string(n[p % 12]) + std::to_string(p / 12 - 2); }
bool blackKey(int p) { const int k = p % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }
const char* const kTabNames[] = {"Mixer", "Inspector", "Smart Controls", "Piano Roll", "Step Sequencer", "Drummer", "Automation", "Loops"};
}  // namespace

// Tab ids in the order of kTabNames; Tab::Mixer = 0 ... (Inspector and Smart Controls are stored beyond the enum below)
// m_tab only holds Mixer / PianoRoll / Steps / Drummer / Loops; the two extra tabs use these constants.
static constexpr int kTabInspector = 100, kTabSmart = 101;

std::string NativeUi::trackTitle(int t) const { return t >= 0 && t < int(m_tracks.size()) ? m_tracks[size_t(t)].name : std::string("-"); }

// ---------------------------------------------------------------- generic controls
void NativeUi::uiSlider(Canvas& c, const Rect& r, const std::string& label, double t01, const std::string& valueText, std::function<void(double)> set, Rgba color) {
    t01 = std::clamp(t01, 0.0, 1.0);
    c.textIn({r.x, r.y, r.w, 14}, label, kDim, Font::Regular11);
    c.textIn({r.x, r.y, r.w, 14}, valueText, kText, Font::Regular11, Align::Right);
    const Rect groove{r.x, r.y + 20, r.w, 6};
    c.fillRoundRect(groove, 3, rgba(14, 15, 18));
    c.pushClip(groove);
    c.fillRoundRect({groove.x, groove.y, std::max(0, int(groove.w * t01)), groove.h}, 3, withAlpha(color, 235));
    c.popClip();
    c.fillCircle(groove.x + groove.w * t01, groove.y + 3, 6, rgba(236, 240, 247));
    m_sliderFns.push_back(std::move(set));
    addHit({r.x - 4, r.y + 12, r.w + 8, 22}, Hit::Slider, int(m_sliderFns.size()) - 1);
}

void NativeUi::uiAction(Canvas& c, const Rect& r, const std::string& label, std::function<void()> fn, bool on, Rgba onColor) {
    const bool hover = r.contains(m_hoverX, m_hoverY);
    c.fillRoundRect(r, 5, on ? onColor : (hover ? kPanel3 : kPanel2));
    if (!on) c.strokeRoundRect(r, 5, withAlpha(kLine, 160));
    c.textIn(r.inset(4, 0), label, on ? (isDark(onColor) ? kText : rgba(16, 18, 22)) : kText, Font::Regular11, Align::Center);
    m_actionFns.push_back(std::move(fn));
    addHit(r, Hit::Action, int(m_actionFns.size()) - 1);
}

// ---------------------------------------------------------------- tab bar and dispatch
void NativeUi::drawBottom(Canvas& c) {
    const Rect area = bottomArea();
    c.fillRect({area.x, area.y, area.w, 30}, rgba(27, 29, 35));
    c.hline(0, m_w - 1, area.y, kLine);
    int x = 8;
    const int ids[] = {0, kTabInspector, kTabSmart, int(Tab::PianoRoll), int(Tab::Steps), int(Tab::Drummer), int(Tab::Automation), int(Tab::Loops)};
    const int cur = m_tabExtra ? m_tabExtra : int(m_tab);
    for (int i = 0; i < 8; ++i) {
        const int id = ids[i];
        const int w = c.textWidth(kTabNames[i], Font::Bold11) + 24;
        const Rect r{x, area.y + 4, w, 22};
        const bool on = (id == 0 && cur == 0) || id == cur;
        c.fillRoundRect(r, 6, on ? kPanel : (r.contains(m_hoverX, m_hoverY) ? kPanel2 : rgba(27, 29, 35)));
        if (on) c.fillRect({r.x + 8, r.bottom() - 2, r.w - 16, 2}, kAccent);
        c.textIn(r, kTabNames[i], on ? kText : kDim, Font::Bold11, Align::Center);
        addHit(r, Hit::Tab, id);
        x += w + 4;
    }
    const Rect content = paneContent();
    c.fillRect(content, kPanel);
    c.pushClip(content);
    switch (cur) {
    case kTabInspector: drawInspector(c, content); break;
    case kTabSmart: drawSmartControls(c, content); break;
    case int(Tab::PianoRoll): drawPianoRoll(c, content); break;
    case int(Tab::Steps): drawSteps(c, content); break;
    case int(Tab::Drummer): drawDrummer(c, content); break;
    case int(Tab::Automation): drawAutomation(c, content); break;
    case int(Tab::Loops): drawLoops(c, content); break;
    default: drawMixer(c); break;
    }
    c.popClip();
}

// ---------------------------------------------------------------- inspector
void NativeUi::drawInspector(Canvas& c, const Rect& r) {
    const bool haveTrack = m_selTrack >= 0 && m_selTrack < int(m_tracks.size());
    if (!haveTrack) { c.textIn(r, "Select a track or a region to see its properties.", kDim, Font::Regular11, Align::Center); return; }
    const int ti = m_selTrack;
    const core::Track& t = m_tracks[size_t(ti)];
    const Rgba col = trackColor(t);
    // ---- column 1: track ----
    int x = r.x + 18, y = r.y + 14;
    const int cw = 250;
    c.fillRoundRect({x, y, cw, 22}, 11, col);
    c.textIn({x + 12, y, cw - 24, 22}, t.name, isDark(col) ? rgba(240, 244, 250) : rgba(16, 18, 22), Font::Bold11, Align::Center);
    addHit({x, y, cw, 22}, Hit::Action, int(m_actionFns.size()));
    m_actionFns.push_back([this] { m_selRegion = -1; dlgRename(); });
    y += 32;
    static const core::Color palette[] = {{70, 130, 240}, {40, 200, 170}, {95, 190, 100}, {235, 200, 70}, {240, 150, 50}, {235, 90, 90}, {225, 80, 170}, {165, 90, 225}};
    for (int i = 0; i < 8; ++i) {
        const Rect sw{x + i * 31, y, 25, 18};
        c.fillRoundRect(sw, 5, rgba(palette[i].r, palette[i].g, palette[i].b));
        if (palette[i].r == t.color.r && palette[i].g == t.color.g && palette[i].b == t.color.b) c.strokeRoundRect(sw.inset(-2), 6, rgba(240, 244, 250));
        const core::Color cc = palette[i];
        m_actionFns.push_back([this, ti, cc] { m_s.setTrackColor(ti, cc); });
        addHit(sw, Hit::Action, int(m_actionFns.size()) - 1);
    }
    y += 30;
    uiSlider(c, {x, y, cw, 30}, "Volume", gainToFader(t.volume), t.volume <= 0.0009f ? "-inf" : num("%.1f dB", 20.0 * std::log10(double(t.volume))), [this, ti](double v) { m_s.setVolume(ti, faderToGain(v)); }, col);
    y += 38;
    uiSlider(c, {x, y, cw, 30}, "Pan", (t.pan + 1.0) / 2.0, std::fabs(t.pan) < 0.02f ? "Center" : num(t.pan < 0 ? "L%.0f" : "R%.0f", std::fabs(t.pan) * 100), [this, ti](double v) { m_s.setPan(ti, float(v * 2.0 - 1.0)); }, col);
    y += 38;
    uiSlider(c, {x, y, cw / 2 - 6, 30}, "Send A (reverb)", t.sendA, num("%.0f %%", t.sendA * 100), [this, ti](double v) { m_s.setSendA(ti, float(v)); }, col);
    uiSlider(c, {x + cw / 2 + 6, y, cw / 2 - 6, 30}, "Send B (echo)", t.sendB, num("%.0f %%", t.sendB * 100), [this, ti](double v) { m_s.setSendB(ti, float(v)); }, col);

    // ---- column 2: instrument and inserts ----
    x = r.x + 18 + cw + 30; y = r.y + 14;
    c.text(x, y, "Instrument", kDim, Font::Bold11);
    y += 20;
    if (t.instrument) {
        const int n = t.instrument->presetCount();
        c.textIn({x, y, cw, 20}, t.instrumentId == "drumkit" ? "Drum Kit" : t.instrumentId == "synth" ? "Synth" : t.instrumentId, kText, Font::Regular11);
        y += 22;
        if (n > 0) {
            uiAction(c, {x, y, 26, 22}, "<", [this, ti, n] { setPresetDelta(ti, -1, n); });
            c.textIn({x + 30, y, cw - 60, 22}, "Preset  -  use the arrows", kDim, Font::Regular11, Align::Center);
            uiAction(c, {x + cw - 26, y, 26, 22}, ">", [this, ti, n] { setPresetDelta(ti, 1, n); });
            y += 26;
        }
    } else { c.textIn({x, y, cw, 20}, "Audio track", kDim, Font::Regular11); y += 26; }
    c.text(x, y, "Inserts", kDim, Font::Bold11);
    y += 20;
    for (int k = 0; k < 5; ++k) {
        const Rect slot{x, y + k * 24, cw - 56, 20};
        c.fillRoundRect(slot, 5, rgba(20, 22, 27));
        if (k < int(t.inserts.size()) && t.inserts[size_t(k)]) {
            const auto& fx = t.inserts[size_t(k)];
            c.fillRoundRect(slot, 5, k == m_insertSel ? rgba(52, 110, 150) : rgba(44, 86, 120));
            c.textIn(slot.inset(8, 0), fx->displayName(), rgba(214, 232, 250), Font::Regular11);
            m_actionFns.push_back([this, k] { m_insertSel = k; m_tabExtra = kTabSmart; });
            addHit(slot, Hit::Action, int(m_actionFns.size()) - 1);
            uiAction(c, {slot.right() + 4, slot.y, 22, 20}, "B", [this, ti, k, on = !fx->bypass.load()] { m_s.setInsertBypass(ti, k, on); }, fx->bypass.load(), kAmber);
            uiAction(c, {slot.right() + 28, slot.y, 22, 20}, "x", [this, ti, k] { m_s.removeInsert(ti, k); });
        } else if (k == int(t.inserts.size())) {
            c.textIn(slot, "+ Add effect", kDim, Font::Regular11, Align::Center);
            m_actionFns.push_back([this, ti, slot] {
                std::vector<MenuItem> items;
                for (const std::string& id : dsp::insertEffectIds()) {
                    std::string label = id;
                    if (auto fx = dsp::createEffect(id)) label = fx->displayName();
                    items.push_back({label, "", true, false, [this, ti, id] { m_s.addInsert(ti, id); }});
                }
                openMenu(items, slot.x, slot.bottom());
            });
            addHit(slot, Hit::Action, int(m_actionFns.size()) - 1);
        }
    }

    // ---- column 3: region ----
    x = r.x + 18 + 2 * (cw + 30); y = r.y + 14;
    c.text(x, y, "Region", kDim, Font::Bold11);
    y += 20;
    if (!hasRegion()) { c.textIn({x, y, cw, 20}, "No region selected.", kDim, Font::Regular11); return; }
    const int ri = m_selRegion;
    const core::Region& rg = t.regions[size_t(ri)];
    c.fillRoundRect({x, y, cw, 22}, 6, rgba(20, 22, 27));
    c.textIn({x + 8, y, cw - 16, 22}, rg.name.empty() ? t.name : rg.name, kText, Font::Regular11);
    m_actionFns.push_back([this] { dlgRename(); });
    addHit({x, y, cw, 22}, Hit::Action, int(m_actionFns.size()) - 1);
    y += 28;
    uiAction(c, {x, y, 76, 22}, "Mute", [this, ti, ri, on = !rg.mute] { m_s.setRegionMute(ti, ri, on); }, rg.mute, kAmber);
    if (rg.midi) {
        char b[64]; std::snprintf(b, sizeof b, "%.2f beats, %d notes", m_s.regionBeats(rg), int(rg.notes.size()));
        c.textIn({x + 84, y, cw - 84, 22}, b, kDim, Font::Regular11);
        y += 30;
        uiAction(c, {x, y, 76, 22}, "Quantize", [this] { dlgQuantize(); });
        uiAction(c, {x + 82, y, 76, 22}, "Transpose", [this] { dlgTranspose(); });
        uiAction(c, {x + 164, y, 76, 22}, "Piano Roll", [this] { m_tabExtra = 0; m_tab = Tab::PianoRoll; });
    } else {
        uiAction(c, {x + 82, y, 76, 22}, "Reverse", [this, ti, ri] { m_s.reverseRegion(ti, ri); });
        uiAction(c, {x + 164, y, 76, 22}, "Normalize", [this, ti, ri] { m_s.normalizeRegion(ti, ri); });
        y += 30;
        uiSlider(c, {x, y, cw, 30}, "Gain", (rg.gainDb + 24.0) / 36.0, num("%.1f dB", rg.gainDb), [this, ti, ri](double v) { const auto& g = m_tracks[size_t(ti)].regions[size_t(ri)]; m_s.setRegionAudio(ti, ri, float(std::round((v * 36.0 - 24.0) * 2) / 2), g.fadeIn, g.fadeOut); }, kAccent);
        y += 36;
        uiSlider(c, {x, y, cw / 2 - 6, 30}, "Fade in", std::min(1.0, double(rg.fadeIn) / 88200.0), num("%.0f ms", double(rg.fadeIn) / 44.1), [this, ti, ri](double v) { const auto& g = m_tracks[size_t(ti)].regions[size_t(ri)]; m_s.setRegionAudio(ti, ri, g.gainDb, core::i64(v * 88200.0), g.fadeOut); }, kAccent);
        uiSlider(c, {x + cw / 2 + 6, y, cw / 2 - 6, 30}, "Fade out", std::min(1.0, double(rg.fadeOut) / 88200.0), num("%.0f ms", double(rg.fadeOut) / 44.1), [this, ti, ri](double v) { const auto& g = m_tracks[size_t(ti)].regions[size_t(ri)]; m_s.setRegionAudio(ti, ri, g.gainDb, g.fadeIn, core::i64(v * 88200.0)); }, kAccent);
        y += 40;
        uiAction(c, {x, y, 120, 22}, rg.follow ? "Follows project tempo" : "Follow project tempo", [this, ti, ri, follow = !rg.follow] {
            const auto& g = m_tracks[size_t(ti)].regions[size_t(ri)];
            double bpm = g.sourceBpm > 0 ? g.sourceBpm : m_s.detectRegionTempo(ti, ri);
            if (bpm <= 0) { message("No tempo found - use Detect Tempo and choose 'Follow project tempo'."); return; }
            m_s.setRegionFlex(ti, ri, follow, bpm, g.semitones);
        }, rg.follow);
        uiSlider(c, {x + 128, y - 6, cw - 128, 30}, "Transpose", (rg.semitones + 12.0) / 24.0, num("%+.0f st", rg.semitones), [this, ti, ri](double v) {
            const auto& g = m_tracks[size_t(ti)].regions[size_t(ri)];
            const double semis = std::round(v * 24.0 - 12.0);
            if (semis != g.semitones) m_s.setRegionFlex(ti, ri, g.follow, g.sourceBpm > 0 ? g.sourceBpm : m_s.bpm(), semis);
        }, kAccent);
    }
}

void NativeUi::setPresetDelta(int ti, int d, int n) {
    if (ti < 0 || ti >= int(m_tracks.size()) || n <= 0) return;
    m_presetIdx[ti] = ((m_presetIdx.count(ti) ? m_presetIdx[ti] : 0) + d + n) % n;
    m_s.setInstrumentPreset(ti, m_presetIdx[ti]);
    const auto& inst = m_tracks[size_t(ti)].instrument;
    if (inst) message(std::string("Preset: ") + inst->presetName(m_presetIdx[ti]));
}

// ---------------------------------------------------------------- smart controls (instrument and insert parameters)
void NativeUi::drawSmartControls(Canvas& c, const Rect& r) {
    if (m_selTrack < 0 || m_selTrack >= int(m_tracks.size())) { c.textIn(r, "Select a track to edit its instrument and effects.", kDim, Font::Regular11, Align::Center); return; }
    const core::Track& t = m_tracks[size_t(m_selTrack)];
    const Rgba col = trackColor(t);
    struct Source { std::string title; dsp::ParamSet* set; };
    std::vector<Source> sources;
    if (t.instrument) sources.push_back({t.instrumentId == "drumkit" ? "Drum Kit" : "Synth", t.instrument.get()});
    for (const auto& fx : t.inserts) if (fx) sources.push_back({fx->displayName(), fx.get()});
    if (sources.empty()) { c.textIn(r, "This track has no instrument or effects. Add an effect in the Inspector.", kDim, Font::Regular11, Align::Center); return; }
    int pick = 0;
    if (m_insertSel >= 0) pick = std::clamp(m_insertSel + (t.instrument ? 1 : 0), 0, int(sources.size()) - 1);
    // source selector
    int x = r.x + 14;
    for (int i = 0; i < int(sources.size()); ++i) {
        const int w = c.textWidth(sources[size_t(i)].title, Font::Bold11) + 22;
        uiAction(c, {x, r.y + 10, w, 22}, sources[size_t(i)].title, [this, i, hasInst = bool(t.instrument)] { m_insertSel = i - (hasInst ? 1 : 0); }, i == pick, col);
        x += w + 6;
    }
    dsp::ParamSet* set = sources[size_t(pick)].set;
    const int cols = 4, cw = (r.w - 28 - (cols - 1) * 16) / cols;
    int idx = 0;
    for (auto& p : set->params) {
        const int row = idx / cols, colI = idx % cols;
        const Rect cell{r.x + 14 + colI * (cw + 16), r.y + 48 + row * 44, cw, 36};
        if (cell.bottom() > r.bottom()) break;
        dsp::Param* prm = p.get();
        const float v = prm->get();
        std::string text = prm->stepped && !prm->choices.empty() ? prm->choices[size_t(std::clamp(int(v), 0, int(prm->choices.size()) - 1))] : num(std::fabs(v) >= 100 ? "%.0f" : "%.2f", v) + (prm->unit.empty() ? "" : " " + prm->unit);
        double t01 = prm->log && prm->min > 0 ? std::log(double(v) / prm->min) / std::log(double(prm->max) / prm->min) : double(v - prm->min) / double(prm->max - prm->min);
        uiSlider(c, cell, prm->name, t01, text, [prm](double t01v) {
            const double val = prm->log && prm->min > 0 ? prm->min * std::pow(double(prm->max) / prm->min, t01v) : prm->min + t01v * (prm->max - prm->min);
            prm->set(float(val));
        }, col);
        ++idx;
    }
}

// ---------------------------------------------------------------- step sequencer
void NativeUi::drawSteps(Canvas& c, const Rect& r) {
    if (m_selTrack < 0 || m_selTrack >= int(m_tracks.size()) || m_tracks[size_t(m_selTrack)].type != core::TrackType::Drums) {
        c.textIn(r, "Select a Drum Machine track to edit its pattern.", kDim, Font::Regular11, Align::Center);
        return;
    }
    static const char* kRowNames[kDrumRows] = {"Kick", "Snare", "Hi-Hat", "Clap"};
    static const Rgba kRowColors[kDrumRows] = {rgba(70, 130, 240), rgba(165, 90, 225), rgba(40, 200, 170), rgba(225, 80, 170)};
    const core::Track& t = m_tracks[size_t(m_selTrack)];
    const int lw = 96, pad = 14;
    const double cw = double(r.w - lw - 2 * pad) / kSteps, ch = double(r.h - 2 * pad - 24) / kDrumRows;
    const double stepNow = std::fmod(m_s.positionBeats() * 4.0, double(kSteps));
    for (int row = 0; row < kDrumRows; ++row) {
        const int y = r.y + pad + 24 + int(row * ch);
        c.textIn({r.x + pad, y, lw - 8, int(ch) - 4}, kRowNames[row], kText, Font::Bold11);
        for (int s = 0; s < kSteps; ++s) {
            const Rect cell{r.x + pad + lw + int(s * cw), y, int(cw) - 4, int(ch) - 4};
            const bool on = t.steps[size_t(row)][size_t(s)];
            c.fillRoundRect(cell, 5, on ? kRowColors[row] : (s / 4 % 2 ? rgba(36, 39, 47) : rgba(44, 48, 58)));
            if (m_s.isPlaying() && int(stepNow) == s) c.strokeRoundRect(cell, 5, rgba(240, 244, 250));
        }
    }
    for (int s = 0; s < kSteps; ++s) c.textIn({r.x + pad + lw + int(s * cw), r.y + pad, int(cw) - 4, 18}, std::to_string(s + 1), s % 4 == 0 ? kText : kDim, Font::Regular11, Align::Center);
    addHit({r.x + pad + lw, r.y + pad + 24, int(cw * kSteps), int(ch * kDrumRows)}, Hit::StepCell);
    m_stepGrid = {r.x + pad + lw, r.y + pad + 24, int(cw * kSteps), int(ch * kDrumRows)};
    uiAction(c, {r.right() - 120, r.y + 8, 100, 20}, "Clear pattern", [this] { m_s.checkpoint(); for (int rw = 0; rw < kDrumRows; ++rw) for (int s = 0; s < kSteps; ++s) m_s.setStep(m_selTrack, rw, s, false); });
}

// ---------------------------------------------------------------- drummer editor
void NativeUi::drawDrummer(Canvas& c, const Rect& r) {
    if (!hasRegion() || !m_tracks[size_t(m_selTrack)].regions[size_t(m_selRegion)].drummer) {
        c.textIn(r, "Select a Drummer region (double-click an empty lane of a Drummer track to create one).", kDim, Font::Regular11, Align::Center);
        return;
    }
    const int ti = m_selTrack, ri = m_selRegion;
    dsp::DrummerParams p = m_tracks[size_t(ti)].regions[size_t(ri)].drum;
    const Rgba col = trackColor(m_tracks[size_t(ti)]);
    auto apply = [this, ti, ri](dsp::DrummerParams np) { m_s.checkpoint(); m_s.setDrummerParams(ti, ri, np); };
    int x = r.x + 20, y = r.y + 18;
    const int cw = 280;
    c.text(x, y, "Style", kDim, Font::Bold11);
    y += 20;
    const int n = dsp::drummerStyleCount();
    for (int i = 0; i < n; ++i) {
        const Rect b{x + (i % 3) * 94, y + (i / 3) * 28, 88, 24};
        uiAction(c, b, dsp::drummerStyleName(i), [apply, p, i]() mutable { p.style = i; apply(p); }, p.style == i, col);
    }
    x = r.x + 20 + 3 * 94 + 40; y = r.y + 14;
    uiSlider(c, {x, y, cw, 30}, "Complexity", p.complexity, num("%.0f %%", p.complexity * 100), [this, ti, ri, p](double v) mutable { p.complexity = v; m_s.setDrummerParams(ti, ri, p); }, col);
    y += 40;
    uiSlider(c, {x, y, cw, 30}, "Loudness", p.loudness, num("%.0f %%", p.loudness * 100), [this, ti, ri, p](double v) mutable { p.loudness = v; m_s.setDrummerParams(ti, ri, p); }, col);
    y += 40;
    uiSlider(c, {x, y, cw, 30}, "Swing", p.swing, num("%.0f %%", p.swing * 100), [this, ti, ri, p](double v) mutable { p.swing = v; m_s.setDrummerParams(ti, ri, p); }, col);
    x += cw + 40; y = r.y + 18;
    uiAction(c, {x, y, 140, 24}, p.fills ? "Fills: On" : "Fills: Off", [apply, p]() mutable { p.fills = !p.fills; apply(p); }, p.fills, col);
    y += 34;
    uiAction(c, {x, y, 140, 24}, "New variation", [apply, p]() mutable { p.seed = p.seed % 9999 + 1; apply(p); });
    y += 34;
    c.text(x, y + 4, "Bars", kDim, Font::Regular11);
    uiAction(c, {x + 50, y, 26, 24}, "-", [apply, p]() mutable { p.bars = std::max(1, p.bars - 1); apply(p); });
    c.textIn({x + 78, y, 36, 24}, std::to_string(p.bars), kText, Font::Bold11, Align::Center);
    uiAction(c, {x + 114, y, 26, 24}, "+", [apply, p]() mutable { p.bars = std::min(32, p.bars + 1); apply(p); });
}

// ---------------------------------------------------------------- loop browser
void NativeUi::reloadLoops() {
    namespace fs = std::filesystem;
    m_loops.clear();
    m_loopsLoaded = true;
    std::error_code ec;
    static const std::regex bpmRe("(\\d{2,3})\\s*[-_ ]?\\s*bpm", std::regex::icase);
    std::vector<std::string> files;
    for (fs::recursive_directory_iterator it(m_host.loopFolder, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::string ext = it->path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
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

void NativeUi::drawLoops(Canvas& c, const Rect& r) {
    if (!m_loopsLoaded) reloadLoops();
    c.textIn({r.x + 16, r.y + 8, r.w - 300, 22}, m_loops.empty() ? "No audio files found in " + m_host.loopFolder : std::to_string(m_loops.size()) + " loops - click to audition, double-click to add at the playhead.", kDim, Font::Regular11);
    uiAction(c, {r.right() - 270, r.y + 8, 120, 22}, "Choose folder...", [this] {
        if (!m_host.fileDialog) return;
        const std::string f = m_host.fileDialog("Choose a file in the loops folder", "Audio files|*.wav;*.mp3;*.flac;*.ogg;*.m4a", false);
        if (f.empty()) return;
        m_host.loopFolder = std::filesystem::path(f).parent_path().string();
        reloadLoops();
    });
    uiAction(c, {r.right() - 140, r.y + 8, 120, 22}, "Stop preview", [this] { m_s.stopAudition(); });
    const int rowH = 24, top = r.y + 38, visible = (r.h - 40) / rowH;
    c.fillRect({r.x + 10, top - 2, r.w - 20, r.h - 38}, rgba(24, 26, 31));
    m_loopScroll = std::clamp(m_loopScroll, 0.0, std::max(0.0, double(int(m_loops.size()) - visible)));
    for (int i = 0; i < visible; ++i) {
        const int idx = int(m_loopScroll) + i;
        if (idx >= int(m_loops.size())) break;
        const Rect row{r.x + 12, top + i * rowH, r.w - 24, rowH - 2};
        c.fillRoundRect(row, 5, idx == m_loopSel ? rgba(52, 110, 150) : (i % 2 ? rgba(30, 33, 40) : rgba(27, 29, 35)));
        c.textIn({row.x + 12, row.y, row.w - 160, row.h}, m_loops[size_t(idx)].name, kText, Font::Regular11);
        if (m_loops[size_t(idx)].bpm > 0) c.textIn({row.right() - 140, row.y, 128, row.h}, num("%.0f BPM", m_loops[size_t(idx)].bpm), kAmber, Font::Regular11, Align::Right);
        addHit(row, Hit::LoopItem, idx);
    }
}

// ---------------------------------------------------------------- piano roll
bool NativeUi::pianoRegion(int& t, int& r) const {
    if (!hasRegion() || !m_tracks[size_t(m_selTrack)].regions[size_t(m_selRegion)].midi) return false;
    t = m_selTrack; r = m_selRegion;
    return true;
}

void NativeUi::pianoSelectionChanged() {
    int t, r;
    if (!pianoRegion(t, r)) return;
    const core::Region& rg = m_tracks[size_t(t)].regions[size_t(r)];
    int hi = 60;
    for (const auto& n : rg.notes) hi = std::max(hi, n.pitch);
    m_prTop = std::min(127, hi + 8);
    m_prScroll = 0;
    m_prSel = -1;
    const double len = std::max(1.0, m_s.regionBeats(rg));
    m_prPpb = std::clamp(double(m_w - 56 - 30) / len, 20.0, 240.0);
}

void NativeUi::auditionNote(int track, int pitch) {
    if (m_auditionPitch >= 0) m_s.noteOff(m_auditionTrack, m_auditionPitch);
    m_s.noteOn(track, pitch, 100);
    m_auditionTrack = track; m_auditionPitch = pitch; m_auditionTicks = 10;
}

void NativeUi::drawPianoRoll(Canvas& c, const Rect& r) {
    int t, ri;
    if (!pianoRegion(t, ri)) { c.textIn(r, "Select a MIDI region (double-click one) to edit its notes.", kDim, Font::Regular11, Align::Center); m_prGrid = {}; return; }
    const int key = t * 100000 + ri;
    if (key != m_prKey) { m_prKey = key; pianoSelectionChanged(); }
    const core::Track& tr = m_tracks[size_t(t)];
    const core::Region& rg = tr.regions[size_t(ri)];
    const Rgba col = trackColor(tr);
    const int kw = 56, velH = 64;
    m_prKeys = {r.x, r.y, kw, r.h - velH};
    m_prGrid = {r.x + kw, r.y, r.w - kw, r.h - velH};
    m_prVel = {r.x + kw, m_prGrid.bottom(), m_prGrid.w, velH};
    const int rows = m_prGrid.h / m_prRowH + 1;
    const double len = m_s.regionBeats(rg);
    auto yOf = [&](int pitch) { return m_prGrid.y + (m_prTop - pitch) * m_prRowH; };
    auto xOf = [&](double beat) { return m_prGrid.x + int(std::lround(beat * m_prPpb - m_prScroll)); };
    c.pushClip(m_prGrid);
    for (int i = 0; i < rows; ++i) {
        const int pitch = m_prTop - i;
        if (pitch < 0 || pitch > 127) continue;
        c.fillRect({m_prGrid.x, yOf(pitch), m_prGrid.w, m_prRowH}, blackKey(pitch) ? rgba(24, 26, 31) : rgba(31, 34, 41));
        if (pitch % 12 == 0) c.hline(m_prGrid.x, m_prGrid.right() - 1, yOf(pitch) + m_prRowH - 1, rgba(70, 76, 90));
    }
    const int endX = xOf(len);
    c.fillRect({endX, m_prGrid.y, std::max(0, m_prGrid.right() - endX), m_prGrid.h}, withAlpha(rgba(0, 0, 0), 110));
    const auto tm = m_s.tempoMap();
    const double regionStart = rg.start;
    const int firstBar = std::max(1, tm->barAt(regionStart));
    for (int bar = firstBar; tm->barStart(bar) < regionStart + len + 0.001; ++bar) {
        const double b0 = tm->barStart(bar) - regionStart;
        c.vline(xOf(b0), m_prGrid.y, m_prGrid.bottom() - 1, withAlpha(rgba(255, 255, 255), 40));
        for (double b = 1; b < tm->barLength(bar); b += 1) c.vline(xOf(b0 + b), m_prGrid.y, m_prGrid.bottom() - 1, withAlpha(rgba(255, 255, 255), 16));
    }
    const double step = snapStep() > 0 ? snapStep() : 0.25;
    if (step <= 0.25 && m_prPpb * step >= 8) for (double b = 0; b < len; b += step) c.vline(xOf(b), m_prGrid.y, m_prGrid.bottom() - 1, withAlpha(rgba(255, 255, 255), 7));
    for (size_t i = 0; i < rg.notes.size(); ++i) {
        const auto& n = rg.notes[i];
        const Rect nr{xOf(n.start), yOf(n.pitch) + 1, std::max(3, int(n.length * m_prPpb) - 1), m_prRowH - 2};
        const Rgba nc = mix(lighten(col, 0.1), rgba(255, 255, 255), n.velocity / 127.0 * 0.45);
        c.fillRoundRect(nr, 3, rg.mute ? rgba(110, 116, 128) : nc);
        if (int(i) == m_prSel) c.strokeRoundRect(nr, 3, rgba(245, 248, 252));
        else c.strokeRoundRect(nr, 3, withAlpha(rgba(0, 0, 0), 120));
    }
    const double px = xOf(m_s.positionBeats() - regionStart);
    if (px >= m_prGrid.x && px < m_prGrid.right()) c.vline(int(px), m_prGrid.y, m_prGrid.bottom() - 1, withAlpha(kAccent, 235));
    c.popClip();
    addHit(m_prGrid, Hit::PianoGrid);
    // keyboard
    c.pushClip(m_prKeys);
    c.fillRect(m_prKeys, rgba(210, 214, 222));
    for (int i = 0; i < rows; ++i) {
        const int pitch = m_prTop - i;
        if (pitch < 0 || pitch > 127) continue;
        const int y = yOf(pitch);
        if (blackKey(pitch)) { c.fillRect({m_prKeys.x, y, kw * 62 / 100, m_prRowH}, rgba(34, 36, 42)); c.hline(m_prKeys.x + kw * 62 / 100, m_prKeys.right() - 1, y + m_prRowH / 2, rgba(150, 156, 168)); }
        else c.hline(m_prKeys.x, m_prKeys.right() - 1, y + m_prRowH - 1, rgba(150, 156, 168));
        if (pitch % 12 == 0) c.text(m_prKeys.right() - 4, y + 1, noteName(pitch), rgba(40, 44, 54), Font::Regular11, Align::Right);
    }
    c.popClip();
    addHit(m_prKeys, Hit::PianoKeys);
    // velocity lane
    c.fillRect(m_prVel, rgba(22, 24, 29));
    c.text(r.x + 6, m_prVel.y + 6, "Velocity", kDim, Font::Regular11);
    c.pushClip(m_prVel);
    for (size_t i = 0; i < rg.notes.size(); ++i) {
        const auto& n = rg.notes[i];
        const int h = int((velH - 8) * n.velocity / 127.0);
        c.fillRect({xOf(n.start), m_prVel.bottom() - 4 - h, 5, h}, int(i) == m_prSel ? rgba(245, 248, 252) : col);
    }
    c.popClip();
    addHit(m_prVel, Hit::PianoVel);
}

// ---------------------------------------------------------------- pane input
bool NativeUi::paneMouseDown(int x, int y, int button, int mods) {
    (void)mods;
    int t, ri;
    const int cur = m_tabExtra ? m_tabExtra : int(m_tab);
    if (cur == int(Tab::Automation)) return autoMouseDown(x, y, button);
    if (cur != int(Tab::PianoRoll) || !pianoRegion(t, ri)) return false;
    const core::Region& rg = m_tracks[size_t(t)].regions[size_t(ri)];
    auto xOf = [&](double beat) { return m_prGrid.x + int(std::lround(beat * m_prPpb - m_prScroll)); };
    auto yOf = [&](int pitch) { return m_prGrid.y + (m_prTop - pitch) * m_prRowH; };
    if (m_prKeys.contains(x, y)) {
        const int pitch = m_prTop - (y - m_prKeys.y) / m_prRowH;
        if (pitch >= 0 && pitch <= 127) auditionNote(t, pitch);
        return true;
    }
    if (m_prVel.contains(x, y)) {
        int best = -1, bestD = 9;
        for (size_t i = 0; i < rg.notes.size(); ++i) { const int d = std::abs(xOf(rg.notes[i].start) + 2 - x); if (d < bestD) { bestD = d; best = int(i); } }
        if (best >= 0) { m_prSel = best; m_prVelNote = best; m_drag = Drag::Velocity; m_prChecked = false; mouseMove(x, y, 0); }
        return true;
    }
    if (!m_prGrid.contains(x, y)) return false;
    const double beat = double(x - m_prGrid.x + m_prScroll) / m_prPpb;
    const int pitch = m_prTop - (y - m_prGrid.y) / m_prRowH;
    int hit = -1;
    for (int i = int(rg.notes.size()) - 1; i >= 0; --i) {
        const auto& n = rg.notes[size_t(i)];
        const Rect nr{xOf(n.start), yOf(n.pitch), std::max(3, int(n.length * m_prPpb)), m_prRowH};
        if (nr.contains(x, y)) { hit = i; break; }
    }
    m_prChecked = false;
    if (button == 1) {
        if (hit >= 0) { m_s.checkpoint(); auto notes = rg.notes; notes.erase(notes.begin() + hit); m_s.setRegionNotes(t, ri, notes); m_prSel = -1; }
        return true;
    }
    if (hit >= 0) {
        const auto& n = rg.notes[size_t(hit)];
        m_prSel = hit;
        const Rect nr{xOf(n.start), yOf(n.pitch), std::max(3, int(n.length * m_prPpb)), m_prRowH};
        m_prDragOff = beat - n.start;
        m_prDragPitch = pitch - n.pitch;
        m_prMinLen = snapStep() > 0 ? snapStep() : 0.0625;
        m_drag = (nr.right() - x <= 6 && nr.w > 12) ? Drag::NoteResize : Drag::Note;
        auditionNote(t, n.pitch);
        return true;
    }
    // empty space: add a note and keep dragging to set its length
    if (pitch < 0 || pitch > 127) return true;
    const double st = snapStep() > 0 ? snapStep() : 0.25;
    const double start = std::max(0.0, std::floor(beat / st) * st);
    m_s.checkpoint();
    m_prChecked = true;
    auto notes = rg.notes;
    notes.push_back({pitch, start, st, int(last("prVel", 100))});
    m_s.setRegionNotes(t, ri, notes);
    m_prSel = int(notes.size()) - 1;
    m_prMinLen = st;
    m_drag = Drag::NoteResize;
    auditionNote(t, pitch);
    return true;
}

void NativeUi::paneMouseMove(int x, int y) {
    int t, ri;
    if (!pianoRegion(t, ri) || m_prSel < 0 || m_prSel >= int(m_tracks[size_t(t)].regions[size_t(ri)].notes.size())) return;
    auto notes = m_tracks[size_t(t)].regions[size_t(ri)].notes;
    core::Note& n = notes[size_t(m_prSel)];
    const double len = m_s.regionBeats(m_tracks[size_t(t)].regions[size_t(ri)]);
    const double beat = double(x - m_prGrid.x + m_prScroll) / m_prPpb;
    const double st = snapStep() > 0 ? snapStep() : 0.0;
    auto snap = [&](double b) { return st > 0 ? std::round(b / st) * st : b; };
    if (!m_prChecked) { m_s.checkpoint(); m_prChecked = true; }
    if (m_drag == Drag::Note) {
        n.start = std::clamp(snap(beat - m_prDragOff), 0.0, std::max(0.0, len - 0.01));
        const int pitch = std::clamp(m_prTop - (y - m_prGrid.y) / m_prRowH - m_prDragPitch, 0, 127);
        if (pitch != n.pitch) { n.pitch = pitch; auditionNote(t, pitch); }
    } else if (m_drag == Drag::NoteResize) {
        n.length = std::clamp(snap(beat - n.start + (st > 0 ? st * 0.5 : 0.0)), m_prMinLen, std::max(m_prMinLen, len - n.start));
    } else if (m_drag == Drag::Velocity) {
        n.velocity = std::clamp(int(std::lround((m_prVel.bottom() - 4 - y) * 127.0 / (m_prVel.h - 8))), 1, 127);
        m_last["prVel"] = n.velocity;
    } else return;
    m_s.setRegionNotes(t, ri, notes);
}

void NativeUi::paneMouseUp() {
    if (m_auditionPitch >= 0) { m_s.noteOff(m_auditionTrack, m_auditionPitch); m_auditionPitch = -1; }
}

bool NativeUi::paneWheel(int x, int y, double dx, double dy, int mods) {
    const int cur = m_tabExtra ? m_tabExtra : int(m_tab);
    const Rect pane = paneContent();
    if (!pane.contains(x, y)) return false;
    if (cur == int(Tab::PianoRoll) && !m_prGrid.empty()) {
        if (mods & ModCtrl) { const double before = (x - m_prGrid.x + m_prScroll) / m_prPpb; m_prPpb = std::clamp(m_prPpb * (dy > 0 ? 1.15 : 1 / 1.15), 10.0, 600.0); m_prScroll = std::max(0.0, before * m_prPpb - (x - m_prGrid.x)); }
        else if ((mods & ModShift) || std::fabs(dx) > 0) m_prScroll = std::max(0.0, m_prScroll - (std::fabs(dx) > 0 ? dx : dy) * 60);
        else m_prTop = std::clamp(m_prTop + (dy > 0 ? 3 : -3), m_prGrid.h / m_prRowH, 127);
        return true;
    }
    if (cur == int(Tab::Loops)) { m_loopScroll -= dy * 2; return true; }
    return false;
}

bool NativeUi::paneKey(int k, int mods) {
    (void)mods;
    const int cur = m_tabExtra ? m_tabExtra : int(m_tab);
    int t, ri;
    if (cur == int(Tab::PianoRoll) && pianoRegion(t, ri) && m_prSel >= 0 && (k == KeyDelete || k == KeyBackspace)) {
        auto notes = m_tracks[size_t(t)].regions[size_t(ri)].notes;
        if (m_prSel < int(notes.size())) { m_s.checkpoint(); notes.erase(notes.begin() + m_prSel); m_s.setRegionNotes(t, ri, notes); m_prSel = -1; return true; }
    }
    return false;
}

// ---------------------------------------------------------------- automation and tempo lane
void NativeUi::drawAutomation(Canvas& c, const Rect& r) {
    const bool tempo = m_autoParam == 4;
    if (!tempo && (m_selTrack < 0 || m_selTrack >= int(m_tracks.size()))) m_autoParam = 4;
    static const char* kNames[] = {"Volume", "Pan", "Send A", "Send B", "Tempo"};
    int x = r.x + 14;
    for (int i = 0; i < 5; ++i) {
        const int w = c.textWidth(kNames[i], Font::Bold11) + 22;
        uiAction(c, {x, r.y + 8, w, 22}, kNames[i], [this, i] { m_autoParam = i; m_autoSel = -1; }, m_autoParam == i, kAccent);
        x += w + 6;
    }
    c.textIn({x + 12, r.y + 8, r.right() - x - 24, 22}, "Click to add a point, drag to move it, right-click to delete it.", kDim, Font::Regular11, Align::Right);
    m_autoLane = {kHeaderW, r.y + 40, r.w - kHeaderW - 8, r.h - 48};
    c.fillRect(m_autoLane, rgba(24, 26, 31));
    c.textIn({r.x + 14, m_autoLane.y, kHeaderW - 24, 20}, tempo ? "Project tempo" : trackTitle(m_selTrack), kText, Font::Bold11);
    float lo = 20, hi = 300;
    std::vector<core::AutoPoint> pts;
    float base = 0;
    const auto ap = tempo ? core::AutoParam::Volume : core::AutoParam(m_autoParam);
    if (tempo) { for (const auto& p : m_s.tempoMap()->tempoPoints()) pts.push_back({p.beat, float(p.bpm)}); base = float(m_s.bpm()); }
    else { const core::Track& t = m_tracks[size_t(m_selTrack)]; pts = t.autoOf(ap); base = t.baseValue(ap); lo = core::autoRange(ap).first; hi = core::autoRange(ap).second; }
    auto yOf = [&](float v) { return m_autoLane.bottom() - 6 - int((v - lo) / (hi - lo) * (m_autoLane.h - 12)); };
    // value axis
    for (int i = 0; i <= 4; ++i) {
        const float v = lo + (hi - lo) * float(i) / 4.f;
        c.hline(m_autoLane.x, m_autoLane.right() - 1, yOf(v), withAlpha(rgba(255, 255, 255), 14));
        c.textIn({r.x + 14, yOf(v) - 8, kHeaderW - 24, 16}, num(tempo || std::fabs(hi) > 5 ? "%.0f" : "%.2f", v), kDim, Font::Regular11, Align::Right);
    }
    const auto tm = m_s.tempoMap();
    c.pushClip(m_autoLane);
    for (int bar = std::max(1, tm->barAt(std::max(0.0, xToBeat(kHeaderW)))); tm->barStart(bar) <= xToBeat(m_w) + 1; ++bar) c.vline(int(beatToX(tm->barStart(bar))), m_autoLane.y, m_autoLane.bottom() - 1, withAlpha(rgba(255, 255, 255), 26));
    const Rgba col = tempo ? kAmber : (m_selTrack >= 0 ? trackColor(m_tracks[size_t(m_selTrack)]) : kAccent);
    if (pts.empty()) c.hline(m_autoLane.x, m_autoLane.right() - 1, yOf(base), withAlpha(col, 160));
    else {
        c.line(m_autoLane.x, yOf(pts.front().value), beatToX(pts.front().beat), yOf(pts.front().value), col);
        for (size_t i = 0; i + 1 < pts.size(); ++i) c.line(beatToX(pts[i].beat), yOf(pts[i].value), beatToX(pts[i + 1].beat), yOf(pts[i + 1].value), col);
        c.line(beatToX(pts.back().beat), yOf(pts.back().value), m_autoLane.right(), yOf(pts.back().value), col);
        for (size_t i = 0; i < pts.size(); ++i) {
            const double px = beatToX(pts[i].beat), py = yOf(pts[i].value);
            c.fillCircle(px, py, 5.5, int(i) == m_autoSel ? rgba(245, 248, 252) : col);
            c.strokeCircle(px, py, 5.5, 1.2, rgba(10, 12, 16));
            if (int(i) == m_autoSel) c.text(int(px) + 9, int(py) - 18, num(tempo ? "%.1f BPM" : "%.2f", pts[i].value), kText, Font::Regular11);
        }
    }
    const double px = beatToX(m_s.positionBeats());
    c.vline(int(px), m_autoLane.y, m_autoLane.bottom() - 1, withAlpha(kAccent, 235));
    c.popClip();
    addHit(m_autoLane, Hit::AutoLane);
}

bool NativeUi::autoMouseDown(int x, int y, int button) {
    if (!m_autoLane.contains(x, y)) return false;
    const bool tempo = m_autoParam == 4;
    const auto ap = tempo ? core::AutoParam::Volume : core::AutoParam(m_autoParam);
    float lo = 20, hi = 300;
    std::vector<core::AutoPoint> pts;
    if (tempo) { for (const auto& p : m_s.tempoMap()->tempoPoints()) pts.push_back({p.beat, float(p.bpm)}); }
    else { if (m_selTrack < 0 || m_selTrack >= int(m_tracks.size())) return true; pts = m_tracks[size_t(m_selTrack)].autoOf(ap); lo = core::autoRange(ap).first; hi = core::autoRange(ap).second; }
    auto yOf = [&](float v) { return m_autoLane.bottom() - 6 - int((v - lo) / (hi - lo) * (m_autoLane.h - 12)); };
    auto valueAt = [&](int py) { return lo + float(std::clamp(double(m_autoLane.bottom() - 6 - py) / std::max(1, m_autoLane.h - 12), 0.0, 1.0)) * (hi - lo); };
    int hit = -1;
    for (size_t i = 0; i < pts.size(); ++i) if (std::hypot(beatToX(pts[i].beat) - x, yOf(pts[i].value) - y) < 9) { hit = int(i); break; }
    auto write = [&](const std::vector<core::AutoPoint>& v, bool commit) {
        if (tempo) {
            std::vector<dsp::TempoPoint> tp;
            for (const auto& p : v) tp.push_back({p.beat, double(p.value), false});
            m_s.setTempoPoints(tp, commit);
        } else m_s.setAuto(m_selTrack, ap, v);
    };
    if (button == 1) {
        if (hit >= 0 && !(tempo && pts.size() <= 1)) { m_s.checkpoint(); pts.erase(pts.begin() + hit); write(pts, true); m_autoSel = -1; }
        return true;
    }
    m_s.checkpoint();
    if (hit < 0) {
        core::AutoPoint np{std::max(0.0, snapBeat(xToBeat(x))), valueAt(y)};
        if (tempo) np.value = std::clamp(np.value, 20.f, 300.f);
        for (const auto& p : pts) if (std::fabs(p.beat - np.beat) < 1e-6) np.beat += 0.01;
        pts.push_back(np);
        std::sort(pts.begin(), pts.end(), [](const core::AutoPoint& a, const core::AutoPoint& b) { return a.beat < b.beat; });
        write(pts, false);
        for (size_t i = 0; i < pts.size(); ++i) if (pts[i].beat == np.beat) hit = int(i);
    }
    m_autoSel = hit;
    m_drag = Drag::AutoPoint;
    return true;
}

void NativeUi::autoMouseMove(int x, int y) {
    if (m_autoSel < 0) return;
    const bool tempo = m_autoParam == 4;
    const auto ap = tempo ? core::AutoParam::Volume : core::AutoParam(m_autoParam);
    float lo = 20, hi = 300;
    std::vector<core::AutoPoint> pts;
    if (tempo) { for (const auto& p : m_s.tempoMap()->tempoPoints()) pts.push_back({p.beat, float(p.bpm)}); }
    else { if (m_selTrack < 0 || m_selTrack >= int(m_tracks.size())) return; pts = m_tracks[size_t(m_selTrack)].autoOf(ap); lo = core::autoRange(ap).first; hi = core::autoRange(ap).second; }
    if (m_autoSel >= int(pts.size())) return;
    core::AutoPoint& p = pts[size_t(m_autoSel)];
    const double b0 = m_autoSel > 0 ? pts[size_t(m_autoSel) - 1].beat + 0.01 : 0.0;
    const double b1 = m_autoSel + 1 < int(pts.size()) ? pts[size_t(m_autoSel) + 1].beat - 0.01 : 1e9;
    p.beat = m_autoSel == 0 && tempo ? 0.0 : std::clamp(snapBeat(xToBeat(x)), b0, b1);
    p.value = lo + float(std::clamp(double(m_autoLane.bottom() - 6 - y) / std::max(1, m_autoLane.h - 12), 0.0, 1.0)) * (hi - lo);
    if (tempo) {
        std::vector<dsp::TempoPoint> tp;
        for (const auto& q : pts) tp.push_back({q.beat, double(q.value), false});
        m_s.setTempoPoints(tp, false);
    } else m_s.setAuto(m_selTrack, ap, pts);
}

}  // namespace ui
