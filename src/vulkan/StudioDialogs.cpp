// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Vulkan UI: the modal parameter dialog and every dialog built on it, plus the file commands.
#include "IconFont.h"
#include "../core/LogicImport.h"
#include <algorithm>
#include <cstdio>
#include "../dsp/AudioOps.h"
#include "Icons.h"
#include "../core/ExportFormats.h"
#include "../core/FileTypes.h"
#include "Studio.h"
#include "Theme.h"

namespace vkui {
using namespace theme;

DlgField DlgField::number(std::string l, double v, double lo, double hi, double st, int dec, std::string suffix) { DlgField f; f.kind = Number; f.label = std::move(l); f.value = v; f.min = lo; f.max = hi; f.step = st; f.decimals = dec; f.suffix = std::move(suffix); return f; }
DlgField DlgField::slider(std::string l, double v, double lo, double hi, int dec, std::string suffix) { DlgField f; f.kind = Slider; f.label = std::move(l); f.value = v; f.min = lo; f.max = hi; f.decimals = dec; f.suffix = std::move(suffix); return f; }
DlgField DlgField::check(std::string l, bool on) { DlgField f; f.kind = Check; f.label = std::move(l); f.checked = on; return f; }
DlgField DlgField::choice(std::string l, std::vector<std::string> items, int index) { DlgField f; f.kind = Choice; f.label = std::move(l); f.choices = std::move(items); f.index = index; return f; }
DlgField DlgField::input(std::string l, std::string text) { DlgField f; f.kind = Text; f.label = std::move(l); f.text = std::move(text); return f; }
DlgField DlgField::note(std::string text) { DlgField f; f.kind = Label; f.text = std::move(text); return f; }

namespace {
const std::vector<std::string> kGridChoices = {"1/4", "1/8", "1/16", "1/32", "1/8 Triplet", "1/16 Triplet"};
const double kGridValues[] = {1, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0};
std::vector<std::string> scaleNames() { std::vector<std::string> v; for (int i = 0; i < int(core::ScaleParams::Count); ++i) v.push_back(core::scaleName(i)); return v; }
std::vector<std::string> chordNames() { std::vector<std::string> v; for (int i = 0; i < int(core::ChordParams::Count); ++i) v.push_back(core::chordName(i)); return v; }
std::vector<std::string> keyNames() { return {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}; }
}  // namespace

// ------------------------------------------------------------------------------------------------------- dialog engine

void Studio::openDialog(const std::string& title, std::vector<DlgField> fields, std::function<void(const std::vector<DlgField>&)> onOk, const std::string& okLabel) {
    m_dlg = Dlg{};
    m_dlg.open = true;
    m_dlg.title = title;
    m_dlg.fields = std::move(fields);
    m_dlg.onOk = std::move(onOk);
    m_dlg.okLabel = okLabel;
}

void Studio::finishDialog(bool ok) {
    if (!m_dlg.open) return;
    auto cb = m_dlg.onOk;
    const auto fields = m_dlg.fields;
    m_dlg.open = false;
    m_g->clearFocus();
    if (ok && cb) { cb(fields); m_modelDirty = true; }
}

void Studio::drawDialog() {
    Gui& g = *m_g;
    DrawList& dl = *m_dl;
    dl.rect({0, 0, dl.width(), dl.height()}, withAlpha(rgb(0, 0, 0), 140));
    float rows = 0;
    for (const DlgField& f : m_dlg.fields) rows += f.kind == DlgField::Label ? 28.f : 34.f;
    const float W = std::min(480.f, dl.width() - 40), H = std::min(rows + 112.f, dl.height() - 60);
    const Rect panel{(dl.width() - W) * 0.5f, (dl.height() - H) * 0.5f - 10, W, H};
    dl.shadow(panel, 12, 20, withAlpha(rgb(0, 0, 0), 170));
    dl.rrect(panel, 12, bg1);
    dl.rrectStroke(panel, 12, 1, border);
    dl.textIn({panel.x + 20, panel.y + 12, W - 40, 26}, tr(m_dlg.title), text, {15, true});
    dl.hline(panel.x + 16, panel.right() - 16, panel.y + 46, border);
    const Rect body{panel.x, panel.y + 52, W, H - 52 - 56};
    if (g.hovering(body) && g.wheelY() != 0) { m_dlg.scroll = std::clamp(m_dlg.scroll - g.wheelY() * 36.f, 0.f, std::max(0.f, rows - body.h)); g.consumeWheel(); }
    dl.pushClip(body);
    float y = body.y + 6 - m_dlg.scroll;
    int idx = 0;
    for (DlgField& f : m_dlg.fields) {
        const float h = f.kind == DlgField::Label ? 28.f : 34.f;
        const Rect row{panel.x + 20, y, W - 40, h - 4};
        y += h;
        ++idx;
        if (row.bottom() < body.y || row.y > body.bottom()) continue;
        const Id fid = id("dlg-field", idx);
        if (f.kind == DlgField::Label) { dl.textIn(row, tr(f.text), textDim, small); continue; }
        const float lw = f.kind == DlgField::Check ? 0.f : std::min(190.f, row.w * 0.42f);
        if (lw > 0) dl.textIn({row.x, row.y, lw - 8, row.h}, tr(f.label), text, normal);
        const Rect c{row.x + lw, row.y, row.w - lw, row.h};
        switch (f.kind) {
        case DlgField::Number: g.numberField(fid, {c.x, c.y + 1, std::min(150.f, c.w), c.h - 2}, f.value, f.min, f.max, f.step, f.decimals, f.suffix, f.value); break;
        case DlgField::Slider: {
            float v = float(f.value);
            char b[32]; std::snprintf(b, sizeof b, "%.*f%s", f.decimals, double(v), f.suffix.c_str());
            if (g.slider(fid, c, v, float(f.min), float(f.max), float(f.value), b)) f.value = v;
            break;
        }
        case DlgField::Check: g.checkbox(fid, row, f.label, f.checked); break;
        case DlgField::Choice: g.dropdown(fid, {c.x, c.y + 1, std::min(240.f, c.w), c.h - 2}, f.index, f.choices); break;
        case DlgField::Text: g.textField(fid, {c.x, c.y + 1, c.w, c.h - 2}, f.text); break;
        default: break;
        }
    }
    dl.popClip();
    // footer
    const float by = panel.bottom() - 46;
    bool ok = g.button(id("dlg-ok"), {panel.right() - 20 - 96, by, 96, 32}, m_dlg.okLabel.empty() ? "OK" : m_dlg.okLabel, ButtonKind::Accent);
    const bool cancel = g.button(id("dlg-cancel"), {panel.right() - 20 - 96 - 8 - 90, by, 90, 32}, "Cancel");
    bool extra = false;
    if (!m_dlg.extraLabel.empty()) extra = g.button(id("dlg-extra"), {panel.x + 20, by, 150, 32}, m_dlg.extraLabel, ButtonKind::Danger);
    if (!g.textFocused()) { if (g.key(KeyEnter, 0, false, true)) ok = true; }
    const bool esc = g.key(KeyEscape, 0, false, true);
    if (extra) { auto f = m_dlg.onExtra; m_dlg.open = false; if (f) f(); return; }
    if (ok) finishDialog(true);
    else if (cancel || esc) finishDialog(false);
}

// ------------------------------------------------------------------------------------------------------------ file ops

void Studio::addRecent(const std::string& path) {
    if (path.empty()) return;
    std::vector<std::string> list = {path};
    for (int i = 1; i <= 5; ++i) {
        auto it = m_host.settings.find("recent" + std::to_string(i));
        if (it != m_host.settings.end() && !it->second.empty() && it->second != path && list.size() < 5) list.push_back(it->second);
    }
    for (int i = 1; i <= 5; ++i) m_host.settings["recent" + std::to_string(i)] = i <= int(list.size()) ? list[size_t(i - 1)] : std::string();
    if (m_host.applySettings) m_host.applySettings(m_host.settings);
}

void Studio::openFile(const std::string& p) {
    std::string err;
    if (m_s.loadProject(p, &err)) { message(m_s.loadedFromNewerVersion() ? "Opened a project saved by a newer MiniDAW: some settings may be missing." : "Opened " + p); addRecent(p); m_selTrack = 0; m_selRegion = -1; }
    else message("Could not open the project: " + err);
    if (m_host.setTitle) m_host.setTitle("MiniDAW - " + p);
    m_modelDirty = true;
}

void Studio::importAudioFile(const std::string& p) {
    const bool audio = m_selTrack >= 0 && m_selTrack < int(m_tracks.size()) && m_tracks[size_t(m_selTrack)].type == core::TrackType::Audio;
    m_s.importAudio(p, audio ? m_selTrack : -1, m_s.positionBeats());
    message(fmt("Importing %1", p));
    m_modelDirty = true;
}

void Studio::importLogic(const std::string& folder) {
    std::string summary;
    core::importLogicPackage(m_s, folder, &summary);
    message(summary);
    m_modelDirty = true;
}

void Studio::openAny(const std::string& p) {
    switch (core::classifyFile(p)) {
    case core::FileKind::Project: guardUnsaved([this, p] { openFile(p); }); break;
    case core::FileKind::Audio: importAudioFile(p); break;
    case core::FileKind::Logic: importLogic(p); break;
    case core::FileKind::Midi: {
        std::string err;
        if (m_s.importMidiFile(p, &err)) message(fmt("Imported %1", p));
        else message(fmt("Could not import: %1", err));
        m_modelDirty = true;
        break;
    }
    default: {
        const std::string e = core::fileExtension(p);
        message(fmt("This file type is not supported (%1). Supported: %2", e.empty() ? std::string("no extension") : e, core::supportedExtensionsText()));
        break;
    }
    }
}

void Studio::guardUnsaved(std::function<void()> proceed) {
    if (!m_s.isDirty()) { proceed(); return; }
    openDialog("Unsaved changes", {DlgField::note("Save changes to the project before continuing?")}, [this, proceed](const std::vector<DlgField>&) {
        doFile("save");
        if (!m_s.isDirty()) proceed();   // still dirty: the save was cancelled or failed, stay in the project
    }, "Save");
    m_dlg.extraLabel = "Discard Changes";
    m_dlg.onExtra = proceed;
}

void Studio::doFile(const std::string& what) {
    auto ask = [&](const char* title, const char* filter, bool save) -> std::string {
        if (!m_host.fileDialog) { message("File dialogs are not available here."); return {}; }
        return m_host.fileDialog(title, filter, save);
    };
    std::string err;
    const char* kProject = "MiniDAW project|*.mdaw";
    if (what == "new") { guardUnsaved([this] { doFile("newnow"); }); return; }
    if (what == "open") { const std::string p = ask("Open", core::openFilter().c_str(), false); if (!p.empty()) openAny(p); return; }
    if (what == "recover") { guardUnsaved([this] { doFile("recovernow"); }); return; }
    if (what == "newnow") { m_s.newProject(); m_selTrack = 0; m_selRegion = -1; message("New project."); }
    else if (what == "opennow") { const std::string p = ask("Open", core::openFilter().c_str(), false); if (!p.empty()) openAny(p); }
    else if (what == "recovernow") {
        if (!m_s.hasAutosave()) message("There is no autosaved project to recover.");
        else if (m_s.loadProject(m_s.autosavePath(), &err)) { m_s.markRecovered(); m_selTrack = 0; m_selRegion = -1; message("Recovered the autosaved project. Please save it."); }
        else message("Could not open the project: " + err);
    } else if (what == "save" || what == "saveas") {
        std::string p = what == "save" ? m_s.projectPath() : std::string();
        if (p.empty()) p = ask("Save Project", kProject, true);
        if (!p.empty()) { if (p.find('.') == std::string::npos) p += ".mdaw"; if (m_s.saveProject(p, &err)) { message("Saved " + p); addRecent(p); } else message("Could not save: " + err); }
    } else if (what == "import") {
        const std::string p = ask("Import Audio", core::audioFilter().c_str(), false);
        if (!p.empty()) importAudioFile(p);
    } else if (what == "importmidi") {
        dlgImportMidi();
    } else if (what == "exportmidi") {
        std::string p = ask("Export MIDI File", "MIDI files|*.mid", true);
        if (!p.empty()) { if (p.find('.') == std::string::npos) p += ".mid"; if (m_s.exportMidiFile(p, &err)) message("Exported " + p); else message("Could not export: " + err); }
    }
    if (m_host.setTitle) m_host.setTitle(m_s.projectPath().empty() ? "MiniDAW" : "MiniDAW - " + m_s.projectPath());
    m_modelDirty = true;
}

void Studio::tapTempo() {
    if (!m_taps.empty() && m_time - m_taps.back() > 2.0) m_taps.clear();
    m_taps.push_back(m_time);
    if (m_taps.size() > 6) m_taps.erase(m_taps.begin());
    if (m_taps.size() >= 2) {
        const double avg = (m_taps.back() - m_taps.front()) / double(m_taps.size() - 1);
        if (avg > 0.15 && avg < 3.0) { m_s.setBpm(60.0 / avg); message("Tempo: " + std::to_string(int(std::lround(60.0 / avg))) + " BPM"); }
    }
}

void Studio::gotoMarker(int dir) {
    const double pos = m_s.positionBeats();
    double best = -1;
    for (const core::Marker& m : m_markers) {
        if (dir > 0 && m.beat > pos + 1e-6 && (best < 0 || m.beat < best)) best = m.beat;
        if (dir < 0 && m.beat < pos - 1e-6 && (best < 0 || m.beat > best)) best = m.beat;
    }
    if (best >= 0) { m_s.seekBeats(best); ensureVisible(best); } else message(dir > 0 ? "No next marker." : "No previous marker.");
}

// ----------------------------------------------------------------------------------------------------------- MIDI dialogs

void Studio::dlgQuantize() {
    if (!hasMidiRegion()) return;
    const int t = m_selTrack, r = m_selRegion;
    openDialog("Quantize", {DlgField::choice("Grid", kGridChoices, 2), DlgField::slider("Strength", 1.0, 0, 1, 2), DlgField::slider("Swing", 0.5, 0.5, 0.75, 3), DlgField::check("Also snap note lengths", false)}, [this, t, r](const std::vector<DlgField>& v) {
        core::QuantizeParams p;
        p.grid = kGridValues[v[0].index]; p.strength = v[1].value; p.swing = v[2].value; p.lengths = v[3].checked;
        m_s.quantizeRegion(t, r, p);
    });
}
void Studio::dlgScaleQuantize() {
    if (!hasMidiRegion()) return;
    const int t = m_selTrack, r = m_selRegion;
    openDialog("Scale Quantize", {DlgField::choice("Key", keyNames(), m_scaleRoot), DlgField::choice("Scale", scaleNames(), m_scaleType)}, [this, t, r](const std::vector<DlgField>& v) {
        core::ScaleParams p; p.root = v[0].index; p.scale = v[1].index; m_s.scaleQuantizeRegion(t, r, p);
    });
}
void Studio::dlgHumanize() {
    if (!hasMidiRegion()) return;
    const int t = m_selTrack, r = m_selRegion;
    openDialog("Humanize", {DlgField::number("Timing", 0.03, 0, 0.25, 0.005, 3, " beat"), DlgField::number("Velocity", 10, 0, 64, 1, 0), DlgField::number("Seed", 1, 1, 9999, 1, 0)}, [this, t, r](const std::vector<DlgField>& v) {
        core::HumanizeParams p; p.timing = v[0].value; p.velocity = int(v[1].value); p.seed = int(v[2].value); m_s.humanizeRegion(t, r, p);
    });
}
void Studio::dlgChordTrigger() {
    if (!hasMidiRegion()) return;
    const int t = m_selTrack, r = m_selRegion;
    openDialog("Chord Trigger", {DlgField::choice("Chord", chordNames(), 0), DlgField::number("Inversion", 0, 0, 3, 1, 0)}, [this, t, r](const std::vector<DlgField>& v) {
        core::ChordParams p; p.type = v[0].index; p.inversion = int(v[1].value); m_s.chordTriggerRegion(t, r, p);
    });
}
void Studio::dlgArpeggiator() {
    if (!hasMidiRegion()) return;
    const int t = m_selTrack, r = m_selRegion;
    openDialog("Arpeggiator", {DlgField::choice("Pattern", {"Up", "Down", "Up & Down", "Down & Up", "Random", "As Played"}, 0), DlgField::choice("Rate", {"1/4", "1/8", "1/16", "1/32"}, 2),
                               DlgField::number("Octaves", 1, 1, 4, 1, 0), DlgField::slider("Gate", 0.8, 0.05, 1, 2), DlgField::number("Seed", 1, 1, 9999, 1, 0)}, [this, t, r](const std::vector<DlgField>& v) {
        core::ArpParams p; const double rates[] = {1, 0.5, 0.25, 0.125};
        p.pattern = v[0].index; p.rate = rates[v[1].index]; p.octaves = int(v[2].value); p.gate = v[3].value; p.seed = int(v[4].value);
        m_s.arpeggiateRegion(t, r, p);
    });
}
void Studio::dlgTranspose() {
    if (!hasMidiRegion()) return;
    const int t = m_selTrack, r = m_selRegion;
    openDialog("Transpose", {DlgField::number("Interval", 12, -48, 48, 1, 0, " semitones")}, [this, t, r](const std::vector<DlgField>& v) { m_s.transposeRegion(t, r, int(v[0].value)); });
}
void Studio::dlgVelocityRamp() {
    if (!hasMidiRegion()) return;
    const int t = m_selTrack, r = m_selRegion;
    openDialog("Crescendo / Decrescendo", {DlgField::number("Start velocity", 30, 1, 127), DlgField::number("End velocity", 110, 1, 127)}, [this, t, r](const std::vector<DlgField>& v) { m_s.velocityRampRegion(t, r, int(v[0].value), int(v[1].value)); });
}
void Studio::dlgScaleVelocity() {
    if (!hasMidiRegion()) return;
    const int t = m_selTrack, r = m_selRegion;
    openDialog("Scale Velocity", {DlgField::number("Percent", 100, 10, 300, 1, 0, " %"), DlgField::number("Offset", 0, -100, 100)}, [this, t, r](const std::vector<DlgField>& v) { m_s.scaleVelocityRegion(t, r, int(v[0].value), int(v[1].value)); });
}
void Studio::dlgFixedLength() {
    if (!hasMidiRegion()) return;
    const int t = m_selTrack, r = m_selRegion;
    openDialog("Fixed Note Length", {DlgField::choice("Length", kGridChoices, 2)}, [this, t, r](const std::vector<DlgField>& v) { m_s.fixedLengthRegion(t, r, kGridValues[v[0].index]); });
}

// ---- Logic Pro / Ableton Live / FL Studio / Cubase / Pro Tools features

void Studio::dlgRiff() {
    if (!hasMidiRegion()) { message("Select a MIDI region for the Riff Machine."); return; }
    const int t = m_selTrack, r = m_selRegion;
    m_riff.root = m_scaleRoot;
    m_riff.scale = m_scaleType;
    const double steps[] = {1, 0.5, 0.25, 0.125};
    int stepIdx = m_riff.stepLength >= 1 ? 0 : m_riff.stepLength >= 0.5 ? 1 : m_riff.stepLength >= 0.25 ? 2 : 3;
    openDialog("Riff Machine", {DlgField::note("Writes a new melody in the scale into this region (replaces its notes)."), DlgField::choice("Key", keyNames(), m_riff.root), DlgField::choice("Scale", scaleNames(), m_riff.scale),
                                DlgField::number("Lowest octave", m_riff.octave, 1, 7), DlgField::number("Range", m_riff.range, 5, 36, 1, 0, " semitones"), DlgField::choice("Note length", {"1/4", "1/8", "1/16", "1/32"}, stepIdx),
                                DlgField::slider("Density", m_riff.density, 0.1, 1, 2), DlgField::number("Largest leap", m_riff.leap, 1, 7, 1, 0, " steps"), DlgField::number("Velocity from", m_riff.velocityMin, 1, 127),
                                DlgField::number("Velocity to", m_riff.velocityMax, 1, 127), DlgField::number("Seed (new number = new riff)", m_riff.seed, 1, 99999)},
               [this, t, r, steps](const std::vector<DlgField>& v) {
        m_riff.root = v[1].index; m_riff.scale = v[2].index; m_riff.octave = int(v[3].value); m_riff.range = int(v[4].value); m_riff.stepLength = steps[v[5].index];
        m_riff.density = v[6].value; m_riff.leap = int(v[7].value); m_riff.velocityMin = int(v[8].value); m_riff.velocityMax = int(v[9].value); m_riff.seed = int(v[10].value);
        m_scaleRoot = m_riff.root; m_scaleType = m_riff.scale;
        if (m_s.riffRegion(t, r, m_riff)) message(fmt("Riff written (seed %1).", std::to_string(m_riff.seed)));
    }, "Write Riff");
}

void Studio::rollRiff() {
    if (!hasMidiRegion()) { message("Select a MIDI region for the Riff Machine."); return; }
    m_riff.root = m_scaleRoot; m_riff.scale = m_scaleType;
    m_riff.seed = m_riff.seed % 99999 + 1;
    if (m_s.riffRegion(m_selTrack, m_selRegion, m_riff)) message("New riff, seed " + std::to_string(m_riff.seed) + " - press again for another.");
}

void Studio::dlgLogical() {
    if (!hasMidiRegion()) { message("Select a MIDI region for the Logical Editor."); return; }
    const int t = m_selTrack, r = m_selRegion;
    const std::vector<std::string> props = {"Pitch", "Velocity", "Length (beats)", "Position (beats)"};
    std::vector<std::string> actions;
    for (int i = 0; i < 9; ++i) actions.push_back(core::logicalActionName(i));
    openDialog("Logical Editor", {DlgField::note("Filter: every condition must match. Action: applied to the notes that match."), DlgField::choice("Condition 1", props, 1), DlgField::number("   from", 1, 0, 1000, 1, 2), DlgField::number("   to", 40, 0, 1000, 1, 2),
                                  DlgField::check("Add condition 2", false), DlgField::choice("Condition 2", props, 0), DlgField::number("   from", 0, 0, 1000, 1, 2), DlgField::number("   to", 127, 0, 1000, 1, 2),
                                  DlgField::check("Apply to the notes that do NOT match", false), DlgField::choice("Action", actions, int(core::LogicalParams::Delete)), DlgField::number("Value", 12, -1000, 1000, 1, 2)},
               [this, t, r](const std::vector<DlgField>& v) {
        core::LogicalParams p;
        p.conditions.push_back({v[1].index, v[2].value, v[3].value});
        if (v[4].checked) p.conditions.push_back({v[5].index, v[6].value, v[7].value});
        p.invert = v[8].checked; p.action = v[9].index; p.value = v[10].value;
        const int hits = m_s.logicalEditRegion(t, r, p);
        if (hits >= 0) message(fmt(hits == 1 ? "%1 note changed." : "%1 notes changed.", std::to_string(hits)));
    }, "Apply");
}

void Studio::dlgGroove() {
    if (!hasMidiRegion()) return;
    if (!m_s.hasGrooveTemplate()) { message("Make a groove template from a MIDI region first (MIDI > Make Groove Template)."); return; }
    const int t = m_selTrack, r = m_selRegion;
    openDialog("Apply Groove Template", {DlgField::slider("Strength", 1.0, 0, 1, 2)}, [this, t, r](const std::vector<DlgField>& v) { m_s.applyGrooveTemplate(t, r, v[0].value); });
}

void Studio::dlgAdaptTempo() {
    if (!hasAudioRegion()) { message("Select an audio region first."); return; }
    const int t = m_selTrack, r = m_selRegion;
    openDialog("Adapt Project Tempo to Region", {DlgField::note("Sets the project tempo so that the region lasts exactly this many beats. 0 = guess from the audio."), DlgField::number("Region length", 0, 0, 512, 1, 0, " beats")}, [this, t, r](const std::vector<DlgField>& v) { m_s.adaptTempoToRegion(t, r, v[0].value); }, "Adapt");
}

void Studio::dlgBeatDetective() {
    if (!hasAudioRegion()) { message("Select an audio region first."); return; }
    const int t = m_selTrack, r = m_selRegion;
    openDialog("Beat Detective", {DlgField::note("Cuts the region at its transients; the pieces can then be pulled onto the grid."), DlgField::slider("Sensitivity", 0.5, 0, 1, 2), DlgField::choice("Conform to grid", {"Off", "1/4", "1/8", "1/16"}, 0), DlgField::slider("Conform strength", 1.0, 0, 1, 2)}, [this, t, r](const std::vector<DlgField>& v) {
        const double grids[] = {0, 1, 0.5, 0.25};
        const int n = m_s.separateAtTransients(t, r, v[1].value, grids[v[2].index], v[3].value);
        if (n > 0) { m_selRegion = -1; message("Separated into " + std::to_string(n) + " regions."); }
    }, "Separate");
}

// ---- audio region dialogs

void Studio::dlgStripSilence() {
    if (!hasAudioRegion()) return;
    const int t = m_selTrack, r = m_selRegion;
    openDialog("Strip Silence", {DlgField::number("Threshold", -50, -90, -10, 1, 0, " dB"), DlgField::number("Minimum silence", 150, 20, 3000, 10, 0, " ms"), DlgField::number("Pad", 20, 0, 500, 5, 0, " ms")}, [this, t, r](const std::vector<DlgField>& v) {
        const int n = m_s.stripSilenceRegion(t, r, v[0].value, v[1].value, v[2].value);
        message(n > 0 ? "Strip Silence: " + std::to_string(n) + " regions." : "Nothing to strip.");
    });
}
void Studio::dlgNormalize() {
    if (!hasAudioRegion()) return;
    const int t = m_selTrack, r = m_selRegion;
    openDialog("Normalize", {DlgField::number("Normalize to", -1, -24, 0, 0.5, 1, " dB")}, [this, t, r](const std::vector<DlgField>& v) { m_s.normalizeRegion(t, r, float(v[0].value)); });
}
void Studio::dlgFlex() {
    if (!hasAudioRegion()) return;
    const int t = m_selTrack, r = m_selRegion;
    const core::Region& rg = *region();
    openDialog("Follow Tempo and Pitch", {DlgField::check("Follow project tempo", rg.follow), DlgField::number("Source tempo", rg.sourceBpm > 0 ? rg.sourceBpm : m_s.bpm(), 20, 400, 0.5, 1, " BPM"), DlgField::number("Transpose", rg.semitones, -24, 24, 1, 0, " semitones")}, [this, t, r](const std::vector<DlgField>& v) {
        m_s.setRegionFlex(t, r, v[0].checked, v[1].value, v[2].value);
    });
}
void Studio::dlgFades() {
    if (!hasAudioRegion()) return;
    const int t = m_selTrack, r = m_selRegion;
    const core::Region& rg = *region();
    openDialog("Fades and Gain", {DlgField::number("Gain", rg.gainDb, -24, 12, 0.5, 1, " dB"), DlgField::number("Fade in", double(rg.fadeIn) * 1000.0 / ::kSampleRate, 0, 5000, 5, 0, " ms"), DlgField::number("Fade out", double(rg.fadeOut) * 1000.0 / ::kSampleRate, 0, 5000, 5, 0, " ms")}, [this, t, r](const std::vector<DlgField>& v) {
        m_s.setRegionAudio(t, r, float(v[0].value), core::i64(v[1].value * ::kSampleRate / 1000.0), core::i64(v[2].value * ::kSampleRate / 1000.0));
    });
}

// ---- project dialogs

void Studio::dlgTempo() {
    openDialog("Tempo", {DlgField::number("Tempo", m_s.bpm(), 20, 400, 0.5, 1, " BPM")}, [this](const std::vector<DlgField>& v) { m_s.setBpm(v[0].value); });
}
void Studio::dlgTimeSignature() {
    const auto mp = m_s.tempoMap()->meterAtBeat(m_s.positionBeats());
    int denIdx = mp.den == 2 ? 0 : mp.den == 4 ? 1 : mp.den == 8 ? 2 : 3;
    openDialog("Time Signature", {DlgField::number("Beats per bar", mp.num, 1, 32), DlgField::choice("Beat unit", {"2", "4", "8", "16"}, denIdx)}, [this](const std::vector<DlgField>& v) {
        const int dens[] = {2, 4, 8, 16};
        m_s.setMeterPoints({{1, int(v[0].value), dens[v[1].index]}});
    });
}
void Studio::dlgBounce() {
    // File types for other DAWs: WAV / AIFF (16, 24-bit, 32-bit float WAV) at 44.1 - 96 kHz, MP3 with tags, or one file per track (stems)
    openDialog("Bounce / Export Audio",
               {DlgField::choice("Format", core::exportFormatNames(), 1), DlgField::choice("Sample rate", core::exportRateNames(), 0),
                DlgField::choice("MP3 bit rate", core::exportMp3RateNames(), 2), DlgField::check("Only the cycle range", false),
                DlgField::check("Normalize", false), DlgField::number("Normalize to", -1, -24, 0, 0.5, 1, " dB"),
                DlgField::check("Dither (16-bit)", false), DlgField::check("Add a 2 s tail for reverb and echo", true),
                DlgField::check("One file for every track (stems, all the same length)", false),
                DlgField::check("Stems: keep master volume and limiter", true),
                DlgField::check("Stems: keep reverb and echo sends", true),
                DlgField::input("Title (MP3)", ""), DlgField::input("Artist (MP3)", ""), DlgField::input("Album (MP3)", ""),
                DlgField::note("Stems start at the same point and have the same length."),
                DlgField::note("Drop them into Logic Pro, Ableton Live or Cubase at bar 1.")},
               [this](const std::vector<DlgField>& v) {
        core::ExportOptions o;
        core::applyExportChoices(o, v[0].index, v[1].index, v[2].index);
        o.cycleOnly = v[3].checked; o.normalize = v[4].checked; o.normalizeDb = float(v[5].value); o.dither = v[6].checked; o.tail = v[7].checked;
        const bool stems = v[8].checked;
        o.masterEffects = v[9].checked;
        o.stemSends = v[10].checked;
        o.tags.title = v[11].text; o.tags.artist = v[12].text; o.tags.album = v[13].text;
        o.tags.comment = "Made with MiniDAW";
        if (!m_host.fileDialog) { message("File dialogs are not available here."); return; }
        std::string err;
        if (stems) {
            if (!m_host.folderDialog) { message("File dialogs are not available here."); return; }
            const std::string folder = m_host.folderDialog("Export every track to a folder");
            if (folder.empty()) return;
            std::vector<std::string> files;
            if (m_s.exportStems(folder, o, &files, &err)) message(fmt("Exported %1 files.", std::to_string(files.size())));
            else message(fmt("Bounce failed: %1", err));
            return;
        }
        std::string p = m_host.fileDialog("Bounce / Export Audio", core::exportFilter(o.container), true);
        if (p.empty()) return;
        p = core::withExportExtension(p, o.container);
        if (m_s.exportAudio(p, o, &err)) message(fmt("Bounced %1", p)); else message(fmt("Bounce failed: %1", err));
    }, "Bounce");
}
void Studio::dlgImportMidi() {
    openDialog("Import MIDI File",
               {DlgField::choice("Tempo and time signature", {"Adopt if the project is empty", "Use the file's", "Keep the project's"}, 0),
                DlgField::check("One track for every MIDI channel", false),
                DlgField::note("The notes start at bar 1. Markers of the file are added.")},
               [this](const std::vector<DlgField>& v) {
        core::MidiImportOptions opt;
        opt.tempo = v[0].index == 1 ? core::MidiTempoPolicy::UseFile : v[0].index == 2 ? core::MidiTempoPolicy::KeepProject : core::MidiTempoPolicy::Auto;
        opt.splitChannels = v[1].checked;
        if (!m_host.fileDialog) { message("File dialogs are not available here."); return; }
        const std::string p = m_host.fileDialog("Import MIDI File", core::midiFilter(), false);
        if (p.empty()) return;
        std::string err;
        if (m_s.importMidiFile(p, &err, opt)) message(fmt("Imported %1", p)); else message(fmt("Could not import: %1", err));
    }, "Import");
}
void Studio::dlgRenameTrack(int t) {
    if (t < 0 || t >= int(m_tracks.size())) return;
    openDialog("Rename Track", {DlgField::input("Name", m_tracks[size_t(t)].name)}, [this, t](const std::vector<DlgField>& v) { if (!v[0].text.empty()) m_s.renameTrack(t, v[0].text); });
}
void Studio::dlgRenameRegion() {
    if (!hasRegion()) return;
    const int t = m_selTrack, r = m_selRegion;
    openDialog("Rename Region", {DlgField::input("Name", region()->name)}, [this, t, r](const std::vector<DlgField>& v) { if (!v[0].text.empty()) m_s.renameRegion(t, r, v[0].text); });
}
void Studio::dlgSnapshotName(int index) {
    const auto snaps = m_s.mixSnapshots();
    if (index < 0 || index >= int(snaps.size())) return;
    openDialog("Rename Snapshot", {DlgField::input("Name", snaps[size_t(index)].name)}, [this, index](const std::vector<DlgField>& v) { m_s.renameMixSnapshot(index, v[0].text); });
}
void Studio::dlgAux(int bus) {
    auto fx = m_s.auxEffect(bus);
    if (!fx) return;
    std::vector<DlgField> f;
    std::vector<dsp::Param*> params;
    for (const auto& p : fx->params) {
        params.push_back(p.get());
        if (p->stepped && !p->choices.empty()) f.push_back(DlgField::choice(p->name, p->choices, int(p->get())));
        else f.push_back(DlgField::slider(p->name, p->get(), p->min, p->max, p->stepped ? 0 : (p->max - p->min > 50.f ? 1 : 2), p->unit.empty() ? std::string() : " " + p->unit));
    }
    openDialog(bus == 0 ? "Aux A: Reverb" : "Aux B: Echo", std::move(f), [this, params](const std::vector<DlgField>& v) {
        for (size_t i = 0; i < params.size(); ++i) params[i]->set(params[i]->stepped && !params[i]->choices.empty() ? float(v[i].index) : float(v[i].value));
        m_s.markDirty();
        message("Aux effect updated.");
    });
}

void Studio::dlgAbout() {
    openDialog("About MiniDAW", {DlgField::note("MiniDAW - a lightweight Logic Pro-inspired digital audio workstation."), DlgField::note("Vulkan user interface. GPL-3.0-or-later."),
                                 DlgField::note(fmt(m_host.portable ? "Portable mode: settings are kept in %1." : "Settings are kept in %1.", m_host.dataDir + "\\vulkan-settings.ini"))}, nullptr, "Close");
}

void Studio::dlgPreferences() {
    // Reading the audio / MIDI device lists can take seconds (Bluetooth, virtual drivers): the host does it in the background, so the window never waits for it.
    if (m_host.refreshDevices) m_host.refreshDevices();
    if (m_host.devicesReady && !m_host.devicesReady()) { message("Looking for audio devices... open Preferences again in a moment."); return; }
    auto get = [&](const char* k, const std::string& def) { auto it = m_host.settings.find(k); return it == m_host.settings.end() ? def : it->second; };
    const auto outs = m_host.audioDevices ? m_host.audioDevices(false) : std::vector<std::pair<std::string, std::string>>{};
    const auto ins = m_host.audioDevices ? m_host.audioDevices(true) : std::vector<std::pair<std::string, std::string>>{};
    const auto midis = m_host.midiDevices ? m_host.midiDevices() : std::vector<std::string>{};
    const auto langs = m_host.languages ? m_host.languages() : std::vector<std::pair<std::string, std::string>>{};
    auto names = [](const std::vector<std::pair<std::string, std::string>>& v) { std::vector<std::string> n = {"System default"}; for (const auto& d : v) n.push_back(d.second); return n; };
    auto indexOf = [](const std::vector<std::pair<std::string, std::string>>& v, const std::string& idv) { for (size_t i = 0; i < v.size(); ++i) if (v[i].first == idv) return int(i) + 1; return 0; };
    const std::vector<int> bufs = {128, 256, 512, 1024, 2048};
    std::vector<std::string> bufNames;
    int bufIdx = 2;
    for (size_t i = 0; i < bufs.size(); ++i) { bufNames.push_back(std::to_string(bufs[i]) + " samples (" + std::to_string(int(std::lround(bufs[i] * 1000.0 / 44100.0))) + " ms)"); if (std::to_string(bufs[i]) == get("buffer", "256")) bufIdx = int(i); }
    std::vector<std::string> midiNames = {"None"};
    midiNames.insert(midiNames.end(), midis.begin(), midis.end());
    int midiIdx = 0;
    for (size_t i = 0; i < midis.size(); ++i) if (midis[i] == get("midiInput", "")) midiIdx = int(i) + 1;
    std::vector<std::pair<std::string, std::string>> langList = {{"auto", "Automatic (system language)"}, {"en", "English"}};
    langList.insert(langList.end(), langs.begin(), langs.end());
    int langIdx = 0;
    for (size_t i = 0; i < langList.size(); ++i) if (langList[i].first == get("language", "auto")) langIdx = int(i);
    std::vector<std::string> langNames;
    for (const auto& l : langList) langNames.push_back(l.second);
    std::vector<std::string> iconStyleNames;
    for (IconStyle st : {IconStyle::Classic, IconStyle::Outlined, IconStyle::Rounded, IconStyle::Sharp})
        iconStyleNames.push_back(std::string(IconTheme::styleName(st)) + (IconTheme::instance().available(st) ? "" : " (missing)"));
    std::vector<DlgField> f = {DlgField::choice("Output device", names(outs), indexOf(outs, get("output", ""))), DlgField::choice("Buffer size", bufNames, bufIdx), DlgField::choice("Input device", names(ins), indexOf(ins, get("input", ""))),
                               DlgField::number("Recording latency compensation", std::atof(get("recOffset", "0").c_str()), -500, 500, 1, 0, " ms"), DlgField::choice("MIDI input", midiNames, midiIdx), DlgField::choice("Language", langNames, langIdx),
                               DlgField::number("Autosave every", std::atof(get("autosave", "2").c_str()), 0, 60, 1, 0, " min"), DlgField::input("Loops folder", get("loops", m_host.loopFolder)), DlgField::input("VST3 folders (separate with ;)", get("vst3Folders", "")),
                               DlgField::slider("Interface scale", std::atof(get("uiScale", "1").c_str()), 0.75, 2.0, 2, " x"),
                               DlgField::choice("Icon style", iconStyleNames, int(IconTheme::instance().style())),
                               DlgField::note("Restart MiniDAW to apply a new language or scale. Autosave 0 = off."),
                               DlgField::note("Material Symbols needs its font files in the fonts folder."),
                               DlgField::note("Without them the classic icons are used."),
                               DlgField::note(fmt(m_host.portable ? "Portable mode: settings are kept in %1." : "Settings are kept in %1.", m_host.dataDir + "\\vulkan-settings.ini"))};
    openDialog("Preferences", std::move(f), [this, outs, ins, midis, langList, bufs](const std::vector<DlgField>& v) {
        auto s = m_host.settings;
        s["output"] = v[0].index == 0 ? "" : outs[size_t(v[0].index - 1)].first;
        s["buffer"] = std::to_string(bufs[size_t(v[1].index)]);
        s["input"] = v[2].index == 0 ? "" : ins[size_t(v[2].index - 1)].first;
        s["recOffset"] = std::to_string(int(v[3].value));
        s["midiInput"] = v[4].index == 0 ? "" : midis[size_t(v[4].index - 1)];
        s["language"] = langList[size_t(v[5].index)].first;
        s["autosave"] = std::to_string(int(v[6].value));
        s["loops"] = v[7].text;
        s["vst3Folders"] = v[8].text;
        char sc[16]; std::snprintf(sc, sizeof sc, "%.2f", v[9].value);
        s["uiScale"] = sc;
        const IconStyle iconStyle = IconStyle(std::clamp(v[10].index, 0, 3));
        s["iconStyle"] = IconTheme::styleKey(iconStyle);
        IconTheme::instance().setStyle(iconStyle);   // takes effect at once
        m_host.settings = s;
        if (!v[7].text.empty() && v[7].text != m_host.loopFolder) { m_host.loopFolder = v[7].text; m_loopsLoaded = false; }
        m_s.setRecordingOffsetMs(int(v[3].value));
        if (m_host.applySettings) m_host.applySettings(s);
        message("Preferences saved.");
    }, "Save");
}

}  // namespace vkui
