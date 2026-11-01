// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cmath>
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

bool NativeUi::tick() {
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
    return {item("New Project", "Ctrl+N", [this] { doFile("new"); }), item("Open...", "Ctrl+O", [this] { doFile("open"); }), sep(),
            item("Save", "Ctrl+S", [this] { doFile("save"); }), item("Save As...", "Ctrl+Shift+S", [this] { doFile("saveas"); }), sep(),
            item("Import Audio...", "Ctrl+I", [this] { doFile("import"); }), item("Import MIDI File...", "", [this] { doFile("importmidi"); }),
            item("Bounce to WAV...", "Ctrl+B", [this] { doFile("bounce"); }), sep(), item("Quit", "", [this] { if (m_host.quit) m_host.quit(); })};
}

std::vector<MenuItem> NativeUi::editMenu() {
    const bool reg = hasRegion();
    return {item("Undo", "Ctrl+Z", [this] { m_s.undo(); m_dirty = true; }, m_s.canUndo()), item("Redo", "Ctrl+Shift+Z", [this] { m_s.redo(); m_dirty = true; }, m_s.canRedo()), sep(),
            item("Copy Region", "Ctrl+C", [this] { withRegion([this](int t, int r) { m_s.copyRegion(t, r); message("Region copied."); }); }, reg),
            item("Paste Region", "Ctrl+V", [this] { if (m_selTrack >= 0) { m_s.pasteRegion(m_selTrack, m_s.positionBeats()); m_dirty = true; } }, m_s.hasClipboard() && m_selTrack >= 0),
            item("Duplicate Region", "Ctrl+D", [this] { withRegion([this](int t, int r) { m_s.duplicateRegion(t, r); }); }, reg),
            item("Delete Region", "Del", [this] { withRegion([this](int t, int r) { m_s.removeRegion(t, r); m_selRegion = -1; }); }, reg),
            sep(), item("Split at Playhead", "Ctrl+T", [this] { withRegion([this](int t, int r) { m_s.splitRegion(t, r, m_s.positionBeats()); }); }, reg)};
}

std::vector<MenuItem> NativeUi::functionsMenu() {
    const bool reg = hasRegion(), midi = regionIsMidi(), audio = reg && !midi;
    auto say = [this](const char* ok, bool done) { if (done) message(ok); m_dirty = true; };
    std::vector<MenuItem> m;
    m.push_back(item("Repeat Region x3", "", [this] { withRegion([this](int t, int r) { m_s.repeatRegion(t, r, 3); }); }, reg));
    m.push_back(item("Join with Next Region", "", [this] { withRegion([this](int t, int r) { m_s.joinRegions(t, r); }); }, reg));
    m.push_back(sep());
    m.push_back(item("Legato", "", [=, this] { withRegion([=, this](int t, int r) { say("Legato applied.", m_s.legatoRegion(t, r)); }); }, midi));
    m.push_back(item("Fix Overlaps", "", [=, this] { withRegion([=, this](int t, int r) { say("Overlaps fixed.", m_s.fixOverlapsRegion(t, r)); }); }, midi));
    m.push_back(item("Remove Duplicate Notes", "", [=, this] { withRegion([=, this](int t, int r) { say("Duplicate notes removed.", m_s.removeDuplicatesRegion(t, r)); }); }, midi));
    m.push_back(item("Fixed Note Length (1/4)", "", [=, this] { withRegion([=, this](int t, int r) { say("Note lengths set.", m_s.fixedLengthRegion(t, r, 1.0)); }); }, midi));
    m.push_back(item("Retrograde", "", [=, this] { withRegion([=, this](int t, int r) { say("Region reversed.", m_s.retrogradeRegion(t, r)); }); }, midi));
    m.push_back(item("Invert Pitches", "", [=, this] { withRegion([=, this](int t, int r) { say("Pitches inverted.", m_s.invertRegion(t, r)); }); }, midi));
    m.push_back(item("Transpose +1 Octave", "", [=, this] { withRegion([=, this](int t, int r) { say("Transposed up an octave.", m_s.transposeRegion(t, r, 12)); }); }, midi));
    m.push_back(item("Transpose -1 Octave", "", [=, this] { withRegion([=, this](int t, int r) { say("Transposed down an octave.", m_s.transposeRegion(t, r, -12)); }); }, midi));
    m.push_back(item("Crescendo (30 to 110)", "", [=, this] { withRegion([=, this](int t, int r) { say("Crescendo applied.", m_s.velocityRampRegion(t, r, 30, 110)); }); }, midi));
    m.push_back(item("Scale Velocity 80%", "", [=, this] { withRegion([=, this](int t, int r) { say("Velocities scaled.", m_s.scaleVelocityRegion(t, r, 80, 0)); }); }, midi));
    m.push_back(sep());
    m.push_back(item("Strip Silence (-40 dB)", "", [=, this] { withRegion([=, this](int t, int r) { const int n = m_s.stripSilenceRegion(t, r, -40, 100, 10); if (n > 0) message(std::to_string(n) + " region(s) after Strip Silence."); }); }, audio));
    m.push_back(item("Detect Tempo", "", [=, this] { withRegion([=, this](int t, int r) { const double b = m_s.detectRegionTempo(t, r); message(b > 0 ? "Detected tempo: " + std::to_string(int(std::lround(b * 10)) / 10) + "." + std::to_string(int(std::lround(b * 10)) % 10) + " BPM" : "No clear tempo found."); }); }, audio));
    m.push_back(item("Measure Loudness", "", [=, this] { withRegion([=, this](int t, int r) { const double l = m_s.regionLoudness(t, r); char b[64]; if (l <= -119) std::snprintf(b, sizeof b, "Too quiet or too short to measure."); else std::snprintf(b, sizeof b, "Integrated loudness: %.1f LUFS", l); message(b); }); }, audio));
    return m;
}

