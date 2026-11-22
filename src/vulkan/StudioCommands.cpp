// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Vulkan UI: the command registry. One table feeds the menus, the keyboard shortcuts, the command palette and the F1 sheet.
#include <algorithm>
#include "Studio.h"

namespace vkui {

void Studio::registerCommands() {
    const int C = ModCtrl, S = ModShift, A = ModAlt;
    auto sep = [this](const char* cat) { addCmd(cat, "-", "", 0, 0, nullptr); };
    auto midi = [this] { return hasMidiRegion(); };
    auto audio = [this] { return hasAudioRegion(); };
    auto reg = [this] { return hasRegion(); };
    auto trk = [this] { return m_selTrack >= 0 && m_selTrack < int(m_tracks.size()); };
    auto sel = [this](auto fn) { return [this, fn] { if (hasRegion()) fn(m_selTrack, m_selRegion); }; };

    // ------------------------------------------------------------------------------------------------ File
    addCmd("File", "New Project", "Ctrl+N", 'N', C, [this] { doFile("new"); });
    addCmd("File", "Open...", "Ctrl+O", 'O', C, [this] { doFile("open"); });
    addCmd("File", "Open Recent...", "", 0, 0, [this] {
        std::vector<MenuItem> items;
        for (int i = 1; i <= 5; ++i) {
            auto it = m_host.settings.find("recent" + std::to_string(i));
            if (it == m_host.settings.end() || it->second.empty()) continue;
            const std::string path = it->second, shown = path.substr(path.find_last_of("/\\") == std::string::npos ? 0 : path.find_last_of("/\\") + 1);
            MenuItem m; m.label = shown; m.action = [this, path] { guardUnsaved([this, path] { openFile(path); }); };
            items.push_back(m);
        }
        if (items.empty()) { MenuItem m; m.label = "No recent projects"; m.enabled = false; items.push_back(m); }
        else {
            items.push_back(MenuItem::sep());
            MenuItem c; c.label = "Clear Menu"; c.action = [this] { for (int i = 1; i <= 5; ++i) m_host.settings["recent" + std::to_string(i)] = ""; if (m_host.applySettings) m_host.applySettings(m_host.settings); };
            items.push_back(c);
        }
        m_g->openMenu(m_g->mouseX(), 40, std::move(items));
    });
    addCmd("File", "Recover Autosaved Project", "", 0, 0, [this] { doFile("recover"); }, [this] { return m_s.hasAutosave(); });
    sep("File");
    addCmd("File", "Save", "Ctrl+S", 'S', C, [this] { doFile("save"); });
    addCmd("File", "Save As...", "Ctrl+Shift+S", 'S', C | S, [this] { doFile("saveas"); });
    sep("File");
    addCmd("File", "Import Audio...", "Ctrl+I", 'I', C, [this] { doFile("import"); });
    addCmd("File", "Import Logic Pro Project Folder...", "", 0, 0, [this] {
        if (!m_host.folderDialog) { message("Folder dialogs are not available here."); return; }
        const std::string d = m_host.folderDialog("Choose a Logic Pro project (.logicx folder)");
        if (!d.empty()) importLogic(d);
    });
    addCmd("File", "Import MIDI File...", "", 0, 0, [this] { doFile("importmidi"); });
    addCmd("File", "Export MIDI File...", "", 0, 0, [this] { doFile("exportmidi"); });
    addCmd("File", "Bounce / Export Audio...", "Ctrl+B", 'B', C, [this] { dlgBounce(); });
    sep("File");
    addCmd("File", "Preferences...", "Ctrl+,", ',', C, [this] { dlgPreferences(); });
    addCmd("File", "Play Test Tone", "", 0, 0, [this] { m_s.playTestTone(); });
    addCmd("File", "Choose Loops Folder...", "", 0, 0, [this] {
        if (!m_host.folderDialog) { message("Folder dialogs are not available here."); return; }
        const std::string d = m_host.folderDialog("Choose Loops Folder");
        if (d.empty()) return;
        m_host.settings["loops"] = d; m_host.loopFolder = d; m_loopsLoaded = false;
        if (m_host.applySettings) m_host.applySettings(m_host.settings);
        message("Loops folder: " + d);
    });
    addCmd("File", "Add VST3 Folder...", "", 0, 0, [this] {
        if (!m_host.folderDialog) { message("Folder dialogs are not available here."); return; }
        const std::string d = m_host.folderDialog("Add VST3 Folder");
        if (d.empty()) return;
        std::string& list = m_host.settings["vst3Folders"];
        list += (list.empty() ? "" : ";") + d;
        if (m_host.applySettings) m_host.applySettings(m_host.settings);
        if (m_host.rescanPlugins) m_host.rescanPlugins();
        message("Added " + d + " - scanning plug-ins in the background...");
    });
    addCmd("File", "Rescan Plug-ins", "", 0, 0, [this] { if (m_host.rescanPlugins) { m_host.rescanPlugins(); message("Scanning plug-ins in the background..."); } });
    sep("File");
    addCmd("File", "Quit", "Ctrl+Q", 'Q', C, [this] { guardUnsaved([this] { m_quit = true; if (m_host.quit) m_host.quit(); }); });

    // ------------------------------------------------------------------------------------------------ Edit
    addCmd("Edit", "Undo", "Ctrl+Z", 'Z', C, [this] { m_multi.clear(); m_s.undo(); });
    addCmd("Edit", "Redo", "Ctrl+Shift+Z", 'Z', C | S, [this] { m_multi.clear(); m_s.redo(); });
    addCmd("Edit", "~Redo (Ctrl+Y)", "Ctrl+Y", 'Y', C, [this] { m_s.redo(); });
    sep("Edit");
    addCmd("Edit", "Cut Region", "Ctrl+X", 'X', C, [this] { const auto s = selectionList(); if (m_s.copyRegions(s)) { m_s.removeRegions(s); setMulti({}, {m_selTrack, -1}); } }, reg);
    addCmd("Edit", "Copy Region", "Ctrl+C", 'C', C, [this] { if (m_s.copyRegions(selectionList())) message("Region copied"); }, reg);
    addCmd("Edit", "Paste Region", "Ctrl+V", 'V', C, [this] { if (m_selTrack >= 0) { const auto n = m_s.pasteRegions(m_selTrack, m_s.positionBeats()); if (!n.empty()) setMulti(n, n.back()); } }, [this, trk] { return m_s.hasClipboard() && trk(); });
    addCmd("Edit", "Duplicate Region", "Ctrl+D", 'D', C, [this] { const auto c = m_s.duplicateRegions(selectionList()); if (!c.empty()) setMulti(c, c.back()); }, reg);
    addCmd("Edit", "Select All Regions", "Ctrl+A", 'A', C, [this] {
        std::vector<std::pair<int, int>> all;
        for (int t = 0; t < int(m_tracks.size()); ++t) for (int r = 0; r < int(m_tracks[size_t(t)].regions.size()); ++r) all.emplace_back(t, r);
        if (!all.empty()) { setMulti(all, all.front()); message(fmt("%1 regions selected.", std::to_string(all.size()))); }
    }, [this] { return !m_tracks.empty(); });
    addCmd("Edit", "Delete Region", "Del", KeyDelete, 0, [this] { deleteSelectedRegion(); }, reg);
    addCmd("Edit", "~Delete Region (Backspace)", "Backspace", KeyBackspace, 0, [this] { deleteSelectedRegion(); }, reg);
    sep("Edit");
    addCmd("Edit", "Ripple Delete Region", "Shift+Del", KeyDelete, S, [this] { rippleDelete(); }, reg);                                                                // Pro Tools Shuffle
    addCmd("Edit", "Split at Playhead", "S", 'S', 0, [this] { splitAtPlayhead(); });
    addCmd("Edit", "Join Regions", "Ctrl+J", 'J', C, sel([this](int t, int r) { m_s.joinRegions(t, r); }), reg);
    addCmd("Edit", "Mute Region", "Ctrl+Shift+M", 'M', C | S, [this] { const bool now = !region()->mute; m_s.muteRegions(selectionList(), now); }, reg);
    addCmd("Edit", "Rename Region...", "F2", KeyF2, 0, [this] { dlgRenameRegion(); }, reg);
    addCmd("Edit", "Nudge Region Left", "Alt+Left", KeyLeft, A, sel([this](int t, int r) { m_s.moveRegion(t, r, std::max(0.0, region()->start - (m_snapOn ? gridBeats() : 0.25))); }), reg);
    addCmd("Edit", "Nudge Region Right", "Alt+Right", KeyRight, A, sel([this](int t, int r) { m_s.moveRegion(t, r, region()->start + (m_snapOn ? gridBeats() : 0.25)); }), reg);

    // ------------------------------------------------------------------------------------------------ Track
    addCmd("Track", "New Audio Track", "Ctrl+Alt+A", 'A', C | A, [this] { addTrack(core::TrackType::Audio); });
    addCmd("Track", "New Software Instrument Track", "Ctrl+Alt+S", 'S', C | A, [this] { addTrack(core::TrackType::Instrument); });
    addCmd("Track", "New Drum Machine Track", "Ctrl+Alt+M", 'M', C | A, [this] { addTrack(core::TrackType::Drums); });
    addCmd("Track", "New Drummer Track", "Ctrl+Alt+D", 'D', C | A, [this] { addTrack(core::TrackType::Drummer); });
    sep("Track");
    addCmd("Track", "Duplicate Track", "Ctrl+Shift+D", 'D', C | S, [this] { m_s.duplicateTrack(m_selTrack); m_selTrack = std::min(m_selTrack + 1, int(m_tracks.size())); }, trk);
    addCmd("Track", "Delete Track", "Ctrl+Backspace", KeyBackspace, C, [this] { m_s.removeTrack(m_selTrack); m_selRegion = -1; }, trk);
    addCmd("Track", "Rename Track...", "Shift+F2", KeyF2, S, [this] { dlgRenameTrack(m_selTrack); }, trk);
    addCmd("Track", "Move Track Up", "Ctrl+Alt+Up", KeyUp, C | A, [this] { if (m_selTrack > 0) { m_s.moveTrack(m_selTrack, m_selTrack - 1); --m_selTrack; } }, trk);
    addCmd("Track", "Move Track Down", "Ctrl+Alt+Down", KeyDown, C | A, [this] { if (m_selTrack + 1 < int(m_tracks.size())) { m_s.moveTrack(m_selTrack, m_selTrack + 1); ++m_selTrack; } }, trk);
    sep("Track");
    addCmd("Track", "Arm Record", "Shift+R", 'R', S, [this] { m_s.setArmed(m_selTrack, !m_tracks[size_t(m_selTrack)].armed); }, trk);
    {   // palette-only aliases: Ctrl+K, "group b"
        static const char* groupLabels[] = {"~Mixer Group: None", "~Mixer Group: A", "~Mixer Group: B", "~Mixer Group: C", "~Mixer Group: D",
                                            "~Mixer Group: E", "~Mixer Group: F", "~Mixer Group: G", "~Mixer Group: H"};
        for (int gnum = 0; gnum <= 8; ++gnum)
            addCmd("Track", groupLabels[gnum], "", 0, 0, [this, gnum] { m_s.setTrackGroup(m_selTrack, gnum); m_modelDirty = true; }, trk);
    }
    {   // palette-only aliases: Ctrl+K, "midi channel 5" (the channel of the track in an exported MIDI file)
        static const char* chLabels[] = {"~MIDI Channel: Auto", "~MIDI Channel: 1", "~MIDI Channel: 2", "~MIDI Channel: 3", "~MIDI Channel: 4",
                                         "~MIDI Channel: 5", "~MIDI Channel: 6", "~MIDI Channel: 7", "~MIDI Channel: 8", "~MIDI Channel: 9",
                                         "~MIDI Channel: 10", "~MIDI Channel: 11", "~MIDI Channel: 12", "~MIDI Channel: 13", "~MIDI Channel: 14",
                                         "~MIDI Channel: 15", "~MIDI Channel: 16"};
        for (int ch = 0; ch <= 16; ++ch)
            addCmd("Track", chLabels[ch], "", 0, 0, [this, ch] { m_s.setTrackMidiChannel(m_selTrack, ch); m_modelDirty = true; }, trk);
    }
    addCmd("Track", "Mute Track", "Shift+M", 'M', S, [this] { m_s.setMute(m_selTrack, !m_tracks[size_t(m_selTrack)].mute); }, trk);
    addCmd("Track", "Solo Track", "Shift+S", 'S', S, [this] { m_s.setSolo(m_selTrack, !m_tracks[size_t(m_selTrack)].solo); }, trk);
    addCmd("Track", "Select Previous Track", "Up", KeyUp, 0, [this] { m_selTrack = std::max(0, m_selTrack - 1); m_selRegion = -1; }, trk);
    addCmd("Track", "Select Next Track", "Down", KeyDown, 0, [this] { m_selTrack = std::min(int(m_tracks.size()) - 1, m_selTrack + 1); m_selRegion = -1; }, trk);
    sep("Track");
    addCmd("Track", "Bounce in Place", "Ctrl+Alt+B", 'B', C | A, [this] { const int n = m_s.bounceTrackInPlace(m_selTrack); message(n >= 0 ? "Bounced the track to a new audio track." : "Nothing to bounce."); }, trk);
    addCmd("Track", "Track Color: Next", "", 0, 0, [this] {
        const core::Color c = m_tracks[size_t(m_selTrack)].color;
        static const unsigned pal[12] = {0x5b8cff, 0x3dc79a, 0xf2a33a, 0xe8628a, 0x9b6bf0, 0x4fc3e8, 0xb9d94a, 0xf06d4f, 0x6fa66a, 0xd07ad8, 0x8e9ab4, 0xe3c36b};
        int cur = -1;
        for (int i = 0; i < 12; ++i) if (((pal[i] >> 16) & 255) == c.r && ((pal[i] >> 8) & 255) == c.g && (pal[i] & 255) == c.b) cur = i;
        const unsigned n = pal[(cur + 1) % 12];
        core::Color nc; nc.r = std::uint8_t(n >> 16); nc.g = std::uint8_t(n >> 8); nc.b = std::uint8_t(n);
        m_s.setTrackColor(m_selTrack, nc);
    }, trk);

    // ------------------------------------------------------------------------------------------------ Region (audio)
    addCmd("Region", "Normalize...", "", 0, 0, [this] { dlgNormalize(); }, audio);
    addCmd("Region", "Reverse", "", 0, 0, sel([this](int t, int r) { m_s.reverseRegion(t, r); }), audio);
    addCmd("Region", "Trim Start to Playhead", "", 0, 0, [this] { trimAtPlayhead(true); }, audio);
    addCmd("Region", "Trim End to Playhead", "", 0, 0, [this] { trimAtPlayhead(false); }, audio);
    addCmd("Region", "Fades and Gain...", "", 0, 0, [this] { dlgFades(); }, audio);
    addCmd("Region", "Follow Tempo and Pitch...", "", 0, 0, [this] { dlgFlex(); }, audio);
    addCmd("Region", "Strip Silence...", "", 0, 0, [this] { dlgStripSilence(); }, audio);
    addCmd("Region", "Measure Loudness", "", 0, 0, sel([this](int t, int r) { const double l = m_s.regionLoudness(t, r); char b[64]; std::snprintf(b, sizeof b, "Integrated loudness: %.1f LUFS", l); message(b); }), audio);
    addCmd("Region", "Detect Tempo", "", 0, 0, sel([this](int t, int r) { const double b = m_s.detectRegionTempo(t, r); message(b > 0 ? "Detected tempo: " + std::to_string(int(std::lround(b))) + " BPM" : "Could not detect a tempo."); }), audio);
    sep("Region");
    addCmd("Region", "Adapt Project Tempo to Region...", "", 0, 0, [this] { dlgAdaptTempo(); }, audio);          // Logic Pro
    addCmd("Region", "Beat Detective...", "", 0, 0, [this] { dlgBeatDetective(); }, audio);                       // Pro Tools
    addCmd("Region", "Tab to Next Transient", "Tab", KeyTab, 0, [this] { tabTransient(true); });                  // Pro Tools
    addCmd("Region", "Tab to Previous Transient", "Shift+Tab", KeyTab, S, [this] { tabTransient(false); });       // Pro Tools

    // ------------------------------------------------------------------------------------------------ MIDI
    addCmd("MIDI", "Quantize...", "Q", 'Q', 0, [this] { dlgQuantize(); }, midi);
    addCmd("MIDI", "Scale Quantize...", "", 0, 0, [this] { dlgScaleQuantize(); }, midi);
    addCmd("MIDI", "Humanize...", "", 0, 0, [this] { dlgHumanize(); }, midi);
    addCmd("MIDI", "Chord Trigger...", "", 0, 0, [this] { dlgChordTrigger(); }, midi);
    addCmd("MIDI", "Arpeggiator...", "", 0, 0, [this] { dlgArpeggiator(); }, midi);
    addCmd("MIDI", "Transpose...", "", 0, 0, [this] { dlgTranspose(); }, midi);
    addCmd("MIDI", "Crescendo / Decrescendo...", "", 0, 0, [this] { dlgVelocityRamp(); }, midi);
    addCmd("MIDI", "Scale Velocity...", "", 0, 0, [this] { dlgScaleVelocity(); }, midi);
    addCmd("MIDI", "Fixed Note Length...", "", 0, 0, [this] { dlgFixedLength(); }, midi);
    sep("MIDI");
    addCmd("MIDI", "Legato", "", 0, 0, sel([this](int t, int r) { m_s.legatoRegion(t, r); }), midi);
    addCmd("MIDI", "Fix Overlaps", "", 0, 0, sel([this](int t, int r) { m_s.fixOverlapsRegion(t, r); }), midi);
    addCmd("MIDI", "Remove Duplicate Notes", "", 0, 0, sel([this](int t, int r) { m_s.removeDuplicatesRegion(t, r); }), midi);
    addCmd("MIDI", "Stretch Notes x2", "", 0, 0, [this] { applyToNotes([](const std::vector<core::Note>& n) { return core::stretchNotes(n, 2.0); }); }, midi);       // Ableton Live Transform
    addCmd("MIDI", "Stretch Notes /2", "", 0, 0, [this] { applyToNotes([](const std::vector<core::Note>& n) { return core::stretchNotes(n, 0.5); }); }, midi);
    addCmd("MIDI", "Strum Notes Down", "", 0, 0, [this] { applyToNotes([](const std::vector<core::Note>& n) { return core::strumNotes(n, 0.0625, true); }); }, midi);
    addCmd("MIDI", "Strum Notes Up", "", 0, 0, [this] { applyToNotes([](const std::vector<core::Note>& n) { return core::strumNotes(n, 0.0625, false); }); }, midi);
    addCmd("MIDI", "Double Pattern", "", 0, 0, [this] {                                                                                                                       // Ableton Live Duplicate Loop
        const int t = m_selTrack, r = m_selRegion;
        const double len = m_s.regionBeats(*region());
        const auto doubled = core::doublePattern(region()->notes, len);
        m_s.checkpoint(); m_s.resizeRegion(t, r, len * 2); m_s.setRegionNotes(t, r, doubled);
        message(fmt("Pattern doubled to %1 beats.", std::to_string(int(len * 2))));
    }, midi);
    addCmd("MIDI", "Retrograde", "", 0, 0, sel([this](int t, int r) { m_s.retrogradeRegion(t, r); }), midi);
    addCmd("MIDI", "Invert Pitches", "", 0, 0, sel([this](int t, int r) { m_s.invertRegion(t, r); }), midi);
    addCmd("MIDI", "Repeat Region 4 Times", "", 0, 0, sel([this](int t, int r) { m_s.repeatRegion(t, r, 4); }), midi);
    addCmd("MIDI", "Convert Drum Pattern to MIDI Region", "", 0, 0, [this] { const int n = m_s.convertPatternToMidi(m_selTrack, 4); if (n >= 0) message("Pattern converted to a MIDI region."); },
           [this] { return m_selTrack >= 0 && m_selTrack < int(m_tracks.size()) && m_tracks[size_t(m_selTrack)].type == core::TrackType::Drums; });
    sep("MIDI");
    addCmd("MIDI", "Riff Machine...", "Ctrl+R", 'R', C, [this] { dlgRiff(); }, midi);                              // FL Studio
    addCmd("MIDI", "Roll New Riff", "Ctrl+Alt+R", 'R', C | A, [this] { rollRiff(); }, midi);                      // FL Studio
    addCmd("MIDI", "Logical Editor...", "Ctrl+L", 'L', C, [this] { dlgLogical(); }, midi);                        // Cubase
    addCmd("MIDI", "Make Groove Template", "", 0, 0, sel([this](int t, int r) { m_s.makeGrooveTemplate(t, r, 0.25); }), midi);   // Logic Pro
    addCmd("MIDI", "Apply Groove Template...", "", 0, 0, [this] { dlgGroove(); }, [this, midi] { return midi() && m_s.hasGrooveTemplate(); });
    addCmd("MIDI", "Capture MIDI", "Ctrl+Shift+C", 'C', C | S, [this] {                                          // Ableton Live
        const int n = m_s.captureMidi(m_selTrack);
        if (n >= 0) { m_selRegion = n; m_dock = DockTab::Editor; m_showDock = true; }
    }, trk);
    sep("MIDI");
    addCmd("MIDI", "Fold Notes", "Ctrl+F", 'F', C, [this] { m_foldNotes = !m_foldNotes; m_dock = DockTab::Editor; }, midi, [this] { return m_foldNotes; });              // Ableton Live
    addCmd("MIDI", "Scale Highlight", "", 0, 0, [this] { m_scaleHighlight = !m_scaleHighlight; }, midi, [this] { return m_scaleHighlight; });
    addCmd("MIDI", "Lock Notes to Scale", "", 0, 0, [this] { m_scaleSnap = !m_scaleSnap; }, midi, [this] { return m_scaleSnap; });
    addCmd("MIDI", "Chord Stamp: Off", "", 0, 0, [this] { m_stampType = -1; }, midi);
    addCmd("MIDI", "Chord Stamp: Major", "", 0, 0, [this] { m_stampType = int(core::ChordParams::Major); m_dock = DockTab::Editor; }, midi);
    addCmd("MIDI", "Chord Stamp: Minor", "", 0, 0, [this] { m_stampType = int(core::ChordParams::Minor); m_dock = DockTab::Editor; }, midi);
    addCmd("MIDI", "Chord Stamp: Diatonic Triad", "", 0, 0, [this] { m_stampType = 100; m_dock = DockTab::Editor; }, midi);
    addCmd("MIDI", "Musical Typing", "Ctrl+Shift+K", 'K', C | S, [this] { m_typing = !m_typing; message(m_typing ? "Musical typing on: A W S E D F T G Y H U J K play notes, Z / X change the octave." : "Musical typing off."); }, {}, [this] { return m_typing; });
    addCmd("MIDI", "Typing Octave Down", "Z", 'Z', 0, [this] { m_typingOctave = std::max(0, m_typingOctave - 1); }, [this] { return m_typing; });
    addCmd("MIDI", "Typing Octave Up", "X", 'X', 0, [this] { m_typingOctave = std::min(8, m_typingOctave + 1); }, [this] { return m_typing; });

    // ------------------------------------------------------------------------------------------------ Audio (project level)
    addCmd("Audio", "Tempo...", "Ctrl+T", 'T', C, [this] { dlgTempo(); });
    addCmd("Audio", "Time Signature...", "", 0, 0, [this] { dlgTimeSignature(); });
    addCmd("Audio", "Tap Tempo", "T", 'T', 0, [this] { tapTempo(); });
    addCmd("Audio", "Metronome", "K", 'K', 0, [this] { m_s.setMetronome(!m_s.metronome()); }, {}, [this] { return m_s.metronome(); });
    addCmd("Audio", "Count-in", "", 0, 0, [this] { m_s.setCountIn(!m_s.countIn()); }, {}, [this] { return m_s.countIn(); });

    // ------------------------------------------------------------------------------------------------ Mix
    addCmd("Mix", "Show Mixer", "Ctrl+1", '1', C, [this] { m_dock = DockTab::Mixer; m_showDock = true; });
    addCmd("Mix", "Show Editor", "E", 'E', 0, [this] { m_dock = DockTab::Editor; m_showDock = true; m_dockH = std::max(m_dockH, 430.f); });
    addCmd("Mix", "Show Step Sequencer", "Ctrl+3", '3', C, [this] { m_dock = DockTab::Steps; m_showDock = true; });
    addCmd("Mix", "Show Devices", "Ctrl+4", '4', C, [this] { m_dock = DockTab::Devices; m_showDock = true; });
    addCmd("Mix", "Show Snapshots", "Ctrl+5", '5', C, [this] { m_dock = DockTab::Snapshots; m_showDock = true; });
    addCmd("Mix", "Show Automation", "Ctrl+6", '6', C, [this] { m_dock = DockTab::Automation; m_showDock = true; });
    addCmd("Mix", "Show Drummer", "Ctrl+7", '7', C, [this] { m_dock = DockTab::Drummer; m_showDock = true; });
    sep("Mix");
    addCmd("Mix", "Save Mixer Snapshot", "Ctrl+Shift+Enter", KeyEnter, C | S, [this] { const int n = m_s.saveMixSnapshot(); m_dock = DockTab::Snapshots; m_showDock = true; message(fmt("Saved mixer snapshot %1 (Alt+%2 recalls it)", std::to_string(n + 1), std::to_string(std::min(9, n + 1)))); });   // Cubase
    addCmd("Mix", "Recall Previous Snapshot", "", 0, 0, [this] { const auto s = m_s.mixSnapshots(); if (!s.empty()) { static int i = 0; i = (i + int(s.size()) - 1) % int(s.size()); m_s.recallMixSnapshot(i); message(fmt("Recalled %1", s[size_t(i)].name)); } });
    addCmd("Mix", "Aux A: Reverb...", "", 0, 0, [this] { dlgAux(0); });
    addCmd("Mix", "Aux B: Echo...", "", 0, 0, [this] { dlgAux(1); });
    addCmd("Mix", "Add Effect to Track...", "", 0, 0, [this] { m_dock = DockTab::Devices; m_showDock = true; message("Use + Add Effect in the Devices panel or drag an effect from the browser."); });

    // ------------------------------------------------------------------------------------------------ View
    addCmd("View", "Pointer Tool", "1", '1', 0, [this] { m_tool = Tool::Pointer; });
    addCmd("View", "Pencil Tool", "2", '2', 0, [this] { m_tool = Tool::Pencil; });
    addCmd("View", "Scissors Tool", "3", '3', 0, [this] { m_tool = Tool::Scissors; });
    addCmd("View", "Eraser Tool", "4", '4', 0, [this] { m_tool = Tool::Eraser; });
    addCmd("View", "Mute Tool", "5", '5', 0, [this] { m_tool = Tool::Mute; });
    sep("View");
    addCmd("View", "Show / Hide Browser", "B", 'B', 0, [this] { m_showBrowser = !m_showBrowser; m_host.settings["showBrowser"] = m_showBrowser ? "1" : "0"; }, {}, [this] { return m_showBrowser; });
    addCmd("View", "Show / Hide Inspector", "I", 'I', 0, [this] { m_showInspector = !m_showInspector; m_host.settings["showInspector"] = m_showInspector ? "1" : "0"; }, {}, [this] { return m_showInspector; });
    addCmd("View", "Workspace: Arrange", "F5", KeyF5, 0, [this] { applyWorkspace(0); });                                                                                  // Logic screensets, Cubase workspaces
    addCmd("View", "Workspace: Mix", "F6", KeyF6, 0, [this] { applyWorkspace(1); });
    addCmd("View", "Workspace: Edit", "F7", KeyF7, 0, [this] { applyWorkspace(2); });
    addCmd("View", "Maximize Bottom Panel", "Shift+X", 'X', S, [this] { m_dockMax = !m_dockMax; m_showDock = true; }, {}, [this] { return m_dockMax; });
    addCmd("View", "Show / Hide Bottom Panel", "X", 'X', 0, [this] { m_showDock = !m_showDock; }, {}, [this] { return m_showDock; });
    sep("View");
    addCmd("View", "Zoom In", "+", KeyPlus, 0, [this] { zoomAt(m_rLanes.cx(), 1.3); });
    addCmd("View", "~Zoom In (=)", "=", '=', 0, [this] { zoomAt(m_rLanes.cx(), 1.3); });
    addCmd("View", "Zoom Out", "-", KeyMinus, 0, [this] { zoomAt(m_rLanes.cx(), 1 / 1.3); });
    addCmd("View", "Zoom to Fit Project", "Z", 'Z', 0, [this] { zoomToFit(); });
    addCmd("View", "Zoom to Selected Region", "Shift+Z", 'Z', S, [this] {
        if (const core::Region* r = region()) { const double len = std::max(1.0, m_s.regionBeats(*r)); m_ppb = std::clamp(double(m_rLanes.w) / (len + 2.0), 3.0, 900.0); m_scrollX = std::max(0.0, r->start - 1.0); }
    }, reg);
    addCmd("View", "Cycle Track Height", "H", 'H', 0, [this] { m_trackH = m_trackH < 50 ? 72.f : m_trackH < 100 ? 120.f : 40.f; });
    addCmd("View", "Track Height: Small", "", 0, 0, [this] { m_trackH = 40.f; });   // Pro Tools: mini / small / medium / large / extreme
    addCmd("View", "Track Height: Medium", "", 0, 0, [this] { m_trackH = 72.f; });
    addCmd("View", "Track Height: Large", "", 0, 0, [this] { m_trackH = 120.f; });
    addCmd("View", "Track Height: Extra Large", "", 0, 0, [this] { m_trackH = 200.f; });
    addCmd("View", "Mixer: Narrow Strips", "", 0, 0, [this] { m_mixNarrow = !m_mixNarrow; }, {}, [this] { return m_mixNarrow; });
    addCmd("View", "Browser: Project Clips", "", 0, 0, [this] { m_showBrowser = true; m_browserTab = 4; });
    addCmd("View", "Follow Playhead", "F", 'F', 0, [this] { m_follow = !m_follow; message(m_follow ? "Following the playhead." : "Not following the playhead."); }, {}, [this] { return m_follow; });
    addCmd("View", "Snap to Grid", "G", 'G', 0, [this] { m_snapOn = !m_snapOn; message(m_snapOn ? fmt("Snap on: %1", gridName()) : std::string("Snap off.")); }, {}, [this] { return m_snapOn; });
    addCmd("View", "Next Grid Size", "Shift+G", 'G', S, [this] { m_snapIdx = (m_snapIdx + 1) % 8; m_snapOn = true; message(fmt("Grid: %1", gridName())); });
    sep("View");
    addCmd("View", "Command Palette", "Ctrl+K", 'K', C, [this] { m_paletteOpen = true; m_paletteQuery.clear(); m_paletteSel = 0; });
    addCmd("View", "Keyboard Shortcuts", "F1", KeyF1, 0, [this] { m_helpOpen = !m_helpOpen; m_helpScroll = 0; });

    // ------------------------------------------------------------------------------------------------ Transport
    addCmd("Transport", "Play / Pause", "Space", KeySpace, 0, [this] { if (m_s.isPlaying()) m_s.pause(); else m_s.play(); });
    addCmd("Transport", "Stop and Return to Start", "Enter", KeyEnter, 0, [this] { m_s.stop(); m_s.seekBeats(0); ensureVisible(0); });
    addCmd("Transport", "Go to Start", "Home", KeyHome, 0, [this] { m_s.seekBeats(0); ensureVisible(0); });
    addCmd("Transport", "Go to End", "End", KeyEnd, 0, [this] { const double e = m_s.projectEndBeats(); m_s.seekBeats(e); ensureVisible(e); });
    addCmd("Transport", "Record", "R", 'R', 0, [this] { if (m_s.isRecording()) m_s.stopRecording(); else m_s.startRecording(); });
    addCmd("Transport", "Cycle", "C", 'C', 0, [this] { m_s.setLoop(!m_s.loopOn()); }, {}, [this] { return m_s.loopOn(); });
    addCmd("Transport", "Add Marker", "M", 'M', 0, [this] { m_s.addMarker(m_s.positionBeats()); message("Marker added."); });
    addCmd("Transport", "Next Marker", "]", ']', 0, [this] { gotoMarker(1); });
    addCmd("Transport", "Previous Marker", "[", '[', 0, [this] { gotoMarker(-1); });
    addCmd("Transport", "Playhead Back One Bar", "Left", KeyLeft, 0, [this] { const auto tm = m_s.tempoMap(); const int bar = tm->barAt(m_s.positionBeats() - 1e-6); m_s.seekBeats(std::max(0.0, tm->barStart(std::max(1, bar)))); ensureVisible(m_s.positionBeats()); });
    addCmd("Transport", "Playhead Forward One Bar", "Right", KeyRight, 0, [this] { const auto tm = m_s.tempoMap(); const int bar = tm->barAt(m_s.positionBeats()); m_s.seekBeats(tm->barStart(bar + 1)); ensureVisible(m_s.positionBeats()); });

    // ------------------------------------------------------------------------------------------------ Help
    addCmd("Help", "Tutorial...", "", 0, 0, [this] { openTutorial(0); });
    sep("Help");
    addCmd("Help", "Command Palette...", "Ctrl+K", 0, 0, [this] { m_paletteOpen = true; m_paletteQuery.clear(); m_paletteSel = 0; });
    addCmd("Help", "Keyboard Shortcuts...", "F1", 0, 0, [this] { m_helpOpen = true; m_helpScroll = 0; });
    addCmd("Help", "About MiniDAW", "", 0, 0, [this] { dlgAbout(); });
    (void)midi; (void)audio;
}

}  // namespace vkui
