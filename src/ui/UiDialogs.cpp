// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Parameter dialogs of the Qt-free UI (the counterparts of the Qt dialogs of MainWindow.cpp).
#include <algorithm>
#include <cmath>
#include "../core/MidiOps.h"
#include "../dsp/Drummer.h"
#include "Ui.h"

namespace ui {

namespace {
const std::vector<std::string> kGrids = {"1/4", "1/8", "1/16", "1/32"};
const double kGridBeats[] = {1.0, 0.5, 0.25, 0.125};
const std::vector<std::string> kKeys = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
}  // namespace

void NativeUi::openDialog(std::unique_ptr<Dialog> d) {
    m_menu.clear();
    d->setScreen(m_w, m_h);
    m_dialog = std::move(d);
    m_dirty = true;
}

void NativeUi::dlgTranspose() {
    withRegion([this](int t, int r) {
        openDialog(std::make_unique<Dialog>("Transpose", Dialog::Values{Field::number("Interval", last("transpose", 12), -48, 48, 1, 0, " semitones")}, [this, t, r](const Dialog::Values& v) {
            m_last["transpose"] = v[0].value;
            m_s.transposeRegion(t, r, int(v[0].value));
        }));
    });
}

void NativeUi::dlgVelocityRamp() {
    withRegion([this](int t, int r) {
        openDialog(std::make_unique<Dialog>("Crescendo / Decrescendo", Dialog::Values{Field::number("Start velocity", last("rampFrom", 30), 1, 127), Field::number("End velocity", last("rampTo", 110), 1, 127)}, [this, t, r](const Dialog::Values& v) {
            m_last["rampFrom"] = v[0].value; m_last["rampTo"] = v[1].value;
            m_s.velocityRampRegion(t, r, int(v[0].value), int(v[1].value));
        }));
    });
}

void NativeUi::dlgRepeat() {
    withRegion([this](int t, int r) {
        openDialog(std::make_unique<Dialog>("Repeat Region", Dialog::Values{Field::number("Number of copies", last("repeat", 3), 1, 64)}, [this, t, r](const Dialog::Values& v) {
            m_last["repeat"] = v[0].value;
            m_s.repeatRegion(t, r, int(v[0].value));
        }));
    });
}

void NativeUi::dlgQuantize() {
    withRegion([this](int t, int r) {
        openDialog(std::make_unique<Dialog>("Quantize Region", Dialog::Values{Field::choice("Grid", kGrids, int(last("qGrid", 2))), Field::slider("Strength", last("qStrength", 100), 0, 100, 0, " %"), Field::slider("Swing", last("qSwing", 50), 50, 75, 0, " %"), Field::check("Also quantize note lengths", last("qLen", 0) > 0)}, [this, t, r](const Dialog::Values& v) {
            m_last["qGrid"] = v[0].index; m_last["qStrength"] = v[1].value; m_last["qSwing"] = v[2].value; m_last["qLen"] = v[3].checked;
            core::QuantizeParams p;
            p.grid = kGridBeats[v[0].index];
            p.strength = v[1].value / 100.0;
            p.swing = v[2].value / 100.0;
            p.lengths = v[3].checked;
            m_s.quantizeRegion(t, r, p);
        }, "Quantize"));
    });
}

void NativeUi::dlgScale() {
    withRegion([this](int t, int r) {
        std::vector<std::string> names = {"Major", "Minor", "Harmonic Minor", "Melodic Minor", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Pentatonic Major", "Pentatonic Minor", "Blues", "Chromatic"};
        openDialog(std::make_unique<Dialog>("Scale Quantize", Dialog::Values{Field::choice("Key", kKeys, int(last("scRoot", 0))), Field::choice("Scale", names, int(last("scScale", 0)))}, [this, t, r](const Dialog::Values& v) {
            m_last["scRoot"] = v[0].index; m_last["scScale"] = v[1].index;
            core::ScaleParams p;
            p.root = v[0].index;
            p.scale = v[1].index;
            m_s.scaleQuantizeRegion(t, r, p);
        }));
    });
}

void NativeUi::dlgHumanize() {
    withRegion([this](int t, int r) {
        openDialog(std::make_unique<Dialog>("Humanize", Dialog::Values{Field::slider("Timing", last("hTiming", 30), 0, 100, 0, " ms/1000"), Field::number("Velocity", last("hVel", 10), 0, 64), Field::number("Random seed", last("hSeed", 1), 1, 9999)}, [this, t, r](const Dialog::Values& v) {
            m_last["hTiming"] = v[0].value; m_last["hVel"] = v[1].value; m_last["hSeed"] = v[2].value;
            core::HumanizeParams p;
            p.timing = v[0].value / 1000.0;
            p.velocity = int(v[1].value);
            p.seed = int(v[2].value);
            m_s.humanizeRegion(t, r, p);
        }));
    });
}

void NativeUi::dlgArpeggiate() {
    withRegion([this](int t, int r) {
        openDialog(std::make_unique<Dialog>("Arpeggiate Region", Dialog::Values{Field::choice("Pattern", {"Up", "Down", "Up / Down", "Down / Up", "Random", "As played"}, int(last("aPattern", 0))), Field::choice("Rate", kGrids, int(last("aRate", 2))), Field::number("Octaves", last("aOct", 1), 1, 4), Field::slider("Gate", last("aGate", 80), 5, 100, 0, " %"), Field::number("Random seed", last("aSeed", 1), 1, 9999)}, [this, t, r](const Dialog::Values& v) {
            m_last["aPattern"] = v[0].index; m_last["aRate"] = v[1].index; m_last["aOct"] = v[2].value; m_last["aGate"] = v[3].value; m_last["aSeed"] = v[4].value;
            core::ArpParams p;
            p.pattern = v[0].index;
            p.rate = kGridBeats[v[1].index];
            p.octaves = int(v[2].value);
            p.gate = v[3].value / 100.0;
            p.seed = int(v[4].value);
            m_s.arpeggiateRegion(t, r, p);
        }));
    });
}

void NativeUi::dlgChord() {
    withRegion([this](int t, int r) {
        openDialog(std::make_unique<Dialog>("Chord Trigger", Dialog::Values{Field::choice("Chord", {"Major", "Minor", "Diminished", "Augmented", "Sus2", "Sus4", "Major 7", "Minor 7", "Dominant 7", "Octave"}, int(last("cType", 0))), Field::number("Inversion", last("cInv", 0), 0, 3)}, [this, t, r](const Dialog::Values& v) {
            m_last["cType"] = v[0].index; m_last["cInv"] = v[1].value;
            core::ChordParams p;
            p.type = v[0].index;
            p.inversion = int(v[1].value);
            m_s.chordTriggerRegion(t, r, p);
        }));
    });
}

void NativeUi::dlgFixedLength() {
    withRegion([this](int t, int r) {
        openDialog(std::make_unique<Dialog>("Fixed Note Length", Dialog::Values{Field::choice("Length", {"1/16", "1/8", "1/4", "1/2", "1 Bar"}, int(last("flen", 2)))}, [this, t, r](const Dialog::Values& v) {
            m_last["flen"] = v[0].index;
            const double len[] = {0.25, 0.5, 1.0, 2.0, 4.0};
            m_s.fixedLengthRegion(t, r, len[v[0].index]);
        }));
    });
}

void NativeUi::dlgScaleVelocity() {
    withRegion([this](int t, int r) {
        openDialog(std::make_unique<Dialog>("Scale Velocity", Dialog::Values{Field::slider("Scale", last("svPct", 100), 10, 300, 0, " %"), Field::number("Offset", last("svOff", 0), -100, 100)}, [this, t, r](const Dialog::Values& v) {
            m_last["svPct"] = v[0].value; m_last["svOff"] = v[1].value;
            m_s.scaleVelocityRegion(t, r, int(v[0].value), int(v[1].value));
        }));
    });
}

void NativeUi::dlgStripSilence() {
    withRegion([this](int t, int r) {
        openDialog(std::make_unique<Dialog>("Strip Silence", Dialog::Values{Field::slider("Threshold", last("ssDb", -40), -80, -10, 0, " dB"), Field::number("Minimum silence", last("ssMin", 100), 10, 2000, 10, 0, " ms"), Field::number("Padding", last("ssPad", 10), 0, 500, 5, 0, " ms")}, [this, t, r](const Dialog::Values& v) {
            m_last["ssDb"] = v[0].value; m_last["ssMin"] = v[1].value; m_last["ssPad"] = v[2].value;
            const int n = m_s.stripSilenceRegion(t, r, v[0].value, v[1].value, v[2].value);
            if (n > 0) message(std::to_string(n) + (n == 1 ? " region" : " regions") + " after Strip Silence.");
        }, "Strip"));
    });
}

void NativeUi::dlgDetectTempo() {
    withRegion([this](int t, int r) {
        const auto& rg = m_tracks[size_t(t)].regions[size_t(r)];
        if (rg.midi) { message("Select an audio region first."); return; }
        const double bpm = m_s.detectRegionTempo(t, r), semis = rg.semitones;
        if (bpm <= 0) { message("No clear tempo was found in this region."); return; }
        char buf[64];
        std::snprintf(buf, sizeof buf, "Detected tempo: %.1f BPM", bpm);
        Dialog::Values fields = {Field::note(buf),
                                 Field::button("Follow project tempo", [this, t, r, bpm, semis] { m_s.setRegionFlex(t, r, true, bpm, semis); }, true),
                                 Field::button("Set project tempo", [this, bpm] { m_s.setBpm(std::clamp(std::round(bpm * 10.0) / 10.0, 20.0, 300.0)); }, true)};
        openDialog(std::make_unique<Dialog>("Detect Tempo", std::move(fields), nullptr, "Close", false));
    });
}

void NativeUi::dlgPatternToMidi() {
    if (m_selTrack < 0 || m_selTrack >= int(m_tracks.size()) || m_tracks[size_t(m_selTrack)].type != core::TrackType::Drums) { message("Select a drum machine track first."); return; }
    const int t = m_selTrack;
    openDialog(std::make_unique<Dialog>("Convert Pattern to MIDI Region", Dialog::Values{Field::number("Bars", last("pmBars", 4), 1, 64)}, [this, t](const Dialog::Values& v) {
        m_last["pmBars"] = v[0].value;
        const int nt = m_s.convertPatternToMidi(t, int(v[0].value));
        if (nt >= 0) { m_selTrack = nt; m_selRegion = -1; }
    }));
}

void NativeUi::dlgTimeSignature() {
    const auto tm = m_s.tempoMap();
    const int bar = tm->barAt(m_s.positionBeats());
    const auto cur = tm->meterAtBeat(m_s.positionBeats());
    int denIdx = 2;
    const int dens[] = {1, 2, 4, 8, 16, 32};
    for (int i = 0; i < 6; ++i) if (dens[i] == cur.den) denIdx = i;
    openDialog(std::make_unique<Dialog>("Time Signature", Dialog::Values{Field::number("Beats per bar", cur.num, 1, 32), Field::choice("Note value", {"1", "2", "4", "8", "16", "32"}, denIdx), Field::number("Starting at bar", bar, 1, 9999)}, [this](const Dialog::Values& v) {
        auto pts = m_s.tempoMap()->meterPoints();
        const int from = int(v[2].value);
        pts.erase(std::remove_if(pts.begin(), pts.end(), [&](const dsp::MeterPoint& m) { return m.bar == from; }), pts.end());
        const int dens2[] = {1, 2, 4, 8, 16, 32};
        pts.push_back({from, int(v[0].value), dens2[v[1].index]});
        m_s.setMeterPoints(pts);
    }));
}

void NativeUi::dlgTempo() {
    openDialog(std::make_unique<Dialog>("Tempo", Dialog::Values{Field::number("Project tempo", m_s.bpm(), 20, 300, 0.5, 1, " BPM")}, [this](const Dialog::Values& v) { m_s.setBpm(v[0].value); }));
}

void NativeUi::dlgBounce() {
    m_s.pause();
    openDialog(std::make_unique<Dialog>("Bounce", Dialog::Values{Field::choice("Range", {"Whole project", "Cycle range"}, m_s.loopOn() ? 1 : int(last("bRange", 0))), Field::choice("Format", {"16-bit PCM", "24-bit PCM", "32-bit float"}, int(last("bFormat", 0))), Field::check("Dither (16-bit)", last("bDither", 1) > 0), Field::check("Normalize", last("bNorm", 0) > 0), Field::number("Normalize level", last("bLevel", -1), -12, 0, 0.5, 1, " dBFS"), Field::check("Add reverb / echo tail (2 s)", last("bTail", 1) > 0)}, [this](const Dialog::Values& v) {
        m_last["bRange"] = v[0].index; m_last["bFormat"] = v[1].index; m_last["bDither"] = v[2].checked; m_last["bNorm"] = v[3].checked; m_last["bLevel"] = v[4].value; m_last["bTail"] = v[5].checked;
        if (!m_host.fileDialog) { message("File dialogs are not available here."); return; }
        std::string path = m_host.fileDialog("Bounce to WAV", "WAV|*.wav", true);
        if (path.empty()) return;
        if (path.find('.') == std::string::npos) path += ".wav";
        core::ExportOptions opt;
        opt.cycleOnly = v[0].index == 1;
        opt.format = dsp::WavFormat(v[1].index);
        opt.dither = v[2].checked && opt.format == dsp::WavFormat::Pcm16;
        opt.normalize = v[3].checked;
        opt.normalizeDb = float(v[4].value);
        opt.tail = v[5].checked;
        std::string err;
        message(m_s.exportAudio(path, opt, &err) ? "Bounced to " + path : "Could not bounce: " + err);
    }, "Bounce"));
}

void NativeUi::dlgAbout() {
    openDialog(std::make_unique<Dialog>("About MiniDAW", Dialog::Values{Field::note("MiniDAW - a lightweight Logic Pro-inspired digital audio workstation."), Field::note("Qt-free native version. GPL-3.0-or-later. Fonts: Liberation Sans (SIL OFL).")}, nullptr, "Close", false));
}

void NativeUi::dlgRename() {
    if (hasRegion()) {
        const int t = m_selTrack, r = m_selRegion;
        openDialog(std::make_unique<Dialog>("Rename Region", Dialog::Values{Field::input("Name", m_tracks[size_t(t)].regions[size_t(r)].name)}, [this, t, r](const Dialog::Values& v) { m_s.renameRegion(t, r, v[0].text); }));
    } else if (m_selTrack >= 0 && m_selTrack < int(m_tracks.size())) {
        const int t = m_selTrack;
        openDialog(std::make_unique<Dialog>("Rename Track", Dialog::Values{Field::input("Name", m_tracks[size_t(t)].name)}, [this, t](const Dialog::Values& v) { m_s.renameTrack(t, v[0].text); }));
    } else message("Select a track or a region first.");
}

}  // namespace ui

namespace ui {

void NativeUi::dlgPreferences() {
    const auto outs = m_host.audioDevices ? m_host.audioDevices(false) : std::vector<std::pair<std::string, std::string>>{};
    const auto ins = m_host.audioDevices ? m_host.audioDevices(true) : std::vector<std::pair<std::string, std::string>>{};
    const auto midis = m_host.midiDevices ? m_host.midiDevices() : std::vector<std::string>{};
    const auto langs = m_host.languages ? m_host.languages() : std::vector<std::pair<std::string, std::string>>{};
    auto names = [](const std::vector<std::pair<std::string, std::string>>& v, bool withDefault) {
        std::vector<std::string> n;
        if (withDefault) n.push_back("System Default");
        for (const auto& d : v) n.push_back(d.second);
        return n;
    };
    auto indexOf = [](const std::vector<std::pair<std::string, std::string>>& v, const std::string& id) {
        for (size_t i = 0; i < v.size(); ++i) if (v[i].first == id) return int(i) + 1;
        return 0;
    };
    auto& st = m_host.settings;
    auto get = [&](const char* k, const char* def) { auto it = st.find(k); return it == st.end() ? std::string(def) : it->second; };
    std::vector<std::string> bufNames = {"256 samples (5 ms)", "512 samples (11 ms)", "1024 samples (23 ms)", "2048 samples (46 ms)", "4096 samples (92 ms)"};
    const int bufs[] = {256, 512, 1024, 2048, 4096};
    int bufIdx = 3;
    for (int i = 0; i < 5; ++i) if (std::to_string(bufs[i]) == get("buffer", "2048")) bufIdx = i;
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
    Dialog::Values fields = {Field::choice("Output device", names(outs, true), indexOf(outs, get("output", ""))),
                             Field::choice("Buffer size", bufNames, bufIdx),
                             Field::choice("Input device", names(ins, true), indexOf(ins, get("input", ""))),
                             Field::number("Recording latency compensation", std::stod(get("recOffset", "0")), -500, 500, 1, 0, " ms"),
                             Field::choice("MIDI input", midiNames, midiIdx),
                             Field::choice("Language", langNames, langIdx),
                             Field::number("Autosave every", std::stod(get("autosave", "2")), 0, 60, 1, 0, " min"),
                             Field::input("Loops folder", get("loops", m_host.loopFolder.c_str())),
                             Field::note("Restart MiniDAW to apply a new language. Autosave 0 = off.")};
    openDialog(std::make_unique<Dialog>("Preferences", std::move(fields), [this, outs, ins, midis, langList, bufs](const Dialog::Values& v) {
        std::map<std::string, std::string> s = m_host.settings;
        s["output"] = v[0].index == 0 ? "" : outs[size_t(v[0].index - 1)].first;
        s["buffer"] = std::to_string(bufs[v[1].index]);
        s["input"] = v[2].index == 0 ? "" : ins[size_t(v[2].index - 1)].first;
        s["recOffset"] = std::to_string(int(v[3].value));
        s["midiInput"] = v[4].index == 0 ? "" : midis[size_t(v[4].index - 1)];
        s["language"] = langList[size_t(v[5].index)].first;
        s["autosave"] = std::to_string(int(v[6].value));
        s["loops"] = v[7].text;
        m_host.settings = s;
        if (!v[7].text.empty() && v[7].text != m_host.loopFolder) { m_host.loopFolder = v[7].text; m_loopsLoaded = false; }
        m_s.setRecordingOffsetMs(int(v[3].value));
        if (m_host.applySettings) m_host.applySettings(s);
        message("Preferences saved.");
    }));
}

}  // namespace ui
