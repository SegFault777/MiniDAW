// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <string>
#include "Ui.h"

namespace ui {

namespace {
MenuItem item(std::string label, std::string shortcut, std::function<void()> fn, bool enabled = true) { return {std::move(label), std::move(shortcut), enabled, false, std::move(fn)}; }
MenuItem sep() { MenuItem m; m.separator = true; return m; }
}  // namespace

void NativeUi::withRegion(const std::function<void(int, int)>& f) {
    if (!hasRegion()) { message("Select a region first."); return; }
    f(m_selTrack, m_selRegion);
    m_dirty = true;
}

void NativeUi::ensureVisible(double beat) {
    const double x = beat * m_ppb - m_scrollX, w = m_w - kHeaderW;
    if (x < 0) m_scrollX = std::max(0.0, beat * m_ppb - w * 0.1);
    else if (x > w - 40) m_scrollX = beat * m_ppb - w * 0.1;
}

void NativeUi::togglePlay() {
    if (m_s.isPlaying()) { if (m_s.isRecording()) m_s.stopRecording(); m_s.pause(); }
    else m_s.play();
    m_dirty = true;
}
void NativeUi::doStop() { if (m_s.isRecording()) m_s.stopRecording(); m_s.stop(); m_dirty = true; }

bool NativeUi::keyUp(int k) {
    auto it = m_heldKeys.find(k);
    if (it == m_heldKeys.end()) return false;
    m_s.noteOff(m_selTrack, it->second);
    m_heldKeys.erase(it);
    return true;
}

void NativeUi::addRecent(const std::string& path) {
    if (path.empty()) return;
    std::vector<std::string> list = {path};
    for (int i = 1; i <= 5; ++i) {
        auto it = m_host.settings.find("recent" + std::to_string(i));
        if (it != m_host.settings.end() && !it->second.empty() && it->second != path && list.size() < 5) list.push_back(it->second);
    }
    for (int i = 1; i <= 5; ++i) m_host.settings["recent" + std::to_string(i)] = i <= int(list.size()) ? list[size_t(i - 1)] : std::string();
    if (m_host.applySettings) m_host.applySettings(m_host.settings);
}

void NativeUi::openProject(const std::string& p) {
    std::string err;
    if (m_s.loadProject(p, &err)) { message("Opened " + p); addRecent(p); m_selTrack = m_selRegion = -1; }
    else message("Could not open the project: " + err);
    if (m_host.setTitle) m_host.setTitle("MiniDAW - " + p);
}

bool NativeUi::tick() {
    if (m_auditionPitch >= 0 && m_drag == Drag::None && --m_auditionTicks <= 0) { m_s.noteOff(m_auditionTrack, m_auditionPitch); m_auditionPitch = -1; }
    const bool active = m_s.isPlaying() || m_s.isRecording() || m_meterL > 0.004f || m_meterR > 0.004f;
    if (m_s.isPlaying() && m_drag == Drag::None) ensureVisible(m_s.positionBeats());
    const bool r = m_dirty || active;
    return r;
}

// ---------------------------------------------------------------- menus
void NativeUi::openMenu(const std::vector<MenuItem>& items, int x, int y) {
    m_menu = items;
    Canvas probe(1, 1);
    int w = 180;
    for (const auto& it : m_menu) w = std::max(w, probe.textWidth(it.label) + probe.textWidth(it.shortcut) + 60);
    int h = 12;
    for (const auto& it : m_menu) h += it.separator ? 9 : 22;
    m_menuRect = {std::min(x, m_w - w - 4), std::min(y, m_h - h - 4), w, h};
    m_menuHover = -1;
    m_dirty = true;
}

std::vector<MenuItem> NativeUi::fileMenu() {
    return {item("New Project", "Ctrl+N", [this] { doFile("new"); }), item("Open...", "Ctrl+O", [this] { doFile("open"); }), item("Open Recent...", "", [this] { openMenu(recentMenu(), m_menuRect.x + 20, m_menuRect.y + 60); }), sep(),
            item("Save", "Ctrl+S", [this] { doFile("save"); }), item("Save As...", "Ctrl+Shift+S", [this] { doFile("saveas"); }), sep(),
            item("Import Audio...", "Ctrl+I", [this] { doFile("import"); }), item("Import MIDI File...", "", [this] { doFile("importmidi"); }),
            item("Export MIDI File...", "", [this] { doFile("exportmidi"); }), item("Bounce to WAV...", "Ctrl+B", [this] { dlgBounce(); }), sep(),
            item("Preferences...", "Ctrl+,", [this] { dlgPreferences(); }), sep(), item("Quit", "", [this] { if (m_host.quit) m_host.quit(); })};
}

std::vector<MenuItem> NativeUi::recentMenu() {
    std::vector<MenuItem> m;
    for (int i = 1; i <= 5; ++i) {
        auto it = m_host.settings.find("recent" + std::to_string(i));
        if (it == m_host.settings.end() || it->second.empty()) continue;
        const std::string path = it->second, shown = path.substr(path.find_last_of("/\\") == std::string::npos ? 0 : path.find_last_of("/\\") + 1);
        m.push_back(item(shown, "", [this, path] { openProject(path); }));
    }
    if (m.empty()) m.push_back(item("No recent projects", "", [] {}, false));
    return m;
}

std::vector<MenuItem> NativeUi::editMenu() {
    const bool reg = hasRegion();
    return {item("Undo", "Ctrl+Z", [this] { m_s.undo(); m_dirty = true; }, m_s.canUndo()), item("Redo", "Ctrl+Shift+Z", [this] { m_s.redo(); m_dirty = true; }, m_s.canRedo()), sep(),
            item("Cut Region", "Ctrl+X", [this] { withRegion([this](int t, int r) { m_s.copyRegion(t, r); m_s.removeRegion(t, r); m_selRegion = -1; }); }, reg),
            item("Copy Region", "Ctrl+C", [this] { withRegion([this](int t, int r) { m_s.copyRegion(t, r); message("Region copied."); }); }, reg),
            item("Paste Region", "Ctrl+V", [this] { if (m_selTrack >= 0) { m_s.pasteRegion(m_selTrack, m_s.positionBeats()); m_dirty = true; } }, m_s.hasClipboard() && m_selTrack >= 0),
            item("Duplicate Region", "Ctrl+D", [this] { withRegion([this](int t, int r) { m_s.duplicateRegion(t, r); }); }, reg),
            item("Delete Region", "Del", [this] { withRegion([this](int t, int r) { m_s.removeRegion(t, r); m_selRegion = -1; }); }, reg),
            sep(), item("Split at Playhead", "Ctrl+T", [this] { withRegion([this](int t, int r) { m_s.splitRegion(t, r, m_s.positionBeats()); }); }, reg),
            item("Mute / Unmute Region", "Ctrl+M", [this] { withRegion([this](int t, int r) { m_s.setRegionMute(t, r, !m_tracks[size_t(t)].regions[size_t(r)].mute); }); }, reg),
            item("Rename...", "F2", [this] { dlgRename(); }, reg || m_selTrack >= 0), sep(),
            item(m_snapOn ? "Snap to Grid: On" : "Snap to Grid: Off", "", [this] { m_snapOn = !m_snapOn; m_dirty = true; })};
}

std::vector<MenuItem> NativeUi::functionsMenu() {
    const bool reg = hasRegion(), midi = regionIsMidi(), audio = reg && !midi;
    auto say = [this](const char* ok, bool done) { if (done) message(ok); m_dirty = true; };
    std::vector<MenuItem> m;
    m.push_back(item("Repeat Region...", "", [this] { dlgRepeat(); }, reg));
    m.push_back(item("Join Regions", "", [this] { withRegion([this](int t, int r) { m_s.joinRegions(t, r); }); }, reg));
    m.push_back(sep());
    m.push_back(item("Quantize...", "", [this] { dlgQuantize(); }, midi));
    m.push_back(item("Scale Quantize...", "", [this] { dlgScale(); }, midi));
    m.push_back(item("Humanize...", "", [this] { dlgHumanize(); }, midi));
    m.push_back(item("Arpeggiate...", "", [this] { dlgArpeggiate(); }, midi));
    m.push_back(item("Chord Trigger...", "", [this] { dlgChord(); }, midi));
    m.push_back(item("Transpose...", "", [this] { dlgTranspose(); }, midi));
    m.push_back(item("Crescendo / Decrescendo...", "", [this] { dlgVelocityRamp(); }, midi));
    m.push_back(item("Fixed Note Length...", "", [this] { dlgFixedLength(); }, midi));
    m.push_back(item("Scale Velocity...", "", [this] { dlgScaleVelocity(); }, midi));
    m.push_back(item("Legato", "", [=, this] { withRegion([=, this](int t, int r) { say("Legato applied.", m_s.legatoRegion(t, r)); }); }, midi));
    m.push_back(item("Fix Overlaps", "", [=, this] { withRegion([=, this](int t, int r) { say("Overlaps fixed.", m_s.fixOverlapsRegion(t, r)); }); }, midi));
    m.push_back(item("Remove Duplicate Notes", "", [=, this] { withRegion([=, this](int t, int r) { say("Duplicate notes removed.", m_s.removeDuplicatesRegion(t, r)); }); }, midi));
    m.push_back(item("Retrograde", "", [=, this] { withRegion([=, this](int t, int r) { say("Region reversed.", m_s.retrogradeRegion(t, r)); }); }, midi));
    m.push_back(item("Invert Pitches", "", [=, this] { withRegion([=, this](int t, int r) { say("Pitches inverted.", m_s.invertRegion(t, r)); }); }, midi));
    m.push_back(sep());
    m.push_back(item("Strip Silence...", "", [this] { dlgStripSilence(); }, audio));
    m.push_back(item("Detect Tempo...", "", [this] { dlgDetectTempo(); }, audio));
    m.push_back(item("Reverse Region", "", [=, this] { withRegion([=, this](int t, int r) { m_s.reverseRegion(t, r); }); }, audio));
    m.push_back(item("Normalize Region", "", [=, this] { withRegion([=, this](int t, int r) { m_s.normalizeRegion(t, r); }); }, audio));
    m.push_back(item("Measure Loudness", "", [=, this] { withRegion([=, this](int t, int r) { const double l = m_s.regionLoudness(t, r); char b[64]; if (l <= -119) std::snprintf(b, sizeof b, "Too quiet or too short to measure."); else std::snprintf(b, sizeof b, "Integrated loudness: %.1f LUFS", l); message(b); }); }, audio));
    return m;
}

std::vector<MenuItem> NativeUi::viewMenu() {
    return {item(m_mixer ? "Hide Bottom Panel" : "Show Bottom Panel", "X", [this] { m_mixer = !m_mixer; m_dirty = true; }),
            item("Mixer", "", [this] { m_mixer = true; m_tab = Tab::Mixer; m_dirty = true; }), item("Piano Roll", "P", [this] { m_mixer = true; m_tab = Tab::PianoRoll; m_dirty = true; }),
            item("Step Sequencer", "", [this] { m_mixer = true; m_tab = Tab::Steps; m_dirty = true; }), item("Drummer Editor", "", [this] { m_mixer = true; m_tab = Tab::Drummer; m_dirty = true; }),
            item("Loop Browser", "L", [this] { m_mixer = true; m_tab = Tab::Loops; m_dirty = true; }),
            item(m_inspector ? "Hide Inspector" : "Show Inspector", "I", [this] { m_inspector = !m_inspector; m_dirty = true; }), sep(),
            item("Zoom In", "+", [this] { m_ppb = std::min(400.0, m_ppb * 1.25); m_dirty = true; }), item("Zoom Out", "-", [this] { m_ppb = std::max(6.0, m_ppb / 1.25); m_dirty = true; }), sep(),
            item("Tempo...", "", [this] { dlgTempo(); }), item("Time Signature...", "", [this] { dlgTimeSignature(); }), item("Tap Tempo", "T", [this] { tapTempo(); }),
            item("Add Marker", "M", [this] { m_s.addMarker(m_s.positionBeats()); m_dirty = true; }), item("Next Marker", "]", [this] { gotoMarker(1); }), item("Previous Marker", "[", [this] { gotoMarker(-1); }), sep(),
            item("Metronome", "K", [this] { m_s.setMetronome(!m_s.metronome()); m_dirty = true; }), item("Count-in", "", [this] { m_s.setCountIn(!m_s.countIn()); m_dirty = true; }),
            item(m_typing ? "Musical Typing: On" : "Musical Typing: Off", "", [this] { m_typing = !m_typing; message(m_typing ? "Musical typing on: A W S E D F T G Y H U J K play notes, Z / X change the octave." : "Musical typing off."); m_dirty = true; }), sep(),
            item("About MiniDAW", "", [this] { dlgAbout(); })};
}

std::vector<MenuItem> NativeUi::addTrackMenu() {
    auto add = [this](core::TrackType t, const char* name) { m_selTrack = m_s.addTrack(t, name); m_selRegion = -1; m_dirty = true; };
    return {item("New Audio Track", "", [=, this] { add(core::TrackType::Audio, "Audio"); }), item("New Software Instrument Track", "", [=, this] { add(core::TrackType::Instrument, "Instrument"); }),
            item("New Drum Machine Track", "", [=, this] { add(core::TrackType::Drums, "Drum Machine"); }), item("New Drummer Track", "", [=, this] { add(core::TrackType::Drummer, "Drummer"); })};
}

std::vector<MenuItem> NativeUi::trackMenu(int t) {
    const bool valid = t >= 0 && t < int(m_tracks.size());
    std::vector<MenuItem> m = addTrackMenu();
    m.push_back(sep());
    m.push_back(item("Duplicate Track", "", [=, this] { m_s.duplicateTrack(t); m_dirty = true; }, valid));
    m.push_back(item("Delete Track", "", [=, this] { m_s.removeTrack(t); m_selTrack = m_selRegion = -1; m_dirty = true; }, valid));
    m.push_back(item("Move Track Up", "", [=, this] { m_s.moveTrack(t, t - 1); m_selTrack = t - 1; m_dirty = true; }, valid && t > 0));
    m.push_back(item("Move Track Down", "", [=, this] { m_s.moveTrack(t, t + 1); m_selTrack = t + 1; m_dirty = true; }, valid && t + 1 < int(m_tracks.size())));
    m.push_back(item("Rename Track...", "", [this] { m_selRegion = -1; dlgRename(); }, valid));
    m.push_back(sep());
    m.push_back(item("Bounce in Place", "", [=, this] { m_s.bounceTrackInPlace(t); m_dirty = true; }, valid && m_tracks[size_t(t)].type != core::TrackType::Audio));
    m.push_back(item("Convert Pattern to MIDI Region...", "", [this] { dlgPatternToMidi(); }, valid && m_tracks[size_t(t)].type == core::TrackType::Drums));
    return m;
}

void NativeUi::tapTempo() {
    const auto now = std::chrono::steady_clock::now();
    const double t = std::chrono::duration<double>(now.time_since_epoch()).count();
    if (!m_taps.empty() && t - m_taps.back() > 2.0) m_taps.clear();
    m_taps.push_back(t);
    if (m_taps.size() > 8) m_taps.erase(m_taps.begin());
    if (m_taps.size() >= 2) {
        const double bpm = 60.0 * double(m_taps.size() - 1) / (m_taps.back() - m_taps.front());
        m_s.setBpm(std::clamp(std::round(bpm * 10.0) / 10.0, 20.0, 300.0));
    }
    m_dirty = true;
}

void NativeUi::gotoMarker(int dir) {
    const auto marks = m_s.markers();
    const double pos = m_s.positionBeats();
    double target = -1;
    if (dir > 0) { for (const auto& mk : marks) if (mk.beat > pos + 1e-6) { target = mk.beat; break; } }
    else for (auto it = marks.rbegin(); it != marks.rend(); ++it) if (it->beat < pos - 1e-6) { target = it->beat; break; }
    if (target >= 0) { m_s.seekBeats(target); ensureVisible(target); }
    m_dirty = true;
}

void NativeUi::doFile(const std::string& what) {
    auto ask = [&](const char* title, const char* filter, bool save) -> std::string {
        if (!m_host.fileDialog) { message("File dialogs are not available here."); return {}; }
        return m_host.fileDialog(title, filter, save);
    };
    std::string err;
    const char* kProject = "MiniDAW project|*.mdaw";
    if (what == "new") { m_s.newProject(); m_selTrack = m_selRegion = -1; message("New project."); }
    else if (what == "open") {
        const std::string p = ask("Open Project", kProject, false);
        if (!p.empty()) openProject(p);
    } else if (what == "save" || what == "saveas") {
        std::string p = what == "save" ? m_s.projectPath() : std::string();
        if (p.empty()) p = ask("Save Project", kProject, true);
        if (!p.empty()) { if (p.find('.') == std::string::npos) p += ".mdaw"; if (m_s.saveProject(p, &err)) { message("Saved " + p); addRecent(p); } else message("Could not save: " + err); }
    } else if (what == "import") {
        const std::string p = ask("Import Audio", "Audio files|*.wav;*.mp3;*.m4a;*.aac;*.flac;*.wma", false);
        if (!p.empty()) { m_s.importAudio(p, m_selTrack, m_s.positionBeats()); message("Importing " + p); }
    } else if (what == "importmidi") {
        const std::string p = ask("Import MIDI File", "MIDI files|*.mid;*.midi", false);
        if (!p.empty()) { if (m_s.importMidiFile(p, &err)) message("Imported " + p); else message("Could not import: " + err); }
    } else if (what == "exportmidi") {
        std::string p = ask("Export MIDI File", "MIDI files|*.mid", true);
        if (!p.empty()) { if (p.find('.') == std::string::npos) p += ".mid"; if (m_s.exportMidiFile(p, &err)) message("Exported " + p); else message("Could not export: " + err); }
    }
    if (m_host.setTitle) m_host.setTitle(m_s.projectPath().empty() ? "MiniDAW" : "MiniDAW - " + m_s.projectPath());
    m_dirty = true;
}

// ---------------------------------------------------------------- mouse
void NativeUi::applyFader(int track, int y, const Rect& r) {
    const Rect travel{r.x, r.y + 8, r.w, r.h - 16};
    double pos = std::clamp(double(travel.bottom() - y) / std::max(1, travel.h), 0.0, 1.0);
    float g = faderToGain(pos);
    if (std::fabs(20.0 * std::log10(std::max(1e-6f, g))) < 0.8) g = 1.f;   // magnet at 0 dB
    if (track < 0) m_s.setMasterVolume(g); else m_s.setVolume(track, g);
    m_dirty = true;
}

void NativeUi::mouseDown(int x, int y, int button, int mods) {
    m_hoverX = x; m_hoverY = y;
    m_tracks = m_s.snapshot();   // the model may have changed since the last paint (undo, background decode)
    if (m_dialog && !m_dialog->finished()) { m_dialog->mouseDown(x, y, button); m_dirty = true; return; }
    if (!m_menu.empty()) {
        if (m_menuRect.contains(x, y)) {
            int ry = m_menuRect.y + 6;
            for (const MenuItem& it : m_menu) {
                const int h = it.separator ? 9 : 22;
                if (!it.separator && y >= ry && y < ry + h) {
                    if (it.enabled && it.action) { auto act = it.action; closeMenu(); act(); }
                    return;
                }
                ry += h;
            }
            return;
        }
        closeMenu();
        return;
    }
    const HitRect* hit = nullptr;
    for (auto it = m_hits.rbegin(); it != m_hits.rend(); ++it) if (it->r.contains(x, y)) { hit = &*it; break; }
    if (!hit) return;
    const int a = hit->a, b = hit->b;
    if (hit->kind == Hit::PianoGrid || hit->kind == Hit::PianoKeys || hit->kind == Hit::PianoVel || hit->kind == Hit::AutoLane) { paneMouseDown(x, y, button, mods); m_dirty = true; return; }
    const Rect hr = hit->r;
    const bool right = button == 1;
    switch (hit->kind) {
    case Hit::Tab: if (a == 100 || a == 101) m_tabExtra = a; else { m_tabExtra = 0; m_tab = Tab(a); } break;
    case Hit::Slider:
        m_drag = Drag::Slider; m_dragRect = hr; m_dragFn = m_sliderFns[size_t(a)];
        m_dragFn(std::clamp(double(x - (hr.x + 4)) / std::max(1, hr.w - 8), 0.0, 1.0));
        break;
    case Hit::Action: { auto fn = m_actionFns[size_t(a)]; if (fn) fn(); break; }
    case Hit::StepCell: {
        const double cw = double(m_stepGrid.w) / kSteps, ch = double(m_stepGrid.h) / kDrumRows;
        const int st = std::clamp(int((x - m_stepGrid.x) / cw), 0, kSteps - 1), row = std::clamp(int((y - m_stepGrid.y) / ch), 0, kDrumRows - 1);
        m_s.checkpoint();
        m_stepPaint = !m_tracks[size_t(m_selTrack)].steps[size_t(row)][size_t(st)];
        m_s.setStep(m_selTrack, row, st, m_stepPaint);
        m_drag = Drag::Steps;
        break;
    }
    case Hit::LoopItem:
        m_loopSel = a;
        m_s.audition(m_loops[size_t(a)].path);
        break;
    case Hit::Marker:
        if (right) {
            const int mi = a;
            openMenu({item("Rename Marker...", "", [this, mi] {
                          const auto ms = m_s.markers();
                          if (mi < int(ms.size())) openDialog(std::make_unique<Dialog>("Rename Marker", Dialog::Values{Field::input("Name", ms[size_t(mi)].name)}, [this, mi](const Dialog::Values& v) { m_s.renameMarker(mi, v[0].text); }));
                      }),
                      item("Delete Marker", "", [this, mi] { m_s.removeMarker(mi); m_dirty = true; })}, x, y);
        } else {
            const auto ms = m_s.markers();
            if (a < int(ms.size())) { m_s.seekBeats(ms[size_t(a)].beat); m_drag = Drag::MarkerMove; m_dragRegion = a; m_dragMoved = false; }
        }
        break;
    case Hit::Rewind: m_s.seekBeats(0); ensureVisible(0); break;
    case Hit::Stop: doStop(); break;
    case Hit::Play: togglePlay(); break;
    case Hit::Record:
        if (m_s.isRecording()) m_s.stopRecording();
        else if (!m_s.startRecording()) message("Arm a track with the R button first.");
        break;
    case Hit::Cycle:
        if (m_s.loopB() <= m_s.loopA()) m_s.setLoop(!m_s.loopOn(), 0, 16); else m_s.setLoop(!m_s.loopOn());
        break;
    case Hit::Metronome: m_s.setMetronome(!m_s.metronome()); break;
    case Hit::CountIn: m_s.setCountIn(!m_s.countIn()); break;
    case Hit::MenuFile: openMenu(fileMenu(), hr.x, hr.bottom()); break;
    case Hit::MenuTrack: openMenu(trackMenu(m_selTrack), hr.x, hr.bottom()); break;
    case Hit::MenuEdit: openMenu(editMenu(), hr.x, hr.bottom()); break;
    case Hit::MenuFunctions: openMenu(functionsMenu(), hr.x, hr.bottom()); break;
    case Hit::MenuView: openMenu(viewMenu(), hr.x, hr.bottom()); break;
    case Hit::AddTrack: openMenu(addTrackMenu(), hr.x, hr.bottom()); break;
    case Hit::Snap:
        if (!m_snapOn) { m_snapOn = true; m_snapIdx = 0; }
        else if (m_snapIdx == 4) m_snapOn = false;
        else ++m_snapIdx;
        break;
    case Hit::ZoomIn: m_ppb = std::min(400.0, m_ppb * 1.25); break;
    case Hit::ZoomOut: m_ppb = std::max(6.0, m_ppb / 1.25); break;
    case Hit::MixerToggle: m_mixer = !m_mixer; break;
    case Hit::Ruler:
        if (mods & ModShift) { m_drag = Drag::LoopRange; m_dragStart = snapBeat(xToBeat(x)); m_s.setLoop(true, m_dragStart, m_dragStart + 1); }
        else { m_drag = Drag::Seek; m_s.seekBeats(std::max(0.0, xToBeat(x))); }
        break;
    case Hit::Lane: m_selTrack = a; m_selRegion = -1; if (right) openMenu(addTrackMenu(), x, y); break;
    case Hit::Region:
        selectRegion(a, b);
        if (right) { openMenu(functionsMenu(), x, y); break; }
        m_drag = Drag::Region; m_dragTrack = a; m_dragRegion = b; m_dragX = x; m_dragMoved = false;
        m_dragStart = m_tracks[size_t(a)].regions[size_t(b)].start;
        break;
    case Hit::RegionEdge:
        selectRegion(a, b);
        m_drag = Drag::Resize; m_dragTrack = a; m_dragRegion = b; m_dragX = x; m_dragMoved = false;
        m_dragLen = m_s.regionBeats(m_tracks[size_t(a)].regions[size_t(b)]);
        break;
    case Hit::TrackHeader: m_selTrack = a; m_selRegion = -1; if (right) openMenu(trackMenu(a), x, y); break;
    case Hit::MixerStrip: m_selTrack = a; m_selRegion = -1; break;
    case Hit::TrackMute: m_s.setMute(a, !m_tracks[size_t(a)].mute); break;
    case Hit::TrackSolo: m_s.setSolo(a, !m_tracks[size_t(a)].solo); break;
    case Hit::TrackArm: m_s.setArmed(a, !m_tracks[size_t(a)].armed); break;
    case Hit::TrackVol:
        m_drag = Drag::Volume; m_dragTrack = a; m_dragRect = {hr.x + 6, hr.y, hr.w - 12, hr.h};
        m_s.setVolume(a, float(std::clamp(double(x - m_dragRect.x) / m_dragRect.w, 0.0, 1.0)) * 1.5f);
        break;
    case Hit::Fader: m_drag = Drag::Fader; m_dragTrack = a; m_dragRect = hr; applyFader(a, y, hr); break;
    case Hit::MasterFader: m_drag = Drag::Master; m_dragTrack = -1; m_dragRect = hr; applyFader(-1, y, hr); break;
    case Hit::Pan: m_drag = Drag::Pan; m_dragTrack = a; m_dragY = y; m_dragValue = m_tracks[size_t(a)].pan; break;
    default: break;
    }
    m_dirty = true;
}

void NativeUi::mouseMove(int x, int y, int mods) {
    (void)mods;
    m_hoverX = x; m_hoverY = y;
    m_dirty = true;
    if (m_dialog && !m_dialog->finished()) { m_dialog->mouseMove(x, y); return; }
    if (!m_menu.empty()) {
        m_menuHover = -1;
        if (m_menuRect.contains(x, y)) {
            int ry = m_menuRect.y + 6;
            for (int i = 0; i < int(m_menu.size()); ++i) {
                const int h = m_menu[size_t(i)].separator ? 9 : 22;
                if (y >= ry && y < ry + h && !m_menu[size_t(i)].separator) { m_menuHover = i; break; }
                ry += h;
            }
        }
        return;
    }
    switch (m_drag) {
    case Drag::Seek: m_s.seekBeats(std::max(0.0, xToBeat(x))); break;
    case Drag::Slider: if (m_dragFn) m_dragFn(std::clamp(double(x - (m_dragRect.x + 4)) / std::max(1, m_dragRect.w - 8), 0.0, 1.0)); break;
    case Drag::Note: case Drag::NoteResize: case Drag::Velocity: paneMouseMove(x, y); break;
    case Drag::AutoPoint: autoMouseMove(x, y); break;
    case Drag::Steps: {
        if (!m_stepGrid.contains(x, y) || m_selTrack < 0) break;
        const double cw = double(m_stepGrid.w) / kSteps, ch = double(m_stepGrid.h) / kDrumRows;
        const int st = std::clamp(int((x - m_stepGrid.x) / cw), 0, kSteps - 1), row = std::clamp(int((y - m_stepGrid.y) / ch), 0, kDrumRows - 1);
        m_s.setStep(m_selTrack, row, st, m_stepPaint);
        break;
    }
    case Drag::LoopRange: m_s.setLoop(true, std::min(m_dragStart, snapBeat(xToBeat(x))), std::max(m_dragStart + 0.25, snapBeat(xToBeat(x)))); break;
    case Drag::MarkerMove: {
        if (!m_dragMoved && std::abs(x - m_hoverX) < 0) break;
        const int ni = m_s.moveMarker(m_dragRegion, snapBeat(xToBeat(x)));
        m_dragRegion = ni;
        break;
    }
    case Drag::Region:
        if (!m_dragMoved && std::abs(x - m_dragX) < 3) break;
        if (!m_dragMoved) { m_s.checkpoint(); m_dragMoved = true; }
        m_s.moveRegion(m_dragTrack, m_dragRegion, snapBeat(m_dragStart + double(x - m_dragX) / m_ppb));
        break;
    case Drag::Resize:
        if (!m_dragMoved && std::abs(x - m_dragX) < 3) break;
        if (!m_dragMoved) { m_s.checkpoint(); m_dragMoved = true; }
        m_s.resizeRegion(m_dragTrack, m_dragRegion, std::max(0.25, snapBeat(m_dragLen + double(x - m_dragX) / m_ppb)));
        break;
    case Drag::Volume: m_s.setVolume(m_dragTrack, float(std::clamp(double(x - m_dragRect.x) / m_dragRect.w, 0.0, 1.0)) * 1.5f); break;
    case Drag::Fader: case Drag::Master: applyFader(m_dragTrack, y, m_dragRect); break;
    case Drag::Pan: m_s.setPan(m_dragTrack, std::clamp(m_dragValue + float(m_dragY - y) * 0.012f, -1.f, 1.f)); break;
    default: break;
    }
}

void NativeUi::mouseUp(int x, int y, int button, int mods) {
    (void)button; (void)mods;
    if (m_dialog && !m_dialog->finished()) { m_dialog->mouseUp(x, y); return; }
    paneMouseUp();
    m_drag = Drag::None;
    m_dirty = true;
}

void NativeUi::doubleClick(int x, int y) {
    if (m_dialog && !m_dialog->finished()) return;
    const HitRect* hit = nullptr;
    for (auto it = m_hits.rbegin(); it != m_hits.rend(); ++it) if (it->r.contains(x, y)) { hit = &*it; break; }
    if (!hit) return;
    switch (hit->kind) {
    case Hit::TrackVol: case Hit::Fader: if (hit->a >= 0 && hit->a < int(m_tracks.size())) m_s.setVolume(hit->a, 0.8f); break;
    case Hit::MasterFader: m_s.setMasterVolume(0.8f); break;
    case Hit::Pan: m_s.setPan(hit->a, 0.f); break;
    case Hit::Region: {
        const auto& rg = m_tracks[size_t(hit->a)].regions[size_t(hit->b)];
        m_mixer = true; m_tabExtra = 0;
        m_tab = rg.drummer ? Tab::Drummer : rg.midi ? Tab::PianoRoll : Tab::Mixer;
        if (!rg.midi) m_tabExtra = 100;
        m_prKey = -1;
        break;
    }
    case Hit::LoopItem: {
        const int track = m_selTrack >= 0 && m_selTrack < int(m_tracks.size()) && m_tracks[size_t(m_selTrack)].type == core::TrackType::Audio ? m_selTrack : -1;
        m_s.stopAudition();
        m_s.importAudio(m_loops[size_t(hit->a)].path, track, m_s.positionBeats());
        message("Added " + m_loops[size_t(hit->a)].name);
        break;
    }
    case Hit::TrackHeader: {
        if (m_tracks[size_t(hit->a)].type == core::TrackType::Drums) { m_mixer = true; m_tabExtra = 0; m_tab = Tab::Steps; }
        else m_mixer = true;
        break;
    }
    case Hit::Lane: {
        const int t = hit->a;
        const double beat = std::floor(std::max(0.0, xToBeat(x)) / 4.0) * 4.0;
        const auto type = m_tracks[size_t(t)].type;
        if (type == core::TrackType::Instrument) m_s.addMidiRegion(t, beat, 4);
        else if (type == core::TrackType::Drummer) m_s.addDrummerRegion(t, beat, 4);
        else message(type == core::TrackType::Audio ? "Use File > Import Audio to add audio." : "Drum machine patterns are not part of the native UI yet.");
        break;
    }
    default: break;
    }
    m_dirty = true;
}

void NativeUi::mouseWheel(int x, int y, double dx, double dy, int mods) {
    if (m_dialog && !m_dialog->finished()) return;
    if (paneWheel(x, y, dx, dy, mods)) { m_dirty = true; return; }
    const Rect area = arrangeArea();
    if (mods & ModCtrl) {
        const double before = xToBeat(x), f = dy > 0 ? 1.15 : 1 / 1.15;
        m_ppb = std::clamp(m_ppb * f, 6.0, 400.0);
        m_scrollX = std::max(0.0, before * m_ppb - (x - kHeaderW));
    } else if ((mods & ModShift) || std::fabs(dx) > 0) {
        m_scrollX = std::max(0.0, m_scrollX - (std::fabs(dx) > 0 ? dx : dy) * 60);
    } else if (area.contains(x, y)) {
        const double maxY = std::max(0.0, double(m_tracks.size()) * kTrackH - (area.h - kRulerH));
        m_scrollY = std::clamp(m_scrollY - dy * 40, 0.0, maxY);
    }
    m_dirty = true;
}

bool NativeUi::key(int k, int mods) {
    const bool ctrl = mods & ModCtrl, shift = mods & ModShift;
    m_dirty = true;
    if (m_dialog && !m_dialog->finished()) return m_dialog->key(k, mods);
    if (paneKey(k, mods)) return true;
    if (m_typing && !ctrl) {
        static const char kKeys[] = "AWSEDFTGYHUJKOLP";   // C C# D D# E F F# G G# A A# B C C# D D#
        if (k == 'Z' || k == 'X') { m_typingOct = std::clamp(m_typingOct + (k == 'X' ? 1 : -1), 0, 8); message("Octave " + std::to_string(m_typingOct - 1)); return true; }
        const char* p = k < 128 ? std::strchr(kKeys, k) : nullptr;
        if (p && *p && m_selTrack >= 0 && m_selTrack < int(m_tracks.size()) && m_tracks[size_t(m_selTrack)].instrument) {
            const int pitch = std::clamp((m_typingOct + 1) * 12 + int(p - kKeys), 0, 127);
            if (!m_heldKeys.count(k)) { m_heldKeys[k] = pitch; m_s.noteOn(m_selTrack, pitch, 100); }
            return true;
        }
    }
    if (k == KeyEscape) { if (menuOpen()) closeMenu(); else m_selRegion = -1; return true; }
    if (menuOpen()) return false;
    if (ctrl) {
        switch (k) {
        case 'Z': if (shift) m_s.redo(); else m_s.undo(); return true;
        case 'Y': m_s.redo(); return true;
        case 'C': withRegion([this](int t, int r) { m_s.copyRegion(t, r); message("Region copied."); }); return true;
        case 'V': if (m_selTrack >= 0) m_s.pasteRegion(m_selTrack, m_s.positionBeats()); return true;
        case 'D': withRegion([this](int t, int r) { m_s.duplicateRegion(t, r); }); return true;
        case 'T': withRegion([this](int t, int r) { m_s.splitRegion(t, r, m_s.positionBeats()); }); return true;
        case 'N': doFile("new"); return true;
        case 'O': doFile("open"); return true;
        case 'S': doFile(shift ? "saveas" : "save"); return true;
        case 'I': doFile("import"); return true;
        case 'B': dlgBounce(); return true;
        case 'X': withRegion([this](int t, int r) { m_s.copyRegion(t, r); m_s.removeRegion(t, r); m_selRegion = -1; }); return true;
        case 'M': withRegion([this](int t, int r) { m_s.setRegionMute(t, r, !m_tracks[size_t(t)].regions[size_t(r)].mute); }); return true;
        case ',': dlgPreferences(); return true;
        default: return false;
        }
    }
    switch (k) {
    case KeySpace: togglePlay(); return true;
    case KeyEnter: m_s.seekBeats(0); ensureVisible(0); return true;
    case KeyHome: m_s.seekBeats(0); ensureVisible(0); return true;
    case 'R': if (m_s.isRecording()) m_s.stopRecording(); else if (!m_s.startRecording()) message("Arm a track with the R button first."); return true;
    case 'C': if (m_s.loopB() <= m_s.loopA()) m_s.setLoop(!m_s.loopOn(), 0, 16); else m_s.setLoop(!m_s.loopOn()); return true;
    case 'K': m_s.setMetronome(!m_s.metronome()); return true;
    case 'X': m_mixer = !m_mixer; return true;
    case 'P': m_mixer = true; m_tab = Tab::PianoRoll; return true;
    case 'L': m_mixer = true; m_tab = Tab::Loops; return true;
    case 'I': m_inspector = !m_inspector; return true;
    case 'T': tapTempo(); return true;
    case 'M': m_s.addMarker(m_s.positionBeats()); return true;
    case ']': gotoMarker(1); return true;
    case '[': gotoMarker(-1); return true;
    case KeyF2: dlgRename(); return true;
    case KeyDelete: case KeyBackspace: withRegion([this](int t, int r) { m_s.removeRegion(t, r); m_selRegion = -1; }); return true;
    case KeyLeft: case KeyRight: {
        const auto tm = m_s.tempoMap();
        const double pos = m_s.positionBeats();
        const double step = shift ? 1.0 : tm->barLength(tm->barAt(pos));
        m_s.seekBeats(std::max(0.0, pos + (k == KeyRight ? step : -step)));
        ensureVisible(m_s.positionBeats());
        return true;
    }
    case KeyUp: if (m_selTrack > 0) { --m_selTrack; m_selRegion = -1; } return true;
    case KeyDown: if (m_selTrack + 1 < int(m_tracks.size())) { ++m_selTrack; m_selRegion = -1; } return true;
    case KeyPlus: case '=': m_ppb = std::min(400.0, m_ppb * 1.25); return true;
    case KeyMinus: m_ppb = std::max(6.0, m_ppb / 1.25); return true;
    default: break;
    }
    return false;
}

}  // namespace ui