std::vector<MenuItem> NativeUi::viewMenu() {
    return {item(m_mixer ? "Hide Mixer" : "Show Mixer", "X", [this] { m_mixer = !m_mixer; m_dirty = true; }), item("Zoom In", "+", [this] { m_ppb = std::min(400.0, m_ppb * 1.25); m_dirty = true; }),
            item("Zoom Out", "-", [this] { m_ppb = std::max(6.0, m_ppb / 1.25); m_dirty = true; }), sep(),
            item("Metronome", "K", [this] { m_s.setMetronome(!m_s.metronome()); m_dirty = true; }), item("Count-in", "", [this] { m_s.setCountIn(!m_s.countIn()); m_dirty = true; })};
}

std::vector<MenuItem> NativeUi::addTrackMenu() {
    auto add = [this](core::TrackType t, const char* name) { m_selTrack = m_s.addTrack(t, name); m_selRegion = -1; m_dirty = true; };
    return {item("Audio Track", "", [=, this] { add(core::TrackType::Audio, "Audio"); }), item("Software Instrument", "", [=, this] { add(core::TrackType::Instrument, "Instrument"); }),
            item("Drummer", "", [=, this] { add(core::TrackType::Drummer, "Drummer"); }), item("Drum Machine", "", [=, this] { add(core::TrackType::Drums, "Drum Machine"); })};
}

std::vector<MenuItem> NativeUi::trackMenu(int t) {
    return {item("Duplicate Track", "", [=, this] { m_s.duplicateTrack(t); m_dirty = true; }),
            item("Delete Track", "", [=, this] { m_s.removeTrack(t); m_selTrack = m_selRegion = -1; m_dirty = true; })};
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
        if (!p.empty()) { if (m_s.loadProject(p, &err)) message("Opened " + p); else message("Could not open the project: " + err); }
    } else if (what == "save" || what == "saveas") {
        std::string p = what == "save" ? m_s.projectPath() : std::string();
        if (p.empty()) p = ask("Save Project", kProject, true);
        if (!p.empty()) { if (p.find('.') == std::string::npos) p += ".mdaw"; if (m_s.saveProject(p, &err)) message("Saved " + p); else message("Could not save: " + err); }
    } else if (what == "import") {
        const std::string p = ask("Import Audio", "Audio files|*.wav;*.mp3;*.m4a;*.aac;*.flac;*.wma", false);
        if (!p.empty()) { m_s.importAudio(p, m_selTrack, m_s.positionBeats()); message("Importing " + p); }
    } else if (what == "importmidi") {
        const std::string p = ask("Import MIDI File", "MIDI files|*.mid;*.midi", false);
        if (!p.empty()) { if (m_s.importMidiFile(p, &err)) message("Imported " + p); else message("Could not import: " + err); }
    } else if (what == "bounce") {
        const std::string p = ask("Bounce to WAV", "WAV|*.wav", true);
        if (!p.empty()) { if (m_s.exportWav(p, &err)) message("Bounced to " + p); else message("Could not bounce: " + err); }
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
    (void)mods;
    m_hoverX = x; m_hoverY = y;
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
    const Rect hr = hit->r;
    const bool right = button == 1;
    switch (hit->kind) {
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
    case Hit::Ruler: m_drag = Drag::Seek; m_s.seekBeats(std::max(0.0, xToBeat(x))); break;
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
    (void)x; (void)y; (void)button; (void)mods;
    m_drag = Drag::None;
    m_dirty = true;
}

void NativeUi::doubleClick(int x, int y) {
    const HitRect* hit = nullptr;
    for (auto it = m_hits.rbegin(); it != m_hits.rend(); ++it) if (it->r.contains(x, y)) { hit = &*it; break; }
    if (!hit) return;
    switch (hit->kind) {
    case Hit::TrackVol: case Hit::Fader: if (hit->a >= 0 && hit->a < int(m_tracks.size())) m_s.setVolume(hit->a, 0.8f); break;
    case Hit::MasterFader: m_s.setMasterVolume(0.8f); break;
    case Hit::Pan: m_s.setPan(hit->a, 0.f); break;
    case Hit::Region: message("The piano roll and the region editors are not part of the native UI yet."); break;
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
        case 'B': doFile("bounce"); return true;
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
