// Tests of the Qt-free core: JSON, text, WAV files, Session (project model, undo, save / load, autosave,
// export, bounce in place) and the MIDI operations.
#include <cmath>
#include <regex>
#include <cstdio>
#include <fstream>
#include "../src/core/AppPaths.h"
#include "../src/core/ExportFormats.h"
#include "../src/core/FileTypes.h"
#include "../src/core/Id3.h"
#include <chrono>
#include <map>
#include <thread>
#include "../src/core/Mp3Gapless.h"
#include "../src/core/LogicImport.h"
#include "../src/core/LogicProjectData.h"
#include "../src/core/Bplist.h"
#include "../src/core/LogicArrangement.h"
#include "../src/dsp/MidiFile.h"
#include "aiff_util.h"
#include "../src/core/Files.h"
#include "../src/dsp/AudioAnalysis.h"
#include "../src/dsp/TempoDetect.h"
#include "../src/core/Json.h"
#include "../src/core/Session.h"
int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
using namespace core;

// A Session without the two default tracks of a new project.
struct S : Session {
    S() { while (trackCount() > 0) removeTrack(0); clearHistory(); }
};

static std::string tmpDir() {
    static std::string d;
    if (d.empty()) { d = "/tmp/minidaw_core_test"; removeTree(d); makeDirs(d); }
    return d;
}

static std::string makeWav(const std::string& name, float freq, int frames, float amp = 0.5f) {
    std::vector<float> pcm(size_t(frames) * 2);
    for (int i = 0; i < frames; ++i) pcm[2*i] = pcm[2*i+1] = amp * std::sin(2 * 3.14159265f * freq * float(i) / kSampleRate);
    const std::string p = tmpDir() + "/" + name;
    std::string err;
    CHECK(writeWavFile(p, pcm, &err));
    return p;
}

static float peak(const std::vector<float>& v, size_t from = 0, size_t to = size_t(-1)) {
    float p = 0; for (size_t i = from; i < v.size() && i < to; ++i) p = std::max(p, std::fabs(v[i])); return p;
}

static void testJson() {
    Json j;
    std::string err;
    CHECK(Json::parse(R"({"a":1,"b":[true,false,null,"x\n\u00e9\ud83d\ude00"],"c":{"d":-2.5e2}})", j, &err));
    CHECK(j["a"].toInt() == 1 && j["b"].size() == 4 && j["b"].items()[0].toBool() && j["c"]["d"].toDouble() == -250.0);
    CHECK(j["b"].items()[3].toString() == "x\n\xc3\xa9\xf0\x9f\x98\x80");
    CHECK(j["missing"].isNull() && j["missing"].toInt(7) == 7 && j["a"].toString("z") == "z");
    Json k; CHECK(Json::parse(j.dump(2), k) && k.dump(0) == j.dump(0));       // round trip
    Json n = Json::object(); n["pi"] = 3.141592653589793; n["big"] = 1234567890123.0; n["s"] = std::string("q\"\\\t\x01");
    Json m; CHECK(Json::parse(n.dump(0), m) && m["pi"].toDouble() == 3.141592653589793 && m["big"].toDouble() == 1234567890123.0 && m["s"].toString() == "q\"\\\t\x01");
    CHECK(!Json::parse("{", j, &err) && !err.empty());
    CHECK(!Json::parse("[1,]", j) && !Json::parse("{\"a\" 1}", j) && !Json::parse("[1] x", j) && !Json::parse("\"abc", j) && !Json::parse("", j));
    CHECK(!Json::parse(std::string(500, '['), j));                             // nesting limit, no stack overflow
    Json o = Json::object(); o["b"] = 1; o["a"] = 2; CHECK(o.dump(0) == "{\"b\":1,\"a\":2}");   // insertion order
}

static void testText() {
    CHECK(Text("%1 copy").arg(std::string("Piano")).str() == "Piano copy");
    CHECK(Text("%1 / %2").arg(1).arg(2.5).str() == "1 / 2.5");
    CHECK(Text("%2 %1").arg("a").arg("b").str() == "b a");
    CHECK(Text("%1%").arg(5).str() == "5%");
    CHECK(Text("%1").arg(3.14159, 0, 'f', 2).str() == "3.14");
    CHECK(trimmed("  x y \n") == "x y" && startsWith("vst3:abc", "vst3:") && endsWith("a.wav", ".wav") && !endsWith("a", "ab"));
}

static void testWav() {
    const std::string p = makeWav("a.wav", 440, 22050);
    std::vector<float> pcm; std::string err;
    CHECK(fileExists(p));
    CHECK(readWavFile(p, pcm, &err) && pcm.size() == 22050 * 2);
    CHECK(std::fabs(pcm[100] - 0.5f * std::sin(2 * 3.14159265f * 440 * 50 / kSampleRate)) < 1e-3f);
    CHECK(!readWavFile(tmpDir() + "/nope.wav", pcm, &err));
}

// helper: a project with an instrument track, a drummer track, audio, markers, automation
static void buildProject(Session& s, const std::string& wav) {
    s.setBpm(100);
    int ti = s.addTrack(TrackType::Instrument, "Keys");
    int r = s.addMidiRegion(ti, 0, 4);
    s.setRegionNotes(ti, r, {{60, 0, 1, 100}, {64, 0, 1, 90}, {67, 1, 2, 80}});
    s.addInsert(ti, "chorus");
    s.setPan(ti, -0.4f);
    s.setAuto(ti, AutoParam::Pan, {{0, -1.f}, {8, 1.f}});
    s.setAuto(ti, AutoParam::SendA, {{0, 0.f}, {4, 0.5f}});
    int dr = s.addTrack(TrackType::Drummer, "Drums");
    s.addDrummerRegion(dr, 0, 2);
    int dm = s.addTrack(TrackType::Drums, "Machine");
    s.setStep(dm, 0, 0, true); s.setStep(dm, 2, 4, true);
    s.importAudio(wav, -1, 2);
    s.addMarker(8, "Chorus"); s.addMarker(0, "Intro");
    s.setLoop(true, 4, 12);
    s.setMetronome(true);
    s.setMasterVolume(0.7f);
    s.setTrackColor(ti, Color{1, 2, 250});
}

static void testSaveLoad() {
    const std::string wav = makeWav("loop.wav", 330, 44100 * 3);
    S a;
    buildProject(a, wav);
    CHECK(a.trackCount() == 4);
    const std::string file = tmpDir() + "/proj/song.mdaw";
    std::string err;
    makeDirs(tmpDir() + "/proj");
    CHECK(a.saveProject(file, &err));
    CHECK(!a.isDirty() && a.projectPath() == file);

    S b;
    CHECK(b.loadProject(file, &err));
    const auto ta = a.snapshot(), tb = b.snapshot();
    CHECK(ta.size() == tb.size());
    for (size_t i = 0; i < ta.size() && i < tb.size(); ++i) {
        CHECK(ta[i].name == tb[i].name && ta[i].type == tb[i].type && ta[i].regions.size() == tb[i].regions.size());
        CHECK(ta[i].color.r == tb[i].color.r && ta[i].color.b == tb[i].color.b);
        CHECK(std::fabs(ta[i].pan - tb[i].pan) < 1e-6f && ta[i].inserts.size() == tb[i].inserts.size());
        for (int k = 0; k < 4; ++k) CHECK(ta[i].autoOf(AutoParam(k)).size() == tb[i].autoOf(AutoParam(k)).size());
    }
    CHECK(tb[0].regions[0].notes.size() == 3 && tb[0].regions[0].notes[2].pitch == 67 && tb[0].regions[0].notes[2].length == 2);
    CHECK(tb[0].panAuto.size() == 2 && tb[0].panAuto[1].value == 1.f);
    CHECK(tb[2].steps[0][0] && tb[2].steps[2][4] && !tb[2].steps[1][1]);
    CHECK(tb[1].regions[0].drummer);
    const auto marks = b.markers();
    CHECK(marks.size() == 2 && marks[0].name == "Intro" && marks[1].beat == 8);
    CHECK(b.loopOn() && b.loopA() == 4 && b.loopB() == 12 && b.metronome());
    CHECK(std::fabs(b.masterVolume() - 0.7f) < 1e-6f && std::fabs(b.bpm() - 100.0) < 1e-9);
    CHECK(tb[3].regions.size() == 1 && tb[3].regions[0].audio && tb[3].regions[0].frames == 44100 * 3);   // the decoder attached the audio
    // the saved project renders the same
    const auto ra = a.renderOffline(0, 44100 * 2), rb = b.renderOffline(0, 44100 * 2);
    float maxd = 0; for (size_t i = 0; i < ra.size(); ++i) maxd = std::max(maxd, std::fabs(ra[i] - rb[i]));
    CHECK(maxd < 2e-3f);
    CHECK(!b.loadProject(tmpDir() + "/missing.mdaw", &err));
    std::string bad = tmpDir() + "/bad.mdaw"; writeFile(bad, "{\"format\":\"nope\"}");
    CHECK(!b.loadProject(bad, &err) && !err.empty());
}

static void testUndo() {
    S s;
    int t = s.addTrack(TrackType::Instrument, "A");
    s.addMidiRegion(t, 0, 4);
    s.addMarker(4, "m");
    CHECK(s.markers().size() == 1);
    s.undo(); CHECK(s.markers().empty());
    s.redo(); CHECK(s.markers().size() == 1);
    s.undo(); s.undo(); CHECK(s.snapshot()[0].regions.empty());
    s.redo(); CHECK(s.snapshot()[0].regions.size() == 1);
    CHECK(s.canUndo());
}

static void testClipboardAndDuplicate() {
    S s;
    int t = s.addTrack(TrackType::Instrument, "Keys");
    int r = s.addMidiRegion(t, 0, 4);
    s.setRegionNotes(t, r, {{60, 0, 1, 100}});
    s.addInsert(t, "echo");
    CHECK(s.copyRegion(t, r) && s.hasClipboard());
    int at = s.addTrack(TrackType::Audio, "Aud");
    CHECK(s.pasteRegion(at, 4) == -1);                       // MIDI cannot go to an audio track
    int p = s.pasteRegion(t, 8);
    CHECK(p == 1 && s.snapshot()[0].regions[1].start == 8 && s.snapshot()[0].regions[1].notes.size() == 1);
    int d = s.duplicateTrack(t);
    const auto tr = s.snapshot();
    CHECK(d == 1 && tr[1].name == "Keys copy" && tr[1].instrument != tr[0].instrument && tr[1].inserts.size() == 1 && tr[1].inserts[0] != tr[0].inserts[0]);
    s.moveTrack(0, 2); CHECK(s.snapshot()[2].name == "Keys");
    s.setRegionMute(2, 0, true); CHECK(s.snapshot()[2].regions[0].mute);
}

static void testAutosave() {
    const std::string dir = tmpDir() + "/auto";
    removeTree(dir);
    S a;
    a.setAutosaveDir(dir);
    int t = a.addTrack(TrackType::Audio, "Vox");
    a.setArmed(t, true);
    CHECK(a.startRecording());
    std::vector<float> take(44100 * 2); for (size_t i = 0; i < take.size() / 2; ++i) take[2*i] = take[2*i+1] = 0.4f * std::sin(float(i) * 0.05f);
    a.pushRecorded(take);
    a.stopRecording();
    CHECK(a.snapshot()[0].regions.size() == 1);
    CHECK(!a.hasAutosave());
    std::string err;
    CHECK(a.autosave(&err) && a.hasAutosave());
    CHECK(a.isDirty());                                       // an autosave does not clean the project
    CHECK(a.projectPath().empty());
    CHECK(a.autosave(&err));                                  // second autosave reuses the recording file
    int files = 0; for (auto& e : fs::directory_iterator(toPath(dir + "/audio"))) { (void)e; ++files; }
    CHECK(files == 1);

    S b;                                                // "after the crash"
    b.setAutosaveDir(dir);
    CHECK(b.hasAutosave());
    CHECK(b.loadProject(b.autosavePath(), &err));
    b.markRecovered();
    CHECK(b.isDirty() && b.projectPath().empty());
    const auto tb = b.snapshot();
    CHECK(tb.size() == 1 && tb[0].regions.size() == 1 && tb[0].regions[0].audio);
    CHECK(tb[0].regions[0].frames == 44100);
    // saving the recovered project moves the audio next to it and clears the autosave
    makeDirs(tmpDir() + "/saved");
    CHECK(b.saveProject(tmpDir() + "/saved/rec.mdaw", &err));
    CHECK(!b.hasAutosave());
    CHECK(!fileExists(dir + "/audio/rec_1.wav"));
    S c;
    CHECK(c.loadProject(tmpDir() + "/saved/rec.mdaw", &err));
    CHECK(c.snapshot()[0].regions[0].audio && c.snapshot()[0].regions[0].frames == tb[0].regions[0].frames);
}

static std::uint32_t le32(const std::string& s, size_t o) { return std::uint8_t(s[o]) | std::uint8_t(s[o+1]) << 8 | std::uint8_t(s[o+2]) << 16 | std::uint32_t(std::uint8_t(s[o+3])) << 24; }

static void testExport() {
    S s;
    int t = s.addTrack(TrackType::Instrument, "Keys");
    int r = s.addMidiRegion(t, 0, 4);
    s.setRegionNotes(t, r, {{60, 0, 2, 100}, {67, 2, 2, 100}});
    s.setBpm(120);                                            // 4 beats = 2 s
    std::string err;
    const std::string f16 = tmpDir() + "/e16.wav", f24 = tmpDir() + "/e24.wav", ff = tmpDir() + "/ef.wav", fc = tmpDir() + "/ec.wav";
    ExportOptions o; o.tail = false;
    CHECK(s.exportAudio(f16, o, &err));
    std::string b; readFile(f16, b);
    CHECK(le32(b, 40) == 88200u * 4);
    CHECK(b.size() == 44 + 88200 * 4);
    o.format = dsp::WavFormat::Pcm24; CHECK(s.exportAudio(f24, o, &err)); readFile(f24, b); CHECK(b.size() == 44 + 88200 * 6);
    o.format = dsp::WavFormat::Float32; CHECK(s.exportAudio(ff, o, &err)); readFile(ff, b); CHECK(b.size() == 56 + 88200 * 8);
    o.format = dsp::WavFormat::Pcm16; o.tail = true; CHECK(s.exportAudio(f16, o, &err)); readFile(f16, b); CHECK(b.size() == 44 + (88200 + 88200) * 4);
    o.tail = false; o.normalize = true; o.normalizeDb = -6.f; CHECK(s.exportAudio(f16, o, &err));
    std::vector<float> pcm; CHECK(readWavFile(f16, pcm, &err)); CHECK(std::fabs(peak(pcm) - 0.5012f) < 0.002f);   // -6 dBFS
    ExportOptions cyc; cyc.cycleOnly = true; cyc.tail = false; s.setLoop(true, 2, 4);
    CHECK(s.exportAudio(fc, cyc, &err)); readFile(fc, b); CHECK(b.size() == 44 + 44100 * 4);                      // 2 beats at 120 bpm = 1 s
    S empty; CHECK(!empty.exportAudio(f16, ExportOptions{}, &err) && !err.empty());
    CHECK(!s.exportAudio("/nonexistent_dir/x.wav", ExportOptions{}, &err));
}

static void testBounce() {
    S s;
    int t = s.addTrack(TrackType::Instrument, "Lead");
    int r = s.addMidiRegion(t, 0, 4);
    s.setRegionNotes(t, r, {{64, 0, 1, 110}, {71, 1, 1, 100}, {76, 2, 2, 90}});
    s.addInsert(t, "echo");
    s.setPan(t, 0.5f); s.setVolume(t, 0.6f);
    const i64 len = 44100 * 6;
    const auto before = s.renderOffline(0, len);
    CHECK(peak(before) > 0.01f);
    const int nt = s.bounceTrackInPlace(t);
    CHECK(nt == 1 && s.trackCount() == 2);
    const auto tr = s.snapshot();
    CHECK(tr[0].mute && !tr[1].mute && tr[1].type == TrackType::Audio && tr[1].name == "Lead (Bounce)" && tr[1].regions.size() == 1);
    CHECK(tr[1].regions[0].frames > 44100 * 2);
    const auto after = s.renderOffline(0, len);
    float maxd = 0; for (size_t i = 0; i < before.size(); ++i) maxd = std::max(maxd, std::fabs(before[i] - after[i]));
    std::printf("bounce max difference %.6f (peak %.3f)\n", maxd, peak(before));
    CHECK(maxd < 0.005f);
    s.undo(); CHECK(s.trackCount() == 1 && !s.snapshot()[0].mute);
    S e; CHECK(e.bounceTrackInPlace(0) == -1);
    int silent = e.addTrack(TrackType::Instrument, "S"); e.addMidiRegion(silent, 0, 4); CHECK(e.bounceTrackInPlace(silent) == -1);   // no notes: silent
}

static void testAutomationPan() {
    const std::string wav = makeWav("tone.wav", 220, 44100 * 4, 0.4f);
    S s;
    s.importAudio(wav, -1, 0);
    s.setBpm(120);                                            // 8 beats = 4 s
    s.setAuto(0, AutoParam::Pan, {{0, -1.f}, {8, 1.f}});
    const auto out = s.renderOffline(0, 44100 * 4);
    auto rms = [&](size_t a, size_t b, int ch) { double e = 0; size_t n = 0; for (size_t i = a; i < b; ++i, ++n) e += double(out[2*i+ch]) * out[2*i+ch]; return std::sqrt(e / double(n)); };
    CHECK(rms(0, 4410, 0) > 5 * rms(0, 4410, 1));            // starts hard left
    CHECK(rms(44100 * 4 - 4410, 44100 * 4, 1) > 5 * rms(44100 * 4 - 4410, 44100 * 4, 0));   // ends hard right
    const double mid = rms(44100 * 2 - 2205, 44100 * 2 + 2205, 0) / rms(44100 * 2 - 2205, 44100 * 2 + 2205, 1);
    CHECK(mid > 0.8 && mid < 1.25);                           // centre in the middle
    s.setAuto(0, AutoParam::Pan, {{0, 5.f}});                 // out-of-range values are clamped
    CHECK(s.snapshot()[0].panAuto[0].value == 1.f);
}

static void testMidiOps() {
    // Arpeggiator
    std::vector<Note> chord = {{64, 0, 1, 90}, {60, 0, 1, 100}, {67, 0, 1, 80}};
    ArpParams p; p.rate = 0.25; p.gate = 0.5;
    auto up = arpeggiate(chord, 4, p);
    CHECK(up.size() == 4 && up[0].pitch == 60 && up[1].pitch == 64 && up[2].pitch == 67 && up[3].pitch == 60);
    CHECK(up[1].start == 0.25 && up[3].start == 0.75 && std::fabs(up[0].length - 0.125) < 1e-9 && up[1].velocity == 90);
    p.pattern = ArpParams::Down; auto dn = arpeggiate(chord, 4, p); CHECK(dn[0].pitch == 67 && dn[1].pitch == 64 && dn[2].pitch == 60 && dn[3].pitch == 67);
    p.pattern = ArpParams::UpDown; auto ud = arpeggiate(chord, 4, p); CHECK(ud.size() == 4 && ud[0].pitch == 60 && ud[1].pitch == 64 && ud[2].pitch == 67 && ud[3].pitch == 64);
    p.pattern = ArpParams::Up; p.octaves = 2; p.rate = 0.125; auto oc = arpeggiate(chord, 4, p);
    CHECK(oc.size() == 8 && oc[3].pitch == 72 && oc[5].pitch == 79);
    p.pattern = ArpParams::AsPlayed; p.octaves = 1; p.rate = 0.25; auto ap = arpeggiate(chord, 4, p); CHECK(ap[0].pitch == 64 && ap[1].pitch == 60);
    p.pattern = ArpParams::Random; p.seed = 3; auto r1 = arpeggiate(chord, 4, p), r2 = arpeggiate(chord, 4, p);
    CHECK(r1.size() == 4); bool same = true; for (size_t i = 0; i < r1.size(); ++i) same &= r1[i].pitch == r2[i].pitch; CHECK(same);   // seeded
    for (size_t i = 1; i < r1.size(); ++i) CHECK(r1[i].pitch != r1[i-1].pitch);
    // two chords, a single note repeats, notes past the region end are dropped
    std::vector<Note> two = {{60, 0, 1, 100}, {62, 2, 0.5, 100}};
    p = ArpParams{}; auto t2 = arpeggiate(two, 2.25, p);
    CHECK(t2.size() == 4 + 1 && t2.back().pitch == 62 && t2.back().start == 2.0 && std::fabs(t2.back().length - 0.2) < 1e-9);
    CHECK(arpeggiate({}, 4, p).empty());
    // Quantize
    std::vector<Note> loose = {{60, 0.02, 0.2, 100}, {60, 0.27, 0.3, 100}, {60, 0.49, 0.2, 100}, {60, 0.76, 0.2, 100}};
    QuantizeParams q; q.grid = 0.25;
    auto q1 = quantizeNotes(loose, q);
    CHECK(q1[0].start == 0 && q1[1].start == 0.25 && q1[2].start == 0.5 && q1[3].start == 0.75);
    q.strength = 0.5; auto q2 = quantizeNotes(loose, q); CHECK(std::fabs(q2[0].start - 0.01) < 1e-9 && std::fabs(q2[1].start - 0.26) < 1e-9);
    q.strength = 1; q.swing = 2.0 / 3.0; q.grid = 0.5;
    std::vector<Note> eights = {{60, 0, 0.5, 100}, {60, 0.5, 0.5, 100}, {60, 1, 0.5, 100}, {60, 1.5, 0.5, 100}};
    auto sw = quantizeNotes(eights, q);
    CHECK(sw[0].start == 0 && std::fabs(sw[1].start - 2.0 / 3.0) < 1e-9 && sw[2].start == 1 && std::fabs(sw[3].start - (1 + 2.0 / 3.0)) < 1e-9);   // triplet feel
    q.swing = 0.5; q.grid = 0.25; q.lengths = true; auto ql = quantizeNotes(loose, q); CHECK(ql[0].length == 0.25 && ql[1].length == 0.25);
    q.swing = 9.0; auto cl = quantizeNotes(eights, q); (void)cl;   // swing is clamped, no crash
    std::vector<Note> neg = {{60, 0.01, 0.2, 100}}; QuantizeParams qn; qn.grid = 1; CHECK(quantizeNotes(neg, qn)[0].start == 0);
}

static void testSessionRegions() {
    S s;
    int t = s.addTrack(TrackType::Instrument, "A");
    int r = s.addMidiRegion(t, 0, 4);
    s.setRegionNotes(t, r, {{60, 0.01, 0.5, 100}, {64, 0.02, 0.5, 100}, {67, 0.0, 0.5, 100}});
    CHECK(s.quantizeRegion(t, r, QuantizeParams{}));
    CHECK(s.snapshot()[0].regions[0].notes[0].start == 0);
    CHECK(s.arpeggiateRegion(t, r, ArpParams{}) && s.snapshot()[0].regions[0].notes.size() == 2);   // one chord of 0.5 beats: two 1/16 steps
    s.undo(); s.undo();
    CHECK(s.snapshot()[0].regions[0].notes[0].start == 0.01);
    int a = s.addTrack(TrackType::Audio, "Au");
    CHECK(!s.quantizeRegion(a, 0, QuantizeParams{}) && !s.arpeggiateRegion(99, 0, ArpParams{}));
    s.splitRegion(t, r, 2); CHECK(s.snapshot()[0].regions.size() == 2);
    s.removeRegion(t, 1); CHECK(s.snapshot()[0].regions.size() == 1);
}

static void testMidiOps2() {
    // Scale quantize
    std::vector<Note> chroma; for (int p = 60; p < 72; ++p) chroma.push_back({p, 0, 1, 100});
    ScaleParams major;
    auto q = scaleQuantize(chroma, major);
    const int expect[12] = {60, 60, 62, 62, 64, 65, 65, 67, 67, 69, 69, 71};   // ties go down
    for (int i = 0; i < 12; ++i) CHECK(q[size_t(i)].pitch == expect[i]);
    ScaleParams aminor; aminor.root = 9; aminor.scale = ScaleParams::Minor;
    CHECK(scaleQuantize({{70, 0, 1, 100}}, aminor)[0].pitch == 69 && scaleQuantize({{69, 0, 1, 100}}, aminor)[0].pitch == 69 && scaleQuantize({{71, 0, 1, 100}}, aminor)[0].pitch == 71);
    ScaleParams pent; pent.scale = ScaleParams::PentatonicMajor;
    CHECK(scaleQuantize({{71, 0, 1, 100}}, pent)[0].pitch == 72 && scaleQuantize({{65, 0, 1, 100}}, pent)[0].pitch == 64);   // B -> C above, F -> E
    ScaleParams chrom; chrom.scale = ScaleParams::Chromatic;
    auto same = scaleQuantize(chroma, chrom); bool eq = true; for (size_t i = 0; i < same.size(); ++i) eq &= same[i].pitch == chroma[i].pitch; CHECK(eq);
    CHECK(scaleQuantize({{0, 0, 1, 100}, {127, 0, 1, 100}}, major)[0].pitch == 0 && scaleQuantize({{127, 0, 1, 100}}, major)[0].pitch == 127);   // stays in range
    for (int sc = 0; sc < ScaleParams::Count; ++sc) { ScaleParams sp; sp.scale = sc; sp.root = 7; for (const Note& n : scaleQuantize(chroma, sp)) CHECK(n.pitch >= 0 && n.pitch <= 127); }
    // Humanize
    std::vector<Note> grid; for (int i = 0; i < 64; ++i) grid.push_back({60, i * 0.25, 0.2, 100});
    HumanizeParams h; h.timing = 0.03; h.velocity = 10; h.seed = 5;
    auto h1 = humanize(grid, h), h2 = humanize(grid, h);
    bool deterministic = true, bounded = true, moved = false, vmoved = false;
    for (size_t i = 0; i < h1.size(); ++i) {
        deterministic &= h1[i].start == h2[i].start && h1[i].velocity == h2[i].velocity;
        bounded &= std::fabs(h1[i].start - double(i) * 0.25) <= 0.03 + 1e-9 && h1[i].velocity >= 90 && h1[i].velocity <= 110 && h1[i].start >= 0;
        moved |= h1[i].start != double(i) * 0.25; vmoved |= h1[i].velocity != 100;
    }
    CHECK(deterministic && bounded && moved && vmoved);
    for (size_t i = 1; i < h1.size(); ++i) CHECK(h1[i].start >= h1[i-1].start);
    h.seed = 6; auto h3 = humanize(grid, h); bool differs = false; for (size_t i = 0; i < h1.size(); ++i) differs |= h1[i].start != h3[i].start; CHECK(differs);
    HumanizeParams none; none.timing = 0; none.velocity = 0; auto n0 = humanize(grid, none); bool unchanged = true; for (size_t i = 0; i < n0.size(); ++i) unchanged &= n0[i].start == grid[i].start && n0[i].velocity == 100; CHECK(unchanged);
    CHECK(humanize({{60, 0, 1, 127}, {60, 0, 1, 1}}, HumanizeParams{0.0, 50, 2})[0].velocity <= 127);
    for (const Note& n : humanize({{60, 0.0, 1, 3}}, HumanizeParams{0.5, 100, 9})) CHECK(n.start >= 0 && n.velocity >= 1 && n.velocity <= 127);
    // Chord trigger
    auto tri = chordTrigger({{60, 1, 0.5, 90}}, ChordParams{});
    CHECK(tri.size() == 3 && tri[0].pitch == 60 && tri[1].pitch == 64 && tri[2].pitch == 67 && tri[1].start == 1 && tri[2].velocity == 90 && tri[1].length == 0.5);
    ChordParams m7; m7.type = ChordParams::Minor7; auto c7 = chordTrigger({{60, 0, 1, 100}}, m7);
    CHECK(c7.size() == 4 && c7[1].pitch == 63 && c7[3].pitch == 70);
    ChordParams inv; inv.inversion = 1; auto i1 = chordTrigger({{60, 0, 1, 100}}, inv); CHECK(i1[0].pitch == 64 && i1[1].pitch == 67 && i1[2].pitch == 72);
    inv.inversion = 2; auto i2 = chordTrigger({{60, 0, 1, 100}}, inv); CHECK(i2[0].pitch == 67 && i2[1].pitch == 72 && i2[2].pitch == 76);
    inv.inversion = 9; CHECK(chordTrigger({{60, 0, 1, 100}}, inv).size() == 3);                 // inversion is limited
    ChordParams oct; oct.type = ChordParams::Octave; CHECK(chordTrigger({{125, 0, 1, 100}}, oct).size() == 1);   // 137 is dropped
    CHECK(chordTrigger({}, ChordParams{}).empty());
    for (int t = 0; t < ChordParams::Count; ++t) { auto iv = chordIntervals(t); CHECK(iv.size() >= 2 && iv[0] == 0); }
    auto two = chordTrigger({{60, 2, 1, 100}, {62, 0, 1, 100}}, ChordParams{}); CHECK(two.size() == 6 && two[0].pitch == 62);   // sorted by start
}

static void testSessionTransforms() {
    S s;
    int t = s.addTrack(TrackType::Instrument, "A");
    int r = s.addMidiRegion(t, 0, 4);
    s.setRegionNotes(t, r, {{61, 0.0, 1, 100}, {63, 1.0, 1, 100}});
    CHECK(s.scaleQuantizeRegion(t, r, ScaleParams{}));
    CHECK(s.snapshot()[0].regions[0].notes[0].pitch == 60 && s.snapshot()[0].regions[0].notes[1].pitch == 62);
    CHECK(s.chordTriggerRegion(t, r, ChordParams{}) && s.snapshot()[0].regions[0].notes.size() == 6);
    CHECK(s.humanizeRegion(t, r, HumanizeParams{}) && s.snapshot()[0].regions[0].notes.size() == 6);
    s.undo(); s.undo(); s.undo();
    CHECK(s.snapshot()[0].regions[0].notes[0].pitch == 61 && s.snapshot()[0].regions[0].notes.size() == 2);   // one undo step each
    int a = s.addTrack(TrackType::Audio, "Au");
    CHECK(!s.scaleQuantizeRegion(a, 0, ScaleParams{}) && !s.humanizeRegion(5, 5, HumanizeParams{}) && !s.chordTriggerRegion(-1, 0, ChordParams{}));
    // a drummer region becomes hand-edited
    int d = s.addTrack(TrackType::Drummer, "Dr"); int dr = s.addDrummerRegion(d, 0, 2);
    CHECK(s.snapshot()[d].regions[size_t(dr)].drummer);
    CHECK(s.humanizeRegion(d, dr, HumanizeParams{}) && !s.snapshot()[d].regions[size_t(dr)].drummer);
}

// a click track: decaying noise bursts on every beat
static std::vector<float> clickTrack(double bpm, double seconds, bool accents = false) {
    const size_t n = size_t(seconds * kSampleRate);
    std::vector<float> pcm(n * 2, 0.f);
    std::uint32_t seed = 7;
    const double period = 60.0 / bpm * kSampleRate;
    for (int k = 0; k * period < double(n); ++k) {
        const size_t at = size_t(k * period);
        const float amp = (accents && k % 4 == 0) ? 0.9f : 0.5f;
        for (size_t i = 0; i < 700 && at + i < n; ++i) {
            seed = seed * 1664525u + 1013904223u;
            const float v = amp * (float(seed >> 8) / 8388608.f - 1.f) * std::exp(-float(i) / 150.f);
            pcm[2 * (at + i)] = pcm[2 * (at + i) + 1] = v;
        }
    }
    return pcm;
}

static void testMidiOps3() {
    // Legato
    std::vector<Note> n = {{60, 0, 0.25, 100}, {64, 1, 0.25, 100}, {67, 1, 0.25, 100}, {62, 2.5, 3.0, 100}};
    auto l = legato(n, 4);
    CHECK(l[0].length == 1 && l[1].length == 1.5 && l[2].length == 1.5 && l[3].length == 1.5);   // the chord ends at the next start, the last at the region end
    std::vector<Note> over = {{60, 0, 3, 100}, {62, 1, 0.5, 100}};
    CHECK(legato(over, 4)[0].length == 1);                                                     // overlapping notes are shortened
    CHECK(legato({}, 4).empty());
    CHECK(legato({{60, 3.99, 0.1, 100}}, 4)[0].length > 0);                                    // never zero or negative
    // Transpose
    auto up = transposeNotes({{60, 0, 1, 100}, {126, 0, 1, 100}}, 12); CHECK(up[0].pitch == 72 && up[1].pitch == 127);
    CHECK(transposeNotes({{5, 0, 1, 100}}, -12)[0].pitch == 0 && transposeNotes({{60, 0, 1, 100}}, 0)[0].pitch == 60);
    // Velocity ramp
    std::vector<Note> v = {{60, 0, 1, 90}, {60, 2, 1, 90}, {60, 4, 1, 90}, {60, 5, 1, 90}};
    auto cr = velocityRamp(v, 4, 20, 120); CHECK(cr[0].velocity == 20 && cr[1].velocity == 70 && cr[2].velocity == 120 && cr[3].velocity == 120);
    auto de = velocityRamp(v, 4, 120, 20); CHECK(de[0].velocity == 120 && de[1].velocity == 70);
    CHECK(velocityRamp({{60, 0, 1, 90}}, 4, 0, 0)[0].velocity == 1 && velocityRamp({{60, 0, 1, 90}}, 4, 500, 500)[0].velocity == 127);
    CHECK(velocityRamp({{60, 1, 1, 90}}, 0, 20, 100)[0].velocity == 100);                      // a zero-length region does not divide by zero
}

static void testTempoDetect() {
    for (double bpm : {85.0, 100.0, 120.0, 128.0, 140.0, 160.0}) {
        const auto c = clickTrack(bpm, 12);
        const double d = dsp::detectTempo(c.data(), c.size() / 2);
        std::printf("tempo %.0f -> %.2f\n", bpm, d);
        CHECK(std::fabs(d - bpm) < bpm * 0.012);
    }
    const auto acc = clickTrack(110, 16, true); CHECK(std::fabs(dsp::detectTempo(acc.data(), acc.size() / 2) - 110) < 1.4);   // accents on the downbeat
    const auto slow = clickTrack(60, 14);                                    // 60 bpm is folded into the 70..170 range
    CHECK(std::fabs(dsp::detectTempo(slow.data(), slow.size() / 2) - 120) < 2.0);
    std::vector<float> silence(44100 * 2 * 10, 0.f); CHECK(dsp::detectTempo(silence.data(), silence.size() / 2) == 0);
    const auto shortClip = clickTrack(120, 2); CHECK(dsp::detectTempo(shortClip.data(), shortClip.size() / 2) == 0);   // too short
    CHECK(dsp::detectTempo(nullptr, 100000) == 0);
    std::vector<float> noise(44100 * 2 * 8); std::uint32_t sd = 3; for (float& x : noise) { sd = sd * 1664525u + 1013904223u; x = float(sd >> 8) / 8388608.f - 1.f; }
    const double nz = dsp::detectTempo(noise.data(), noise.size() / 2); CHECK(nz == 0 || (nz >= 70 && nz < 170));   // steady noise: no crash, in range
    // through the Session: a drum machine pattern at 100 bpm
    S s; s.setBpm(100);
    int dm = s.addTrack(TrackType::Drums, "DM");
    for (int st : {0, 8}) s.setStep(dm, 0, st, true);
    for (int st : {4, 12}) s.setStep(dm, 1, st, true);
    for (int st = 0; st < 16; st += 2) s.setStep(dm, 2, st, true);
    const auto drums = s.renderOffline(0, 44100 * 17);
    const double dd = dsp::detectTempo(drums.data(), drums.size() / 2);
    std::printf("drum loop at 100 -> %.2f\n", dd);
    CHECK(std::fabs(dd - 100) < 3.0);
    // a region of the clicks
    const std::string wav = tmpDir() + "/clicks.wav"; std::string err; CHECK(writeWavFile(wav, clickTrack(132, 10), &err));
    S a; a.importAudio(wav, -1, 0);
    CHECK(std::fabs(a.detectRegionTempo(0, 0) - 132) < 2.0 && a.detectRegionTempo(0, 5) == 0 && a.detectRegionTempo(9, 0) == 0);
    int mt = a.addTrack(TrackType::Instrument, "M"); a.addMidiRegion(mt, 0, 4); CHECK(a.detectRegionTempo(mt, 0) == 0);   // MIDI has no audio
}

static void testRegionCommands() {
    // Repeat
    S s;
    int t = s.addTrack(TrackType::Instrument, "K");
    int r = s.addMidiRegion(t, 4, 4);
    s.setRegionNotes(t, r, {{60, 0, 1, 100}});
    CHECK(s.repeatRegion(t, r, 3) == 3);
    auto regs = s.snapshot()[0].regions;
    CHECK(regs.size() == 4 && regs[1].start == 8 && regs[2].start == 12 && regs[3].start == 16 && regs[3].notes.size() == 1);
    s.undo(); CHECK(s.snapshot()[0].regions.size() == 1);                          // one undo step
    CHECK(s.repeatRegion(t, r, 500) == 64 && s.snapshot()[0].regions.size() == 65);
    CHECK(s.repeatRegion(9, 0, 2) == 0 && s.repeatRegion(t, 99, 2) == 0);
    // Join MIDI
    S j;
    int jt = j.addTrack(TrackType::Instrument, "J");
    int a = j.addMidiRegion(jt, 0, 4); j.setRegionNotes(jt, a, {{60, 0, 1, 100}, {62, 2, 1, 100}});
    int b = j.addMidiRegion(jt, 4, 2); j.setRegionNotes(jt, b, {{64, 0.5, 1, 90}});
    CHECK(j.joinRegions(jt, a));
    auto jr = j.snapshot()[0].regions;
    CHECK(jr.size() == 1 && jr[0].lengthBeats == 6 && jr[0].notes.size() == 3 && jr[0].notes[2].pitch == 64 && jr[0].notes[2].start == 4.5);
    CHECK(!j.joinRegions(jt, 0));                                                    // nothing after it
    j.undo(); CHECK(j.snapshot()[0].regions.size() == 2);
    // Join audio: split and join again gives the original region
    const std::string wav = makeWav("join.wav", 300, 44100 * 4);
    S au; au.setBpm(120); au.importAudio(wav, -1, 0);                                // 4 s = 8 beats
    const Region orig = au.snapshot()[0].regions[0];
    au.splitRegion(0, 0, 3);
    CHECK(au.snapshot()[0].regions.size() == 2);
    int first = au.snapshot()[0].regions[0].start < au.snapshot()[0].regions[1].start ? 0 : 1;
    CHECK(au.joinRegions(0, first));
    const auto back = au.snapshot()[0].regions;
    CHECK(back.size() == 1 && back[0].srcFrames == orig.srcFrames && back[0].frames == orig.frames && back[0].start == orig.start);
    // two different recordings cannot be joined, nor can regions with a gap
    au.importAudio(makeWav("other.wav", 500, 44100), 0, 8);
    CHECK(!au.joinRegions(0, 0));
    S gap; gap.setBpm(120); gap.importAudio(wav, -1, 0); gap.splitRegion(0, 0, 3);
    int g0 = gap.snapshot()[0].regions[0].start < gap.snapshot()[0].regions[1].start ? 0 : 1;
    gap.moveRegion(0, 1 - g0, 20);
    CHECK(!gap.joinRegions(0, g0));
    S mixed; int mt = mixed.addTrack(TrackType::Instrument, "x"); mixed.addMidiRegion(mt, 0, 4); mixed.addMidiRegion(mt, 4, 4);
    CHECK(mixed.joinRegions(mt, 0));
}

static void testConvertPattern() {
    S s;
    int dm = s.addTrack(TrackType::Drums, "Beat");
    CHECK(s.convertPatternToMidi(dm, 4) == -1);                                      // empty pattern
    s.setStep(dm, 0, 0, true); s.setStep(dm, 0, 8, true); s.setStep(dm, 1, 4, true); s.setStep(dm, 2, 2, true); s.setStep(dm, 3, 12, true);
    const int nt = s.convertPatternToMidi(dm, 2);
    CHECK(nt == 1 && s.trackCount() == 2);
    const auto tr = s.snapshot();
    CHECK(tr[1].type == TrackType::Drummer && tr[1].name == "Beat (MIDI)" && tr[1].regions.size() == 1 && tr[1].regions[0].midi);
    const Region& r = tr[1].regions[0];
    CHECK(r.lengthBeats == 8 && r.notes.size() == 10);                               // 5 hits x 2 bars
    CHECK(r.notes[0].pitch == 36 && r.notes[0].start == 0);
    bool snare = false, hat = false, clap = false;
    for (const Note& n : r.notes) { snare |= n.pitch == 38 && n.start == 1.0; hat |= n.pitch == 42 && n.start == 4.5 + 0.0 * 1; clap |= n.pitch == 39 && n.start == 3.0; }
    CHECK(snare && hat && clap);                                                     // hat at step 2 = beat 0.5 (bar 1) / 4.5 (bar 2)
    bool hatOk = false; for (const Note& n : r.notes) hatOk |= n.pitch == 42 && n.start == 0.5; CHECK(hatOk);
    CHECK(s.convertPatternToMidi(1, 4) == -1 && s.convertPatternToMidi(9, 4) == -1);   // only drum machine tracks
    // the MIDI version plays the same hits: the two tracks render with transients at the same places
    s.setMute(1, true); const auto a = s.renderOffline(0, 44100 * 4); s.setMute(1, false); s.setMute(0, true); const auto b = s.renderOffline(0, 44100 * 4);
    auto onsets = [](const std::vector<float>& x) { std::vector<int> o; float prev = 0; for (size_t i = 0; i < x.size() / 2; i += 256) { float e = 0; for (size_t k = 0; k < 256 && i + k < x.size() / 2; ++k) e = std::max(e, std::fabs(x[2*(i+k)])); if (e > 0.05f && prev < 0.02f) o.push_back(int(i / 256)); prev = e; } return o; };
    const auto oa = onsets(a), ob = onsets(b);
    std::printf("pattern onsets: %zu vs %zu\n", oa.size(), ob.size());
    CHECK(!oa.empty() && oa.size() == ob.size());
    for (size_t i = 0; i < oa.size() && i < ob.size(); ++i) CHECK(std::abs(oa[i] - ob[i]) <= 1);
}

static void testSessionNewTransforms() {
    S s;
    int t = s.addTrack(TrackType::Instrument, "A");
    int r = s.addMidiRegion(t, 0, 4);
    s.setRegionNotes(t, r, {{60, 0, 0.25, 90}, {64, 2, 0.25, 90}});
    CHECK(s.legatoRegion(t, r) && s.snapshot()[0].regions[0].notes[0].length == 2 && s.snapshot()[0].regions[0].notes[1].length == 2);
    CHECK(s.transposeRegion(t, r, 7) && s.snapshot()[0].regions[0].notes[0].pitch == 67);
    CHECK(s.velocityRampRegion(t, r, 10, 110) && s.snapshot()[0].regions[0].notes[1].velocity == 60);
    s.undo(); s.undo(); s.undo(); CHECK(s.snapshot()[0].regions[0].notes[0].pitch == 60 && s.snapshot()[0].regions[0].notes[0].length == 0.25);
    int a = s.addTrack(TrackType::Audio, "au"); CHECK(!s.legatoRegion(a, 0) && !s.transposeRegion(99, 0, 1));
}

static std::vector<float> stereoSine(double seconds, float amp, double hz = 1000.0) {
    std::vector<float> pcm(size_t(seconds * 44100) * 2);
    for (size_t i = 0; i < pcm.size() / 2; ++i) pcm[2 * i] = pcm[2 * i + 1] = amp * std::sin(2 * 3.14159265358979 * hz * double(i) / 44100.0);
    return pcm;
}

static void testV09() {
    // ---- pure MIDI operations ----
    std::vector<Note> ov = {{60, 0, 2, 100}, {60, 1, 2, 90}, {64, 0.5, 3, 80}};
    auto fo = fixOverlaps(ov);
    CHECK(fo.size() == 3 && std::fabs(fo[0].length - 1) < 1e-9 && std::fabs(fo[2].length - 2) < 1e-9);      // the C ends where the next C starts; the E and the last C are untouched
    CHECK(fixOverlaps({{60, 0, 2, 100}, {62, 1, 1, 100}})[0].length == 2);                                    // other pitches do not matter
    auto dd = removeDuplicateNotes({{60, 1, 0.5, 100}, {60, 1.005, 1, 90}, {64, 1, 0.5, 100}, {60, 2, 0.5, 100}});
    CHECK(dd.size() == 3 && std::fabs(dd[0].length - 1) < 1e-9);                                              // the longer one of the pair survives
    auto fl = fixedLength({{60, 0, 0.1, 100}, {62, 3.8, 0.1, 100}}, 4, 1);
    CHECK(fl[0].length == 1 && std::fabs(fl[1].length - 0.2) < 1e-9);                                         // cut off at the region end
    auto re = retrograde({{60, 0, 1, 100}, {62, 1, 1, 100}, {64, 3, 1, 100}}, 4);
    CHECK(re.size() == 3 && re[0].pitch == 64 && re[0].start == 0 && re[1].pitch == 62 && re[1].start == 2 && re[2].pitch == 60 && re[2].start == 3);
    auto iv = invertPitches({{60, 0, 1, 100}, {64, 1, 1, 100}, {72, 2, 1, 100}});
    CHECK(iv[0].pitch == 72 && iv[1].pitch == 68 && iv[2].pitch == 60);                                       // mirrored between 60 and 72
    CHECK(invertPitches({}).empty());
    auto sv = scaleVelocity({{60, 0, 1, 100}, {60, 1, 1, 10}}, 50, 10);
    CHECK(sv[0].velocity == 60 && sv[1].velocity == 15);
    CHECK(scaleVelocity({{60, 0, 1, 100}}, 400, 100)[0].velocity == 127 && scaleVelocity({{60, 0, 1, 5}}, 1, -100)[0].velocity == 1);

    // ---- loudness ----
    const auto full = stereoSine(5, 1.0f), quiet = stereoSine(5, 0.1f);
    const double lf = dsp::integratedLoudness(full.data(), full.size() / 2), lq = dsp::integratedLoudness(quiet.data(), quiet.size() / 2);
    std::printf("loudness full scale %.2f, -20 dB %.2f\n", lf, lq);
    CHECK(std::fabs(lf) < 0.6);                                   // a full-scale stereo 1 kHz sine is about 0 LUFS
    CHECK(std::fabs((lf - lq) - 20.0) < 0.05);                    // 20 dB lower
    std::vector<float> sil(44100 * 2 * 3, 0.f), tiny = stereoSine(3, 1e-5f);   // -100 dBFS: below the absolute gate
    CHECK(dsp::integratedLoudness(sil.data(), sil.size() / 2) == dsp::kSilentLufs && dsp::integratedLoudness(tiny.data(), tiny.size() / 2) == dsp::kSilentLufs);
    CHECK(dsp::integratedLoudness(full.data(), 1000) == dsp::kSilentLufs && dsp::integratedLoudness(nullptr, 100000) == dsp::kSilentLufs);
    // a quiet passage between loud ones does not drag the result down (relative gate)
    std::vector<float> mix = stereoSine(3, 1.0f); auto mq = stereoSine(3, 0.01f); auto m3 = stereoSine(3, 1.0f);
    mix.insert(mix.end(), mq.begin(), mq.end()); mix.insert(mix.end(), m3.begin(), m3.end());
    CHECK(std::fabs(dsp::integratedLoudness(mix.data(), mix.size() / 2) - lf) < 0.3);

    // ---- strip silence ----
    auto tone = stereoSine(1, 0.5f);
    std::vector<float> clip = tone; clip.resize(clip.size() + 44100 * 2, 0.f); clip.insert(clip.end(), tone.begin(), tone.end());   // 1 s tone, 1 s silence, 1 s tone
    auto rg = dsp::findSoundRanges(clip.data(), clip.size() / 2, -50, 4410, 441);
    CHECK(rg.size() == 2 && rg[0].first == 0 && std::llabs(i64(rg[0].second) - (44100 + 441)) < 300 && std::llabs(i64(rg[1].first) - (88200 - 441)) < 300 && rg[1].second == 132300);
    CHECK(dsp::findSoundRanges(clip.data(), clip.size() / 2, -50, 44100 * 3, 0).size() == 1);                    // gaps shorter than the minimum are kept
    CHECK(dsp::findSoundRanges(sil.data(), sil.size() / 2, -50, 100, 0).empty());
    S a; a.setBpm(120);
    const std::string wav = tmpDir() + "/strip.wav"; std::string err; CHECK(writeWavFile(wav, clip, &err));
    a.importAudio(wav, -1, 0);
    const int n = a.stripSilenceRegion(0, 0, -50, 100, 10);
    const auto tr = a.snapshot()[0].regions;
    CHECK(n == 2 && tr.size() == 2 && std::fabs(tr[0].start) < 1e-9 && std::fabs(tr[1].start - 3.98) < 0.02);    // 120 bpm: 2 beats per second
    CHECK(std::llabs(tr[0].frames + tr[1].frames - 88200) < 2200 && tr[1].offset > tr[0].offset + tr[0].frames);
    a.undo(); CHECK(a.snapshot()[0].regions.size() == 1);
    CHECK(a.stripSilenceRegion(0, 0, -50, 100, 10) == 2 && a.stripSilenceRegion(5, 0, -50, 100, 10) == 0);
    a.setRegionFlex(0, 0, true, 100, 0); CHECK(a.stripSilenceRegion(0, 0, -50, 100, 10) == 0);                    // stretched regions are refused
    // regions of a quiet file and MIDI
    S b; b.importAudio(wav, -1, 0);
    const double rl = b.regionLoudness(0, 0); CHECK(rl < -3 && rl > -20);
    int mt = b.addTrack(TrackType::Instrument, "M"); b.addMidiRegion(mt, 0, 4);
    CHECK(b.regionLoudness(mt, 0) == 1000 && b.regionLoudness(9, 0) == 1000);

    // ---- through the session ----
    S m; int t = m.addTrack(TrackType::Instrument, "K"); int r0 = m.addMidiRegion(t, 0, 4);
    m.setRegionNotes(t, r0, {{60, 0, 2, 100}, {60, 1, 2, 90}, {64, 3, 0.5, 100}});
    CHECK(m.fixOverlapsRegion(t, r0) && std::fabs(m.snapshot()[0].regions[0].notes[0].length - 1) < 1e-9);
    CHECK(m.fixedLengthRegion(t, r0, 0.5) && m.snapshot()[0].regions[0].notes[1].length == 0.5 && !m.fixedLengthRegion(t, r0, 0));
    CHECK(m.retrogradeRegion(t, r0) && m.invertRegion(t, r0) && m.scaleVelocityRegion(t, r0, 50, 0) && m.removeDuplicatesRegion(t, r0));
    CHECK(m.snapshot()[0].regions[0].notes.size() == 3 && m.snapshot()[0].regions[0].notes[0].velocity <= 50);
    CHECK(!b.fixOverlapsRegion(0, 0));                                                                           // audio regions are not MIDI
}


// ---- v0.12: Logic / Ableton / FL Studio / Cubase / Pro Tools features ----
static std::string makeClicks(const std::string& name, const std::vector<double>& seconds, double total) {
    std::vector<float> pcm(size_t(total * kSampleRate) * 2, 0.f);
    for (double sec : seconds) {   // a decaying 200 Hz thump of 120 ms
        const size_t s0 = size_t(sec * kSampleRate);
        for (size_t i = 0; i < 5292 && (s0 + i) * 2 + 1 < pcm.size(); ++i) {
            const float v = 0.8f * std::exp(-float(i) / 1200.f) * std::sin(2 * 3.14159265f * 200.f * float(i) / kSampleRate);
            pcm[(s0 + i) * 2] = pcm[(s0 + i) * 2 + 1] = v;
        }
    }
    const std::string p = tmpDir() + "/" + name;
    std::string err;
    CHECK(writeWavFile(p, pcm, &err));
    return p;
}

static void testAppPaths() {
    auto none = [](const std::string&) { return false; };
    auto marked = [](const std::string& f) { return f == "D:\\Music\\MiniDAW\\MiniDAW.portable"; };
    AppPaths a = resolveAppPaths("C:\\Program Files\\MiniDAW", "C:\\Users\\me\\AppData\\Local", none);
    CHECK(!a.portable && a.dataDir == "C:\\Users\\me\\AppData\\Local\\MiniDAW" && a.exeDir == "C:\\Program Files\\MiniDAW");
    a = resolveAppPaths("D:\\Music\\MiniDAW", "C:\\Users\\me\\AppData\\Local", marked);
    CHECK(a.portable && a.dataDir == "D:\\Music\\MiniDAW\\data");
    a = resolveAppPaths("E:\\x", "", none);                                   // no LOCALAPPDATA: fall back to the program folder
    CHECK(!a.portable && a.dataDir == "E:\\x\\MiniDAW");
    a = resolveAppPaths("/opt/md", "/home/me/.local", [](const std::string& f) { return f == "/opt/md/MiniDAW.portable"; }, '/');
    CHECK(a.portable && a.dataDir == "/opt/md/data");
    CHECK(std::string(kPortableMarker) == "MiniDAW.portable");
}

static void testV012() {
    // --- transient detection ---
    {
        std::vector<float> pcm(size_t(2.5 * kSampleRate) * 2, 0.f);
        for (double sec : {0.5, 1.0, 1.5, 2.0}) {
            const size_t s0 = size_t(sec * kSampleRate);
            for (size_t i = 0; i < 4410; ++i)
                pcm[(s0 + i) * 2] = pcm[(s0 + i) * 2 + 1] = 0.7f * std::exp(-float(i) / 900.f) * std::sin(2 * 3.14159265f * 180.f * float(i) / kSampleRate);
        }
        const auto t = dsp::findTransients(pcm.data(), pcm.size() / 2);
        CHECK(t.size() == 4);
        if (t.size() == 4)
            for (size_t i = 0; i < 4; ++i) CHECK(std::llabs(i64(t[i]) - i64((0.5 + 0.5 * double(i)) * kSampleRate)) < 441);
        std::vector<float> silent(88200, 0.f);
        CHECK(dsp::findTransients(silent.data(), silent.size() / 2).empty());
    }
    // --- chord stamp, scale helpers ---
    {
        StampParams sp;
        auto c = stampChord(60, 1.0, 0.5, 90, sp);
        CHECK(c.size() == 3 && c[0].pitch == 60 && c[1].pitch == 64 && c[2].pitch == 67 && c[0].start == 1.0 && c[0].velocity == 90);
        sp.inversion = 1;
        c = stampChord(60, 0, 1, 100, sp);
        CHECK(c.size() == 3 && c[0].pitch == 64 && c[1].pitch == 67 && c[2].pitch == 72);
        StampParams dia; dia.diatonic = true; dia.keyRoot = 0; dia.scale = ScaleParams::Major;
        c = stampChord(62, 0, 1, 100, dia);   // D in C major: D F A = D minor
        CHECK(c.size() == 3 && c[0].pitch == 62 && c[1].pitch == 65 && c[2].pitch == 69);
        dia.size = 4;
        c = stampChord(67, 0, 1, 100, dia);   // G7
        CHECK(c.size() == 4 && c[1].pitch == 71 && c[2].pitch == 74 && c[3].pitch == 77);
        CHECK(inScale(64, 0, ScaleParams::Major) && !inScale(61, 0, ScaleParams::Major) && inScale(61, 1, ScaleParams::Major));
        CHECK(std::string(scaleName(1)) == "Minor" && std::string(chordName(0)) == "Major" && std::string(noteName(13)) == "C#");
    }
    // --- riff machine ---
    {
        RiffParams rp; rp.root = 9; rp.scale = ScaleParams::Minor; rp.octave = 4; rp.range = 14; rp.seed = 7;
        const auto riff = generateRiff(rp, 8.0);
        CHECK(!riff.empty());
        bool ok = true;
        for (size_t i = 0; i < riff.size(); ++i) {
            ok = ok && inScale(riff[i].pitch, 9, ScaleParams::Minor) && riff[i].pitch >= 69 && riff[i].pitch <= 69 + 14;
            ok = ok && riff[i].start >= 0 && riff[i].start + riff[i].length <= 8.0 + 1e-9;
            if (i + 1 < riff.size()) ok = ok && riff[i].start + riff[i].length <= riff[i + 1].start + 1e-9;
        }
        CHECK(ok);
        CHECK(riff.front().pitch == 69 && riff.front().start == 0.0);
        CHECK(generateRiff(rp, 8.0).size() == riff.size());                       // same seed, same riff
        rp.seed = 8;
        const auto other = generateRiff(rp, 8.0);
        bool same = other.size() == riff.size();
        for (size_t i = 0; same && i < riff.size(); ++i) same = same && other[i].pitch == riff[i].pitch && other[i].start == riff[i].start;
        CHECK(!same);
        rp.density = 0.0;
        CHECK(generateRiff(rp, 8.0).size() == 2);                                  // only the two bar starts remain
    }
    // --- logical editor ---
    {
        const std::vector<Note> n = {{60, 0, 0.5, 40}, {62, 1, 0.5, 80}, {64, 2, 1.0, 120}, {72, 3, 0.25, 100}};
        LogicalParams lp;
        lp.conditions = {{LogicalCondition::Velocity, 1, 50}};
        lp.action = LogicalParams::Delete;
        int hits = 0;
        auto out = logicalEdit(n, lp, &hits);
        CHECK(hits == 1 && out.size() == 3 && out[0].pitch == 62);
        lp.conditions = {{LogicalCondition::Pitch, 60, 64}, {LogicalCondition::Velocity, 60, 127}};   // both conditions must hold
        lp.action = LogicalParams::Transpose; lp.value = 12;
        out = logicalEdit(n, lp, &hits);
        CHECK(hits == 2 && out[1].pitch == 74 && out[2].pitch == 76 && out[0].pitch == 60);
        lp.invert = true; lp.action = LogicalParams::SetVelocity; lp.value = 64;
        out = logicalEdit(n, lp, &hits);
        CHECK(hits == 2 && out[0].velocity == 64 && out[3].velocity == 64 && out[2].velocity == 120);
        lp.invert = false; lp.conditions = {{LogicalCondition::Length, 0.9, 2}}; lp.action = LogicalParams::ScaleLength; lp.value = 50;
        out = logicalEdit(n, lp, &hits);
        CHECK(hits == 1 && std::fabs(out[2].length - 0.5) < 1e-9);
        lp.conditions.clear(); lp.action = LogicalParams::Move; lp.value = -5;           // no condition = every note; start never below 0
        out = logicalEdit(n, lp, &hits);
        CHECK(hits == 4 && out[0].start == 0.0);
    }
    // --- Ableton Live transform tools and the chord display ---
    {
        const std::vector<Note> n = {{60, 1.0, 0.5, 90}, {64, 3.0, 1.0, 80}};
        const auto s2 = stretchNotes(n, 2.0), s05 = stretchNotes(n, 0.5);
        CHECK(s2[0].start == 2.0 && s2[1].length == 2.0 && s05[1].start == 1.5 && s05[0].length == 0.25);
        const std::vector<Note> chord = {{60, 0, 1, 90}, {64, 0, 1, 90}, {67, 0, 1, 90}, {72, 2, 1, 90}};
        const auto down = strumNotes(chord, 0.1, true), up = strumNotes(chord, 0.1, false);
        CHECK(down.size() == 4 && down[0].pitch == 60 && std::fabs(down[1].start - 0.1) < 1e-9 && down[1].pitch == 64 && std::fabs(down[2].start - 0.2) < 1e-9 && std::fabs(down[2].length - 0.8) < 1e-9);
        CHECK(up[0].pitch == 67 && up[1].pitch == 64 && up[2].pitch == 60 && std::fabs(up[3].start - 2.0) < 1e-9);   // the lone note keeps its start
        const auto dbl = doublePattern(n, 4.0);
        CHECK(dbl.size() == 4 && dbl[2].start == 5.0 && dbl[3].start == 7.0 && dbl[2].pitch == 60);
        CHECK(detectChord({60, 64, 67}) == "C" && detectChord({57, 60, 64}) == "Am" && detectChord({64, 67, 72}) == "C/E" && detectChord({55, 59, 62, 65}) == "G7");
        CHECK(detectChord({60, 64, 67, 71}) == "Cmaj7" && detectChord({71, 74, 77}) == "Bdim" && detectChord({60}) == "" && detectChord({60, 61, 62, 63, 64}) == "");
        CHECK(detectChord({62, 65, 69, 72}) == "Dm7" && detectChord({60, 67}) == "C5" && detectChord({60, 65, 67}) == "Csus4");
    }
    // --- groove template ---
    {
        std::vector<Note> swing;   // every second sixteenth is late by 0.06 beat and softer
        for (int i = 0; i < 16; ++i) swing.push_back({36, i * 0.25 + (i % 2 ? 0.06 : 0.0), 0.1, i % 2 ? 60 : 110});
        const Groove g = makeGroove(swing, 0.25);
        CHECK(g.valid() && g.timing.size() == 16 && std::fabs(g.timing[1] - 0.06) < 1e-9 && std::fabs(g.timing[0]) < 1e-9 && g.velocity[1] < g.velocity[0]);
        std::vector<Note> straight;
        for (int i = 0; i < 8; ++i) straight.push_back({38, i * 0.25, 0.1, 100});
        const auto full = applyGroove(straight, g, 1.0), half = applyGroove(straight, g, 0.5), none = applyGroove(straight, g, 0.0);
        CHECK(std::fabs(full[1].start - 0.31) < 1e-9 && std::fabs(half[1].start - 0.28) < 1e-9 && none[1].start == 0.25 && full[0].start == 0.0);
        CHECK(full[1].velocity < full[0].velocity && none[1].velocity == 100);
        CHECK(!makeGroove({}, 0.25).valid() && applyGroove(straight, Groove{}, 1.0)[1].start == 0.25);
    }
    // --- Session: Logic Pro (adapt tempo, groove template) ---
    {
        S s;
        s.setBpm(100);
        const std::string wav = makeWav("adapt.wav", 220, 4 * kSampleRate);          // exactly 4 seconds
        s.importAudio(wav, -1, 0);
        CHECK(std::fabs(s.adaptTempoToRegion(0, 0, 8) - 120.0) < 1e-6 && std::fabs(s.bpm() - 120.0) < 1e-6);   // 8 beats in 4 s
        CHECK(std::fabs(s.adaptTempoToRegion(0, 0, 4) - 60.0) < 1e-6);
        CHECK(s.adaptTempoToRegion(3, 0, 4) == 0);
        const int ti = s.addTrack(TrackType::Instrument);
        const int ri = s.addMidiRegion(ti, 0, 4);
        s.setRegionNotes(ti, ri, {{60, 0, 0.1, 100}, {60, 0.25 + 0.05, 0.1, 100}, {60, 0.5, 0.1, 100}, {60, 0.75 + 0.05, 0.1, 100}});
        CHECK(!s.hasGrooveTemplate());
        CHECK(!s.applyGrooveTemplate(ti, ri, 1.0));
        CHECK(s.makeGrooveTemplate(ti, ri, 0.25) && s.hasGrooveTemplate());
        const int rj = s.addMidiRegion(ti, 8, 4);
        s.setRegionNotes(ti, rj, {{62, 0, 0.1, 100}, {62, 0.25, 0.1, 100}});
        CHECK(s.applyGrooveTemplate(ti, rj, 1.0));
        const auto notes = s.snapshot()[size_t(ti)].regions[size_t(rj)].notes;
        CHECK(notes.size() == 2 && std::fabs(notes[1].start - 0.30) < 1e-9);
        s.undo();
        CHECK(std::fabs(s.snapshot()[size_t(ti)].regions[size_t(rj)].notes[1].start - 0.25) < 1e-9);
        CHECK(!s.makeGrooveTemplate(0, 0, 0.25));                                    // an audio region has no notes
    }
    // --- Session: Ableton Live (Capture MIDI) ---
    {
        S s;
        s.setBpm(120);
        const int ti = s.addTrack(TrackType::Instrument);
        const int id = s.snapshot()[size_t(ti)].id;
        CHECK(s.captureMidi(ti) == -1 && s.captureMidi(9) == -1);
        s.injectCapture(id, 60, 100, 100.0, 100.5);       // C at t, 0.5 s long
        s.injectCapture(id, 64, 90, 101.0, 101.25);       // E one second later
        s.injectCapture(id + 50, 67, 80, 101.0, 101.5);   // another track: not captured
        CHECK(s.capturedNotes(ti) == 2);
        s.seekBeats(8);
        const int r = s.captureMidi(ti);
        CHECK(r == 0 && s.capturedNotes(ti) == 0);
        const Region rg = s.snapshot()[size_t(ti)].regions[0];
        CHECK(rg.midi && rg.notes.size() == 2 && rg.start == 8.0);
        CHECK(std::fabs(rg.notes[0].start) < 1e-9 && std::fabs(rg.notes[0].length - 1.0) < 1e-9);       // 0.5 s at 120 BPM = 1 beat
        CHECK(std::fabs(rg.notes[1].start - 2.0) < 1e-9 && rg.notes[1].pitch == 64 && rg.notes[1].velocity == 90);
        CHECK(rg.lengthBeats == 4.0);                                                                    // rounded up to a bar
        s.undo();
        CHECK(s.snapshot()[size_t(ti)].regions.empty());
        const int dt = s.addTrack(TrackType::Drums);
        CHECK(s.captureMidi(dt) == -1);
    }
    // --- Session: FL Studio (Riff Machine) and Cubase (Logical Editor, snapshots) ---
    {
        S s;
        const int ti = s.addTrack(TrackType::Instrument);
        const int ri = s.addMidiRegion(ti, 0, 8);
        RiffParams rp;
        CHECK(s.riffRegion(ti, ri, rp) && !s.snapshot()[size_t(ti)].regions[size_t(ri)].notes.empty());
        CHECK(!s.riffRegion(ti, 5, rp));
        s.setRegionNotes(ti, ri, {{60, 0, 1, 30}, {62, 1, 1, 100}, {64, 2, 1, 110}});
        LogicalParams lp;
        lp.conditions = {{LogicalCondition::Velocity, 1, 50}};
        lp.action = LogicalParams::Delete;
        CHECK(s.logicalEditRegion(ti, ri, lp) == 1 && s.snapshot()[size_t(ti)].regions[size_t(ri)].notes.size() == 2);
        CHECK(s.logicalEditRegion(ti, 9, lp) == -1);
        s.undo();
        CHECK(s.snapshot()[size_t(ti)].regions[size_t(ri)].notes.size() == 3);

        const int a = s.addTrack(TrackType::Audio), b = s.addTrack(TrackType::Audio);
        s.setVolume(a, 0.5f); s.setPan(a, -0.5f); s.setMute(b, true);
        const int snap = s.saveMixSnapshot("Verse");
        CHECK(snap == 0 && s.mixSnapshots().size() == 1 && s.mixSnapshots()[0].name == "Verse" && s.saveMixSnapshot() == 1);
        CHECK(s.mixSnapshots()[1].name == "Snapshot 2");
        s.setVolume(a, 1.0f); s.setPan(a, 0.9f); s.setMute(b, false); s.setMasterVolume(0.3f);
        CHECK(s.recallMixSnapshot(0));
        const auto tr = s.snapshot();
        CHECK(std::fabs(tr[size_t(a)].volume - 0.5f) < 1e-6 && std::fabs(tr[size_t(a)].pan + 0.5f) < 1e-6 && tr[size_t(b)].mute && std::fabs(s.masterVolume() - 0.9f) < 1e-6);
        s.undo();
        CHECK(std::fabs(s.snapshot()[size_t(a)].volume - 1.0f) < 1e-6);
        CHECK(!s.recallMixSnapshot(7));
        s.renameMixSnapshot(1, "Chorus");
        const std::string path = tmpDir() + "/snap.mdaw";
        std::string err;
        CHECK(s.saveProject(path, &err));
        S l;
        CHECK(l.loadProject(path, &err));
        CHECK(l.mixSnapshots().size() == 2 && l.mixSnapshots()[1].name == "Chorus" && l.mixSnapshots()[0].channels.size() == 3);
        CHECK(std::fabs(l.mixSnapshots()[0].channels[size_t(a)].volume - 0.5f) < 1e-6 && l.mixSnapshots()[0].channels[size_t(b)].mute);
        s.removeMixSnapshot(0);
        CHECK(s.mixSnapshots().size() == 1 && s.mixSnapshots()[0].name == "Chorus");
        l.newProject();
        CHECK(l.mixSnapshots().empty());
    }
    // --- Session: Pro Tools (Tab to Transient, Beat Detective) ---
    {
        S s;
        s.setBpm(120);   // 2 beats per second
        const std::string wav = makeClicks("beats.wav", {0.0, 0.5, 1.02, 1.5}, 2.0);   // the third hit is 20 ms late
        s.importAudio(wav, -1, 4);                                                       // the region starts at beat 4
        auto near = [](double a, double b) { return std::fabs(a - b) < 0.05; };
        CHECK(near(s.nextTransient(0, 0, 4.0, true), 5.0));                              // 0.5 s = 1 beat after the start
        CHECK(near(s.nextTransient(0, 0, 5.2, true), 6.04));
        CHECK(near(s.nextTransient(0, 0, 6.5, false), 6.04) && near(s.nextTransient(0, 0, 5.0 + 1e-6, false), 4.0));
        CHECK(s.nextTransient(0, 0, 7.5, true) < 0 && s.nextTransient(0, 0, 4.0, false) < 0 && s.nextTransient(4, 0, 0, true) < 0);
        const int n = s.separateAtTransients(0, 0, 0.5, 0.0);
        CHECK(n == 4);
        auto regs = s.snapshot()[0].regions;
        CHECK(regs.size() == 4);
        if (regs.size() == 4) {
            CHECK(near(regs[0].start, 4.0) && near(regs[1].start, 5.0) && near(regs[2].start, 6.04) && near(regs[3].start, 7.0));
            i64 sum = 0;
            for (const Region& r : regs) sum += r.frames;
            CHECK(sum == 2 * kSampleRate);                                               // nothing lost, nothing doubled
            CHECK(regs[1].fadeIn > 0 && regs[1].srcOffset == regs[1].offset);
        }
        s.undo();
        CHECK(s.snapshot()[0].regions.size() == 1);
        CHECK(s.separateAtTransients(0, 0, 0.5, 0.5, 1.0) == 4);                         // conform to eighth notes: 6.04 -> 6.0
        regs = s.snapshot()[0].regions;
        if (regs.size() == 4) {
            CHECK(std::fabs(regs[2].start - 6.0) < 1e-6 && std::fabs(regs[1].start - 5.0) < 1e-6);
            const double gap = s.beatToFrame(regs[3].start) - (s.beatToFrame(regs[2].start) + double(regs[2].frames));
            CHECK(std::fabs(gap) < 1.5);                                                 // the neighbour was extended, no hole
        }
        S m;
        const int mi = m.addTrack(TrackType::Instrument);
        m.addMidiRegion(mi, 0, 4);
        CHECK(m.separateAtTransients(mi, 0) == 0);
        s.setRegionFlex(0, 0, false, 0, 3);
        CHECK(s.separateAtTransients(0, 0) == 0);                                        // transposed regions are refused
    }
}




// root-mean-square difference between two interleaved buffers over their common length, relative to the RMS of 'a'
static double relDiff(const std::vector<float>& a, const std::vector<float>& b) {
    const size_t n = std::min(a.size(), b.size());
    double d = 0, e = 0;
    for (size_t i = 0; i < n; ++i) { d += double(a[i] - b[i]) * double(a[i] - b[i]); e += double(a[i]) * double(a[i]); }
    return e > 0 ? std::sqrt(d / e) : 1.0;
}

static void testResampler() {
    // a 1 kHz sine keeps its frequency and level through 44.1 -> 96 -> 44.1 kHz
    std::vector<float> sine(44100 * 2);
    for (size_t i = 0; i < sine.size() / 2; ++i) sine[2 * i] = sine[2 * i + 1] = 0.5f * float(std::sin(2 * 3.14159265358979 * 1000.0 * double(i) / 44100.0));
    const auto up = core::resampleStereo(sine, 44100, 96000);
    CHECK(up.size() > 2 * 96000 - 8 && up.size() < 2 * 96000 + 8);
    double peak = 0; for (size_t i = 4000; i + 4000 < up.size(); i += 2) peak = std::max(peak, double(std::fabs(up[i])));
    CHECK(std::fabs(peak - 0.5) < 0.01);
    const auto back = core::resampleStereo(up, 96000, 44100);
    CHECK(back.size() > sine.size() - 8 && back.size() < sine.size() + 8);
    std::vector<float> a(sine.begin() + 8000, sine.end() - 8000), b(back.begin() + 8000, back.end() - 8000);
    CHECK(relDiff(a, b) < 0.01);
    // going down removes what the lower rate cannot hold: 30 kHz at 96 kHz disappears at 44.1 kHz (no aliasing)
    std::vector<float> hi(96000 * 2);
    for (size_t i = 0; i < hi.size() / 2; ++i) hi[2 * i] = hi[2 * i + 1] = 0.5f * float(std::sin(2 * 3.14159265358979 * 30000.0 * double(i) / 96000.0));
    const auto lo = core::resampleStereo(hi, 96000, 44100);
    double lopeak = 0; for (size_t i = 4000; i + 4000 < lo.size(); ++i) lopeak = std::max(lopeak, double(std::fabs(lo[i])));
    CHECK(lopeak < 0.01);
    CHECK(core::resampleStereo(sine, 44100, 44100) == sine);                   // same rate: unchanged
}

static void testId3() {
    core::Id3Tags t;
    t.title = "Song"; t.artist = "Artist \xEC\x9D\x8C\xEC\x95\x85"; t.album = "Album"; t.year = "2026"; t.comment = "Made with MiniDAW";   // Korean letters in UTF-8
    const auto tag = core::id3v2Tag(t);
    CHECK(tag.size() > 40 && tag[0] == 'I' && tag[1] == 'D' && tag[2] == '3' && tag[3] == 3);
    CHECK(core::id3v2Size(tag.data(), tag.size()) == tag.size());
    // the frames are in there
    const std::string raw(tag.begin(), tag.end());
    CHECK(raw.find("TIT2") != std::string::npos && raw.find("TPE1") != std::string::npos && raw.find("TALB") != std::string::npos && raw.find("TYER") != std::string::npos && raw.find("COMM") != std::string::npos);
    // empty tags still carry the encoder name; a size that is not synch-safe is not a tag
    CHECK(core::id3v2Tag(core::Id3Tags{}).size() > 10);
    std::vector<std::uint8_t> bad = {'I', 'D', '3', 3, 0, 0, 0x80, 0, 0, 0, 0, 0};
    CHECK(core::id3v2Size(bad.data(), bad.size()) == 0);
    // prepending replaces an existing tag and keeps the audio bytes
    const std::string path = tmpDir() + "/tagged.mp3";
    { std::ofstream f(path, std::ios::binary); f.write("\xFF\xFB\x90\x00" "AUDIO-BYTES", 15); }
    std::string err;
    CHECK(core::prependId3v2(path, t, &err));
    t.title = "Second";
    CHECK(core::prependId3v2(path, t, &err));
    std::ifstream in(path, std::ios::binary);
    const std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(all.size() > 20 && all.compare(all.size() - 15, 15, std::string("\xFF\xFB\x90\x00" "AUDIO-BYTES", 15)) == 0);
    CHECK(all.find("ID3", 3) == std::string::npos);                              // exactly one tag
    CHECK(!core::prependId3v2(tmpDir() + "/missing-dir/none.mp3", t, &err));
}

static void testExportFormats() {
    const std::string wav = makeWav("exportsrc.wav", 330, 44100 * 3);
    Session s;
    buildProject(s, wav);
    s.setLoop(false, 0, 0);
    const std::string dir = tmpDir() + "/export";
    makeDirs(dir);
    std::string err;
    ExportOptions o;
    o.tail = false;
    CHECK(s.exportAudio(dir + "/base.wav", o, &err));
    std::vector<float> base;
    CHECK(core::readWavFile(dir + "/base.wav", base, &err) && base.size() > 44100);
    // WAV 24-bit at 48 kHz: the file says 48 kHz and holds the same music
    o.format = dsp::WavFormat::Pcm24; o.sampleRate = 48000;
    CHECK(s.exportAudio(dir + "/w48.wav", o, &err));
    {
        std::ifstream f(dir + "/w48.wav", std::ios::binary);
        unsigned char h[44]; f.read(reinterpret_cast<char*>(h), 44);
        const std::uint32_t rate = h[24] | h[25] << 8 | h[26] << 16 | std::uint32_t(h[27]) << 24;
        CHECK(rate == 48000 && h[34] == 24);
    }
    // the 24-bit samples in the file are the 44.1 kHz mix converted to 48 kHz (the reference is the 16-bit export, so a little quantization noise remains) (decoded here by hand: 44 byte header, little-endian)
    auto raw24 = [&](const std::string& file, size_t header, bool bigEndian) {
        std::ifstream f(file, std::ios::binary);
        const std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        std::vector<float> v;
        for (size_t i = header; i + 3 <= all.size(); i += 3) {
            const unsigned char* p = reinterpret_cast<const unsigned char*>(all.data() + i);
            int x = bigEndian ? (p[0] << 16 | p[1] << 8 | p[2]) : (p[2] << 16 | p[1] << 8 | p[0]);
            if (x & 0x800000) x -= 0x1000000;
            v.push_back(float(x / 8388608.0));
        }
        return v;
    };
    std::vector<float> got = raw24(dir + "/w48.wav", 44, false);
    const auto want48 = core::resampleStereo(base, 44100, 48000);
    CHECK(got.size() == want48.size() && relDiff(want48, got) < 0.005);
    CHECK(std::llabs(i64(got.size()) - i64(want48.size())) == 0);
    // AIFF 24-bit at 96 kHz and 16-bit at 44.1 kHz read back through our own AIFF reader
    o.container = ExportContainer::Aiff; o.sampleRate = 96000;
    CHECK(s.exportAudio(dir + "/a96.aiff", o, &err));
    got = raw24(dir + "/a96.aiff", 54, true);                                  // AIFF: 12 + (8 + 18) + (8 + 8) = 54 byte header, big-endian
    const auto want96 = core::resampleStereo(base, 44100, 96000);
    CHECK(got.size() == want96.size() && relDiff(want96, got) < 0.005);
    CHECK(core::readAiffFile(dir + "/a96.aiff", got, &err) && std::llabs(i64(got.size()) - i64(base.size())) < 16);   // our reader converts it back to 44.1 kHz
    o.format = dsp::WavFormat::Pcm16; o.sampleRate = 44100;
    CHECK(s.exportAudio(dir + "/a44.aif", o, &err));
    CHECK(core::readAiffFile(dir + "/a44.aif", got, &err) && got.size() == base.size() && relDiff(base, got) < 0.002);
    // plain AIFF has no 32-bit float; unsupported rates are refused
    o.format = dsp::WavFormat::Float32;
    CHECK(!s.exportAudio(dir + "/bad.aiff", o, &err) && !err.empty());
    o.container = ExportContainer::Wav; o.format = dsp::WavFormat::Pcm16; o.sampleRate = 12345;
    CHECK(!s.exportAudio(dir + "/bad.wav", o, &err));
}


// builds a fake CBR stream of 128 kbit/s 44.1 kHz stereo MPEG-1 layer 3 frames (silence bodies)
static std::string fakeMp3Frames(int frames, int rateIdx = 0) {
    const int rate = rateIdx == 1 ? 48000 : 44100;
    std::string out;
    for (int i = 0; i < frames; ++i) {
        const int pad = (rate == 44100 && i % 3 == 0) ? 1 : 0;   // 417 / 418 bytes like a real 44.1 kHz stream
        std::string f(size_t(144 * 128000 / rate + pad), '\0');
        f[0] = char(0xFF); f[1] = char(0xFB); f[2] = char(0x90 | (rateIdx << 2) | (pad << 1)); f[3] = 0x00;
        out += f;
    }
    return out;
}

static void testMp3Gapless() {
    const std::string dir = tmpDir() + "/gapless";
    makeDirs(dir);
    const std::string path = dir + "/g.mp3";
    {   std::string f = fakeMp3Frames(100); std::string e; CHECK(writeFile(path, f, &e)); }
    // no header yet
    CHECK(!core::readGaplessHeader(path).valid);
    std::string err;
    const std::uint64_t music = 100 * 1152 - 576 - 700;           // 700 samples of padding
    CHECK(core::addGaplessHeader(path, music, 576, &err));
    auto g = core::readGaplessHeader(path);
    CHECK(g.valid && g.frames == 100 && g.delay == 576 && g.padding == 700 && g.sampleRate == 44100 && g.musicSamples() == music);
    // adding twice changes nothing
    std::string once; readFile(path, once);
    CHECK(core::addGaplessHeader(path, music, 576, &err));
    std::string twice; readFile(path, twice);
    CHECK(once == twice);
    // the Info frame is one more frame of the same size, in front of the audio
    CHECK(once.size() == fakeMp3Frames(100).size() + 417);
    CHECK(once.compare(36, 4, "Info") == 0 && once.compare(52, 9, "LAME-MDAW") == 0);
    // an ID3 tag in front is skipped when reading
    core::Id3Tags t; t.title = "x";
    CHECK(core::prependId3v2(path, t, &err));
    g = core::readGaplessHeader(path);
    CHECK(g.valid && g.delay == 576 && g.padding == 700);
    // more padding than a frame holds is clamped to the 12 bits that exist
    const std::string p2 = dir + "/g2.mp3";
    { std::string f = fakeMp3Frames(20, 1); std::string e; writeFile(p2, f, &e); }
    CHECK(core::addGaplessHeader(p2, 100, 576, &err));
    g = core::readGaplessHeader(p2);
    CHECK(g.valid && g.sampleRate == 48000 && g.padding == 4095 && g.delay == 576);
    // not MP3 / no frames: left alone, no error
    const std::string p3 = dir + "/g3.mp3";
    { std::string e; writeFile(p3, std::string("hello"), &e); }
    CHECK(core::addGaplessHeader(p3, 10, 576, &err));
    std::string h3; readFile(p3, h3);
    CHECK(h3 == "hello");
    CHECK(!core::readGaplessHeader(dir + "/missing.mp3").valid);

    // trimming decoded audio: a decoder that kept the delay ...
    core::Mp3Gapless info; info.valid = true; info.frames = 100; info.delay = 576; info.padding = 700; info.sampleRate = 44100;
    std::vector<float> pcm(size_t(100 * 1152) * 2);
    for (size_t i = 0; i < pcm.size() / 2; ++i) pcm[2 * i] = float(i);
    CHECK(core::trimMp3Decoded(pcm, info, 44100));
    CHECK(pcm.size() / 2 == info.musicSamples() && pcm[0] == float(576 + 529));
    // ... and one that already did (length is about the music length): untouched
    const size_t n = pcm.size();
    CHECK(!core::trimMp3Decoded(pcm, info, 44100) && pcm.size() == n);
    // other output rate: delay and length scale
    std::vector<float> pcm48(size_t(double(100 * 1152) * 48000 / 44100) * 2, 0.f);
    CHECK(core::trimMp3Decoded(pcm48, info, 48000));
    CHECK(std::abs(double(pcm48.size() / 2) - double(info.musicSamples()) * 48000 / 44100) < 2.0);
    core::Mp3Gapless none;
    CHECK(!core::trimMp3Decoded(pcm48, none, 48000));
}



static void testBplistAndLogicChunks() {
    // binary plist written by plistlib: {BeatsPerMinute: 82.0, NumberOfTracks: 111, LastSavedFrom: "Logic Pro X 10.5.0 (5283)", Unicode: "Gr\u00f6\u00dfe \u97f3", Flag: true, List: ["a", 2, 3.5]}
    const std::uint8_t bp[] = {0x62, 0x70, 0x6c, 0x69, 0x73, 0x74, 0x30, 0x30, 0xd6, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0e, 0x0f, 0x5e, 0x42, 0x65, 0x61, 0x74, 0x73, 0x50, 0x65, 0x72, 0x4d, 0x69, 0x6e, 0x75, 0x74, 0x65, 0x54, 0x46, 0x6c, 0x61, 0x67, 0x5d, 0x4c, 0x61, 0x73, 0x74, 0x53, 0x61, 0x76, 0x65, 0x64, 0x46, 0x72, 0x6f, 0x6d, 0x54, 0x4c, 0x69, 0x73, 0x74, 0x5e, 0x4e, 0x75, 0x6d, 0x62, 0x65, 0x72, 0x4f, 0x66, 0x54, 0x72, 0x61, 0x63, 0x6b, 0x73, 0x57, 0x55, 0x6e, 0x69, 0x63, 0x6f, 0x64, 0x65, 0x23, 0x40, 0x54, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x5f, 0x10, 0x19, 0x4c, 0x6f, 0x67, 0x69, 0x63, 0x20, 0x50, 0x72, 0x6f, 0x20, 0x58, 0x20, 0x31, 0x30, 0x2e, 0x35, 0x2e, 0x30, 0x20, 0x28, 0x35, 0x32, 0x38, 0x33, 0x29, 0xa3, 0x0b, 0x0c, 0x0d, 0x51, 0x61, 0x10, 0x02, 0x23, 0x40, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x6f, 0x67, 0x00, 0x47, 0x00, 0x72, 0x00, 0xf6, 0x00, 0xdf, 0x00, 0x65, 0x00, 0x20, 0x97, 0xf3, 0x08, 0x15, 0x24, 0x29, 0x37, 0x3c, 0x4b, 0x53, 0x5c, 0x5d, 0x79, 0x7d, 0x7f, 0x81, 0x8a, 0x8c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x9b};
    core::PlistValue v; std::string err;
    CHECK(core::parseBplist(bp, sizeof bp, v, &err));
    CHECK(v.type == core::PlistValue::Type::Dict && v.find("BeatsPerMinute") && v.find("BeatsPerMinute")->number() == 82.0);
    CHECK(v.find("NumberOfTracks")->number() == 111 && v.find("LastSavedFrom")->text() == "Logic Pro X 10.5.0 (5283)");
    CHECK(v.find("Unicode")->text() == "Gr\xC3\xB6\xC3\x9F" "e \xE9\x9F\xB3" && v.find("Flag")->b);
    CHECK(v.find("List")->array.size() == 3 && v.find("List")->array[2].number() == 3.5 && !v.find("Missing"));
    // damaged / wrong input never crashes
    CHECK(!core::parseBplist(bp, 20, v, &err) && !core::parseBplist(nullptr, 0, v, &err));
    std::vector<std::uint8_t> bad(bp, bp + sizeof bp);
    for (size_t i = 8; i + 32 < bad.size(); i += 3) { auto c = bad; c[i] ^= 0xFF; core::PlistValue w; core::parseBplist(c.data(), c.size(), w, nullptr); }
    // chunk reader on a synthetic file in the layout found in a real Logic Pro X 10.5 project
    auto chunk = [](const char* tag, const std::string& payload) {
        std::string c(36, '\0');
        for (int i = 0; i < 4; ++i) c[size_t(i)] = tag[3 - i];            // type written backwards
        const std::uint32_t n = std::uint32_t(payload.size());
        for (int i = 0; i < 4; ++i) c[size_t(28 + i)] = char(n >> (8 * i));
        return c + payload;
    };
    auto fileEntry = [](const std::string& name) {
        std::string p(8, '\0'); p[0] = char(0xE8); p[1] = 3;
        const std::uint16_t n = std::uint16_t(name.size());
        p += char(n & 0xFF); p += char(n >> 8);
        for (char ch : name) { p += ch; p += '\0'; }
        return p + std::string(30, '\0');
    };
    std::string body = chunk("Song", std::string(60, 'x')) + chunk("AuFl", fileEntry("Kick.wav")) + chunk("AuFl", fileEntry("Vox 1.aif")) + chunk("Trak", "");
    std::string file(24, '\0');
    file[0] = 0x23; file[1] = 0x47; file[2] = char(0xC0); file[3] = char(0xAB);
    file += body;
    auto pr = core::parseProjectData(reinterpret_cast<const std::uint8_t*>(file.data()), file.size());
    CHECK(pr.decoded && pr.project.mediaPool.size() == 2 && pr.project.mediaPool[0] == "Kick.wav" && pr.project.mediaPool[1] == "Vox 1.aif");
    CHECK(pr.note.find("end of the file") != std::string::npos);
    bool song = false; for (auto& kv : pr.project.chunkCounts) if (kv.first == "Song" && kv.second == 1) song = true;
    CHECK(song);
    // truncated file: the chain stops early but nothing breaks
    auto pt = core::parseProjectData(reinterpret_cast<const std::uint8_t*>(file.data()), file.size() - 10);
    CHECK(pt.decoded && pt.note.find("stops early") != std::string::npos);
    // a file that does not start with the magic is not this format
    std::string other = file; other[0] = 0;
    CHECK(!core::parseProjectData(reinterpret_cast<const std::uint8_t*>(other.data()), other.size()).decoded);

    // a real project, when one is at hand (not part of the repository): MINIDAW_LOGICX_SAMPLE=<path of the .logicx folder>
    if (const char* real = std::getenv("MINIDAW_LOGICX_SAMPLE")) {
        const std::string pdFile = core::findProjectDataFile(real);
        std::string raw;
        CHECK(!pdFile.empty() && readFile(pdFile, raw));
        auto rr = core::parseProjectData(reinterpret_cast<const std::uint8_t*>(raw.data()), raw.size());
        std::fprintf(stderr, "real sample: %s | pool %zu\n", rr.note.c_str(), rr.project.mediaPool.size());
        CHECK(rr.decoded && rr.note.find("end of the file") != std::string::npos && !rr.project.mediaPool.empty());
        {   // exact MIDI region length (reference probe: track 8 has a 4-bar region at beat 16)
            bool four = false; int whole = 0;
            for (const auto& tr : rr.project.tracks) for (const auto& mr : tr.midi) {
                if (tr.number == 8 && std::fabs(mr.startBeat - 16.0) < 1e-6 && std::fabs(mr.lengthBeats - 16.0) < 1e-6) four = true;
                if (std::fabs(mr.lengthBeats - std::round(mr.lengthBeats)) < 1e-9) ++whole;
            }
            CHECK(four && whole > 0);
        }
        // numbers of the reference decoder (LogicProFormatWriter session_model) on the Logic Pro X 10.5 sample
        CHECK(rr.note.find("255 audio and 28 MIDI regions, 912 notes, 1 tempo point(s), 1 marker(s)") != std::string::npos);
        core::LogicProject lp = rr.project;
        core::readLogicMetadata(real, lp);
        std::fprintf(stderr, "real sample: creator='%s' bpm=%g sr=%d tracks=%d\n", lp.creator.c_str(), lp.tempo.empty() ? 0.0 : lp.tempo[0].bpm, lp.sampleRate, lp.trackCountHint);
        CHECK(!lp.creator.empty() && !lp.tempo.empty() && lp.sampleRate == 44100 && lp.trackCountHint > 0);
        // the whole pipeline on it: tempo from the plist, recordings as tracks, an honest message
        Session ps; std::string msg;
        const int n = core::importLogicPackage(ps, real, &msg);
        std::fprintf(stderr, "real sample import: %d files | %s\n", n, msg.c_str());
        CHECK(n == 255 + 28 && msg.find("tempo map") != std::string::npos && std::fabs(ps.tempoMap()->bpmAt(0) - 82.0) < 1e-6);
        // wait for the (once per file) decoding, then compare with what Logic shows (WindowImage.jpg of the sample): track 43 plays
        // VFB_kit4_drop_vocal at bar 9 and bar 33, the marker sits at bar 9
        auto audioCount = [&] { int c = 0; for (const auto& t : ps.snapshot()) if (t.type == TrackType::Audio) c += int(t.regions.size()); return c; };
        for (int i = 0; i < 12000 && audioCount() < 255; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::fprintf(stderr, "real sample regions placed: %d of 255\n", audioCount());
        CHECK(audioCount() == 255);
        bool bar9 = false, bar33 = false;
        for (const auto& t : ps.snapshot()) {
            if (t.name != "vox") continue;
            for (const auto& r : t.regions) {
                if (r.name.find("drop_vocal") == std::string::npos) continue;
                if (std::fabs(r.start - 32.0) < 1e-6 && r.frames == 1677952) bar9 = true;
                if (std::fabs(r.start - 128.0) < 1e-6) bar33 = true;
            }
        }
        CHECK(bar9 && bar33);
        {   // track names as Logic shows them (WindowImage.jpg) for tracks that carry regions: 1 CH_digital_piano, 43 vox, 44 ending, 60 KSHMR Clock 05 - Countdown,
            // 64 Studio Violins 2 (only tracks that carry regions are created)
            std::map<std::string, int> have;
            for (const auto& t : ps.snapshot()) ++have[t.name];
            CHECK(have.count("vox") && have.count("ending") && have.count("Studio Violins 2") && have.count("KSHMR Clock 05 - Countdown") && have.count("CH_digital_piano [NICKY ROMERO]"));
            bool voxHasRegions = false;
            for (const auto& t : ps.snapshot()) if (t.name == "vox") voxHasRegions = voxHasRegions || t.regions.size() == 2;
            CHECK(voxHasRegions);
        }
        const auto marks = ps.markers();
        CHECK(marks.size() == 1 && std::fabs(marks[0].beat - 32.0) < 1e-6);
    }
}


// Cross-check against the reference decoder (LogicProFormatWriter, session_model) on its own fixtures. tests/data/logicx_reference.tsv holds
// what the reference returns per fixture: name, audio regions, MIDI regions, notes, tempo points, markers, meter points, take folders.
// Needs the fixtures: MINIDAW_LOGICX_FIXTURES=<root folder of LogicProFormatWriter>. Skipped without it.
static void testLogicFixtures() {
    const char* root = std::getenv("MINIDAW_LOGICX_FIXTURES");
    if (!root) return;
    std::string table;
    if (!readFile("../tests/data/logicx_reference.tsv", table) && !readFile("tests/data/logicx_reference.tsv", table)) { CHECK(false); return; }
    int rows = 0, bad = 0, meterDiff = 0;
    size_t pos = 0;
    while (pos < table.size()) {
        size_t e = table.find('\n', pos);
        if (e == std::string::npos) e = table.size();
        const std::string line = table.substr(pos, e - pos);
        pos = e + 1;
        if (line.empty()) continue;
        std::vector<std::string> f;
        for (size_t a = 0, b; a <= line.size(); a = b + 1) { b = line.find('\t', a); if (b == std::string::npos) b = line.size(); f.push_back(line.substr(a, b - a)); }
        if (f.size() < 8) continue;
        std::string raw;
        if (!readFile(std::string(root) + "/" + f[0] + "/Alternatives/000/ProjectData", raw)) continue;
        ++rows;
        core::LogicProject lp;
        const auto st = core::decodeLogicArrangement(reinterpret_cast<const std::uint8_t*>(raw.data()), raw.size(), lp);
        const int audio = st.audioRegions + st.unresolvedAudio;
        const bool ok = audio == std::stoi(f[1]) && st.midiRegions == std::stoi(f[2]) && st.notes == std::stoi(f[3]) &&
                        st.tempoPoints == std::stoi(f[4]) && st.markers == std::stoi(f[5]);
        if (!ok) {
            ++bad;
            std::fprintf(stderr, "fixture mismatch %s: audio %d/%s midi %d/%s notes %d/%s tempo %d/%s markers %d/%s\n", f[0].c_str(), audio, f[1].c_str(), st.midiRegions, f[2].c_str(),
                         st.notes, f[3].c_str(), st.tempoPoints, f[4].c_str(), st.markers, f[5].c_str());
        }
        if (st.meterPoints != std::stoi(f[6])) ++meterDiff;
        if (f[0] == "fixtures/F23_av.logicx") {   // four 1-bar instrument regions at beat 0
            int bars = 0; for (const auto& tr : lp.tracks) for (const auto& mr : tr.midi) if (std::fabs(mr.lengthBeats - 4.0) < 1e-9 && mr.startBeat == 0.0) ++bars;
            CHECK(bars == 4 && st.exactMidiLengths >= 4);
        }
    }
    std::fprintf(stderr, "logic fixtures: %d compared, %d mismatches, %d meter-count differences\n", rows, bad, meterDiff);
    CHECK(rows > 50 && bad == 0);
}


// Track names against the logicxkit reader (tests/data/logicx_tracknames.tsv: fixture, NumberOfTracks, names of the arrange rows joined by 0x1F).
static void testLogicTrackNames() {
    const char* root = std::getenv("MINIDAW_LOGICX_FIXTURES");
    if (!root) return;
    std::string table;
    if (!readFile("../tests/data/logicx_tracknames.tsv", table) && !readFile("tests/data/logicx_tracknames.tsv", table)) { CHECK(false); return; }
    int rows = 0, bad = 0, names = 0;
    size_t pos = 0;
    while (pos < table.size()) {
        size_t e = table.find('\n', pos);
        if (e == std::string::npos) e = table.size();
        const std::string line = table.substr(pos, e - pos);
        pos = e + 1;
        const size_t t1 = line.find('\t'), t2 = t1 == std::string::npos ? t1 : line.find('\t', t1 + 1);
        if (t2 == std::string::npos) continue;
        std::string raw;
        if (!readFile(std::string(root) + "/" + line.substr(0, t1) + "/Alternatives/000/ProjectData", raw)) continue;
        std::vector<std::string> want;
        for (size_t a = t2 + 1, b; a <= line.size(); a = b + 1) { b = line.find('\x1f', a); if (b == std::string::npos) b = line.size(); want.push_back(line.substr(a, b - a)); }
        const auto got = core::readLogicTrackNames(reinterpret_cast<const std::uint8_t*>(raw.data()), raw.size(), std::stoi(line.substr(t1 + 1, t2 - t1 - 1)));
        ++rows;
        bool ok = got.size() == want.size();
        for (size_t i = 0; ok && i < want.size(); ++i) { if (!want[i].empty()) { ++names; if (got[i] != want[i]) ok = false; } }
        if (!ok) { ++bad; std::fprintf(stderr, "track names differ in %s (%zu/%zu rows)\n", line.substr(0, t1).c_str(), got.size(), want.size()); }
    }
    std::fprintf(stderr, "logic track names: %d projects, %d names compared, %d projects differ\n", rows, names, bad);
    CHECK(rows > 50 && bad == 0);
}

static void testLogicPackage() {
    const std::string root = tmpDir() + "/logic";
    removeTree(root);
    const std::string pkg = root + "/My Song.logicx";
    makeDirs(pkg + "/Alternatives/000");
    makeDirs(pkg + "/Media/Audio Files");
    makeDirs(pkg + "/Media/Bounces");
    { std::string e; writeFile(pkg + "/Alternatives/000/ProjectData", std::string("binary"), &e); }
    const std::string a = makeWav("lg_a.wav", 220, 44100), b = makeWav("lg_b.wav", 330, 44100);
    { std::string d; readFile(a, d); std::string e; writeFile(pkg + "/Media/Audio Files/Guitar#01.wav", d, &e); readFile(b, d); writeFile(pkg + "/Media/Audio Files/Bass#01.wav", d, &e); writeFile(pkg + "/Media/Bounces/Mix.wav", d, &e); }
    { std::string e; writeFile(pkg + "/Media/Audio Files/Old.caf", std::string("caff"), &e); writeFile(pkg + "/Media/Audio Files/notes.txt", std::string("x"), &e); }
    CHECK(core::isLogicPath(pkg) && core::isLogicPath("X.LOGICX") && !core::isLogicPath("x.mdaw"));
    CHECK(core::classifyFile(pkg) == core::FileKind::Logic);
    auto pk = core::scanLogicPackage(pkg);
    CHECK(pk.found && pk.name == "My Song" && pk.hasProjectData);
    CHECK(pk.audioFiles.size() == 2 && pk.bounces.size() == 1 && pk.skippedUnreadable == 1);
    CHECK(pk.audioFiles[0].find("Bass#01.wav") != std::string::npos);            // sorted
    CHECK(core::scanLogicPackage(pkg + "/Media/Bounces/Mix.wav").found == false);   // a file is not a package
    CHECK(core::scanLogicPackage(root + "/missing.logicx").found == false);
    // a trailing slash is fine
    CHECK(core::scanLogicPackage(pkg + "/").name == "My Song");
    // no recordings: bounces are used
    const std::string pkg2 = root + "/Only Bounce.logicx";
    makeDirs(pkg2 + "/Media/Bounces");
    { std::string d, e; readFile(a, d); writeFile(pkg2 + "/Media/Bounces/B.wav", d, &e); }
    // import
    Session s;
    const int base = s.trackCount();
    std::string msg;
    CHECK(core::importLogicPackage(s, pkg, &msg) == 2 && msg.find("My Song") != std::string::npos && msg.find("not read") != std::string::npos);
    for (int i = 0; i < 400 && s.trackCount() < base + 2; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(s.trackCount() == base + 2);
    CHECK(core::importLogicPackage(s, pkg, &msg, 1) == 1 && msg.find("1 more") != std::string::npos);
    CHECK(core::importLogicPackage(s, pkg2, &msg) == 1 && msg.find("bounce") != std::string::npos);
    const std::string empty = root + "/Empty.logicx";
    makeDirs(empty);
    CHECK(core::importLogicPackage(s, empty, &msg) == 0 && msg.find("no audio") != std::string::npos);
    CHECK(core::importLogicPackage(s, root + "/plain", &msg) == 0 && msg.find("not a Logic") != std::string::npos);
    // limit
    CHECK(core::importLogicPackage(s, pkg, &msg, 0) == 1);   // at least one

    // ---- the pipeline: ProjectData decoder -> model -> session (the decoder here is a stand-in with an invented layout, it only
    // proves the stages around it; real Logic files need a real decoder)
    {
        core::clearProjectDataDecoders();
        // survey + choice of the ProjectData file
        makeDirs(pkg + "/Alternatives/001");
        { std::string e; writeFile(pkg + "/Alternatives/001/ProjectData", std::string("FAKE-layout 0123456789 TRAK TRAK TRAK TRAK \x01\x02"), &e); }
        CHECK(core::findProjectDataFile(pkg) == pkg + "/Alternatives/001/ProjectData");
        CHECK(core::findProjectDataFile(root + "/Empty.logicx").empty());
        std::string raw; readFile(pkg + "/Alternatives/001/ProjectData", raw);
        auto pr = core::parseProjectData(reinterpret_cast<const std::uint8_t*>(raw.data()), raw.size());
        CHECK(!pr.decoded && pr.note.find("No decoder") != std::string::npos);
        CHECK(pr.survey.size == raw.size() && !pr.survey.fourCCs.empty() && pr.survey.fourCCs[0].first == "TRAK" && pr.survey.fourCCs[0].second == 4);
        CHECK(!pr.survey.strings.empty());
        CHECK(!core::parseProjectData(nullptr, 0).decoded);
        CHECK(!core::registerProjectDataDecoder({}));
        core::registerProjectDataDecoder([](const std::uint8_t* d, std::size_t n, core::LogicProject& out, std::string& name) {
            if (n < 4 || std::string(reinterpret_cast<const char*>(d), 4) != "FAKE") return false;
            name = "test decoder";
            out.name = "Night";
            out.tempo = {{0, 100, false}, {8, 140, false}};
            core::LogicTrack a; a.name = "Guitar"; a.kind = core::LogicTrackKind::Audio;
            a.audio.push_back({"Guitar", "Guitar#01.wav", 4.0, 0});
            a.audio.push_back({"Gone", "NotThere.wav", 8.0, 0});
            core::LogicTrack k; k.name = "Keys"; k.kind = core::LogicTrackKind::Software;
            core::LogicMidiRegion mr; mr.startBeat = 2; mr.lengthBeats = 4; mr.notes = {{60, 0, 1, 100}, {64, 1, 1, 90}};
            k.midi.push_back(mr);
            core::LogicTrack x; x.name = "Skipped"; x.kind = core::LogicTrackKind::Other;
            out.tracks = {a, k, x};
            return true;
        });
        Session ps;
        const int b = ps.trackCount();
        CHECK(core::importLogicPackage(ps, pkg, &msg) == 2);       // one audio + one MIDI region
        CHECK(msg.find("2 track") != std::string::npos && msg.find("1 MIDI") != std::string::npos && msg.find("1 audio file(s) were not found") != std::string::npos);
        for (int i = 0; i < 400; ++i) { const auto sn = ps.snapshot(); if (sn.size() == size_t(b + 2) && !sn[size_t(b)].regions.empty()) break; std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
        const auto sn = ps.snapshot();
        CHECK(sn.size() == size_t(b + 2) && sn[size_t(b)].name == "Guitar" && sn[size_t(b + 1)].name == "Keys");
        CHECK(sn[size_t(b)].regions.size() == 1 && std::fabs(sn[size_t(b)].regions[0].start - 4.0) < 1e-9);
        CHECK(sn[size_t(b + 1)].regions.size() == 1 && std::fabs(sn[size_t(b + 1)].regions[0].start - 2.0) < 1e-9 && sn[size_t(b + 1)].regions[0].notes.size() == 2);
        CHECK(std::fabs(ps.tempoMap()->bpmAt(0) - 100) < 1e-6 && std::fabs(ps.tempoMap()->bpmAt(9) - 140) < 1e-6);
        core::clearProjectDataDecoders();
    }
}

static void testStemsMasterEffects() {
    const std::string dir = tmpDir() + "/stem_master";
    removeTree(dir);
    makeDirs(dir);
    const std::string wav = makeWav("sm.wav", 330, 44100 * 2, 0.4f);
    Session s;
    const int t = s.addTrack(TrackType::Audio, "Tone");
    s.importAudio(wav, t, 0);
    for (int i = 0; i < 400 && s.snapshot()[size_t(t)].regions.empty(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(!s.snapshot()[size_t(t)].regions.empty());
    s.setMasterVolume(0.5f);
    auto peakOf = [](const std::string& path) {
        std::vector<float> pcm; std::string err;
        float p = 0;
        if (readWavFile(path, pcm, &err)) for (float x : pcm) p = std::max(p, std::fabs(x));
        return p;
    };
    ExportOptions o;
    o.format = dsp::WavFormat::Float32;
    o.tail = false;
    std::vector<std::string> a, b; std::string err;
    CHECK(s.exportStems(dir + "/with", o, &a, &err));
    o.masterEffects = false;
    CHECK(s.exportStems(dir + "/without", o, &b, &err));
    auto tone = [](const std::vector<std::string>& v) { for (const auto& f : v) if (f.find("Tone.wav") != std::string::npos) return f; return std::string(); };
    CHECK(!tone(a).empty() && !tone(b).empty());
    const float pw = peakOf(tone(a)), pn = peakOf(tone(b));
    CHECK(pw > 0.05f && pn > pw * 1.8f && pn < pw * 2.2f);          // master volume 0.5 is gone
    CHECK(std::fabs(s.masterVolume() - 0.5f) < 1e-6f);              // and comes back

    // sends: with the reverb send up, the stem carries the reverb; without sends it is the dry track, and the aux returns come back afterwards
    s.setMasterVolume(1.f);
    s.setSendA(t, 1.0f);
    ExportOptions w; w.format = dsp::WavFormat::Float32; w.tail = true; w.masterEffects = false;   // no limiter: it would press the louder (wet) stem
    std::vector<std::string> wet, dry;
    auto energy = [](const std::string& path) { std::vector<float> pcm; std::string e; double sum = 0; if (readWavFile(path, pcm, &e)) for (float x : pcm) sum += double(x) * double(x); return sum; };
    CHECK(s.exportStems(dir + "/wet", w, &wet, &err));
    w.stemSends = false;
    CHECK(s.exportStems(dir + "/dry", w, &dry, &err));
    s.setSendA(t, 0.0f);
    std::vector<std::string> base;
    CHECK(s.exportStems(dir + "/base", w, &base, &err));   // sendA 0, stemSends is false here: the track alone
    w.stemSends = true;
    std::vector<std::string> base2;
    CHECK(s.exportStems(dir + "/base2", w, &base2, &err)); // sendA 0 with sends on: still the track alone
    s.setSendA(t, 1.0f);
    auto load = [](const std::string& path) { std::vector<float> pcm; std::string e; readWavFile(path, pcm, &e); return pcm; };
    auto diffEnergy = [&](const std::string& a, const std::string& b) {
        const auto x = load(a), y = load(b);
        double d = 0;
        for (size_t i = 0; i < std::max(x.size(), y.size()); ++i) { const double u = i < x.size() ? x[i] : 0.0, v = i < y.size() ? y[i] : 0.0; d += (u - v) * (u - v); }
        return d;
    };
    const double e0 = energy(tone(base));
    CHECK(e0 > 0);
    // with the send at 0 the sends make no difference; with the send up, the dry stem is still the track alone ...
    CHECK(diffEnergy(tone(base), tone(base2)) < e0 * 1e-6);
    CHECK(diffEnergy(tone(dry), tone(base)) < e0 * 1e-6);
    // ... and the stem with sends carries the reverb: a clearly different signal, with a longer sounding tail
    CHECK(diffEnergy(tone(wet), tone(dry)) > e0 * 0.02);
    CHECK(load(tone(wet)).size() >= load(tone(dry)).size());
    std::vector<std::string> again;
    CHECK(s.exportStems(dir + "/wet2", w, &again, &err));   // after a dry export the sends are back
    CHECK(diffEnergy(tone(again), tone(wet)) < e0 * 1e-6);
}


static void testMidiImportOptions() {
    const std::string dir = tmpDir() + "/midi_opts";
    removeTree(dir);
    makeDirs(dir);
    // a file with its own tempo (100 BPM, 3/4) and one file track that uses three channels (0, 1 and the drum channel 9)
    dsp::MidiSong song;
    song.bpm = 100; song.num = 3; song.den = 4;
    song.tempos = {{0, 100}};
    dsp::MidiTrack mt;
    mt.name = "Multi";
    mt.notes = {{0, 60, 90, 0.0, 1.0}, {1, 64, 90, 1.0, 1.0}, {9, 36, 100, 2.0, 0.5}};
    song.tracks.push_back(mt);
    const auto bytes = dsp::writeMidiFile(song);
    const std::string path = dir + "/multi.mid";
    { std::string raw(bytes.begin(), bytes.end()), e; CHECK(writeFile(path, raw, &e)); }
    std::string err;

    {   // Auto, empty project: the file's tempo is adopted; one track
        Session s; s.setBpm(120);
        const int base = s.trackCount();
        CHECK(s.importMidiFile(path, &err));
        CHECK(std::fabs(s.tempoMap()->bpmAt(0) - 100) < 1e-6 && s.tempoMap()->meterAtBeat(0).num == 3);
        CHECK(s.trackCount() == base + 1);
    }
    {   // KeepProject: nothing changes in the tempo map
        Session s; s.setBpm(120);
        MidiImportOptions o; o.tempo = MidiTempoPolicy::KeepProject;
        CHECK(s.importMidiFile(path, &err, o));
        CHECK(std::fabs(s.tempoMap()->bpmAt(0) - 120) < 1e-6 && s.tempoMap()->meterAtBeat(0).num == 4);
    }
    {   // Auto in a project that already has a region: the project's tempo stays; UseFile replaces it anyway
        Session s; s.setBpm(120);
        CHECK(s.importMidiFile(path, &err, [] { MidiImportOptions o; o.tempo = MidiTempoPolicy::KeepProject; return o; }()));
        s.setBpm(90);
        CHECK(s.importMidiFile(path, &err));                                    // there are regions now: Auto keeps 90
        CHECK(std::fabs(s.tempoMap()->bpmAt(0) - 90) < 1e-6);
        MidiImportOptions o; o.tempo = MidiTempoPolicy::UseFile;
        CHECK(s.importMidiFile(path, &err, o));
        CHECK(std::fabs(s.tempoMap()->bpmAt(0) - 100) < 1e-6 && s.tempoMap()->meterAtBeat(0).num == 3);
    }
    {   // splitChannels: three tracks, the drum channel becomes a Drummer track, names tell the channel
        Session s;
        const int base = s.trackCount();
        MidiImportOptions o; o.splitChannels = true;
        CHECK(s.importMidiFile(path, &err, o));
        const auto sn = s.snapshot();
        CHECK(int(sn.size()) == base + 3);
        CHECK(sn[size_t(base)].name == "Multi (ch 1)" && sn[size_t(base + 1)].name == "Multi (ch 2)" && sn[size_t(base + 2)].name == "Multi (ch 10)");
        CHECK(sn[size_t(base)].type == TrackType::Instrument && sn[size_t(base + 2)].type == TrackType::Drummer);
        CHECK(sn[size_t(base)].regions[0].notes.size() == 1 && sn[size_t(base + 2)].regions[0].notes.size() == 1 && sn[size_t(base + 2)].regions[0].notes[0].pitch == 36);
    }
    {   // a single-channel track is never split
        Session s;
        const int base = s.trackCount();
        dsp::MidiSong one; one.tracks.push_back({"Solo", {{2, 60, 90, 0.0, 1.0}, {2, 62, 90, 1.0, 1.0}}});
        const auto b2 = dsp::writeMidiFile(one);
        const std::string p2 = dir + "/solo.mid";
        { std::string raw(b2.begin(), b2.end()), e; CHECK(writeFile(p2, raw, &e)); }
        MidiImportOptions o; o.splitChannels = true;
        CHECK(s.importMidiFile(p2, &err, o));
        CHECK(s.trackCount() == base + 1 && s.snapshot()[size_t(base)].name == "Solo");
    }
}

static void testExportMp3AndStems() {
    const std::string wav = makeWav("exportsrc2.wav", 330, 44100 * 3);
    Session s;
    buildProject(s, wav);
    s.setLoop(false, 0, 0);
    const std::string dir = tmpDir() + "/export2";
    makeDirs(dir);
    std::string err;
    ExportOptions o;
    o.tail = false;
    o.container = ExportContainer::Mp3;
    o.tags.title = "My Song"; o.tags.artist = "Me";
    // no encoder: a clear message
    CHECK(!s.exportAudio(dir + "/x.mp3", o, &err) && !err.empty());
    // with an encoder (a fake one here): rate, bitrate (snapped to a legal one) and the tag arrive
    int gotRate = 0, gotKbps = 0;
    size_t gotSamples = 0;
    Session::Hooks hk;
    hk.encodeMp3 = [&](const std::vector<float>& pcm, int rate, int kbps, const std::string& path, std::string*) {
        gotRate = rate; gotKbps = kbps; gotSamples = pcm.size();
        std::ofstream f(path, std::ios::binary); f.write("\xFF\xFB\x90\x00" "FAKE-MP3-FRAMES", 19);
        return true;
    };
    s.setHooks(hk);
    o.sampleRate = 48000; o.mp3Kbps = 200;
    CHECK(s.exportAudio(dir + "/y.mp3", o, &err));
    CHECK(gotRate == 48000 && gotKbps == 192 && gotSamples > 48000);
    {
        std::ifstream in(dir + "/y.mp3", std::ios::binary);
        const std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(all.compare(0, 3, "ID3") == 0 && all.find("My Song") == std::string::npos);   // text is UTF-16: not plain
        CHECK(all.find(std::string("M\0y\0", 4)) != std::string::npos);
        CHECK(all.compare(all.size() - 15, 15, "FAKE-MP3-FRAMES") == 0);
    }
    {   // a stream of real frames gets the gapless header on export
        Session::Hooks hk2;
        size_t sent = 0;
        hk2.encodeMp3 = [&](const std::vector<float>& pcm, int, int, const std::string& path, std::string*) {
            sent = pcm.size() / 2;
            std::string f = fakeMp3Frames(int(pcm.size() / 2 / 1152) + 1, 1);
            std::ofstream out(path, std::ios::binary); out.write(f.data(), std::streamsize(f.size()));
            return true;
        };
        s.setHooks(hk2);
        CHECK(s.exportAudio(dir + "/gap.mp3", o, &err));
        const auto g = core::readGaplessHeader(dir + "/gap.mp3");
        CHECK(g.valid && g.delay == 576 && g.sampleRate == 48000 && g.musicSamples() == sent);   // decodes to exactly the project length
        s.setHooks(hk);
    }
    o.sampleRate = 96000;                                   // MP3 cannot be 96 kHz
    CHECK(!s.exportAudio(dir + "/z.mp3", o, &err));
    o.sampleRate = 44100; o.mp3Kbps = 5000;
    CHECK(s.exportAudio(dir + "/hi.mp3", o, &err) && gotKbps == 320);
    // stems: one file per track that makes sound, all the same length, solo / mute untouched afterwards
    const auto before = s.snapshot();
    ExportOptions so;
    so.tail = false;
    std::vector<std::string> files;
    CHECK(s.exportStems(dir + "/stems", so, &files, &err));
    CHECK(files.size() >= 3);
    size_t len = 0;
    bool same = true;
    for (const auto& f : files) {
        std::vector<float> pcm;
        CHECK(core::readWavFile(f, pcm, &err));
        if (!len) len = pcm.size(); else if (pcm.size() != len) same = false;
    }
    CHECK(same && len > 44100);
    CHECK(files[0].find("/01 ") != std::string::npos && files[0].find(".wav") != std::string::npos);
    const auto after = s.snapshot();
    bool restored = before.size() == after.size();
    for (size_t i = 0; restored && i < before.size(); ++i) restored = before[i].mute == after[i].mute && before[i].solo == after[i].solo;
    CHECK(restored);
    // a project with nothing in it has nothing to export
    Session empty;
    for (int i = 0; i < empty.trackCount(); ++i) empty.setMute(i, true);       // everything muted: nothing to export
    CHECK(!empty.exportStems(dir + "/none", so, nullptr, &err) && !err.empty());
}


static void testExportFormats2() {
    using core::ExportContainer;
    CHECK(core::exportFormatNames().size() == 6 && core::exportRateNames().size() == 4 && core::exportMp3RateNames().size() == 6);
    ExportOptions o;
    core::applyExportChoices(o, 4, 3, 5);       // AIFF 24-bit, 96 kHz, 320 kbps
    CHECK(o.container == ExportContainer::Aiff && o.format == dsp::WavFormat::Pcm24 && o.sampleRate == 96000 && o.mp3Kbps == 320);
    core::applyExportChoices(o, 2, 1, 0);       // WAV 32-bit float, 48 kHz, 128 kbps
    CHECK(o.container == ExportContainer::Wav && o.format == dsp::WavFormat::Float32 && o.sampleRate == 48000 && o.mp3Kbps == 128);
    core::applyExportChoices(o, 5, 0, 2);       // MP3
    CHECK(o.container == ExportContainer::Mp3 && o.sampleRate == 44100 && o.mp3Kbps == 192);
    core::applyExportChoices(o, 99, -4, 77);    // nonsense is clamped
    CHECK(o.container == ExportContainer::Mp3 && o.sampleRate == 44100 && o.mp3Kbps == 320);
    CHECK(core::exportExtension(ExportContainer::Aiff) == ".aiff" && core::exportFilter(ExportContainer::Mp3).find("*.mp3") != std::string::npos);
    CHECK(core::withExportExtension("C:\\out\\song", ExportContainer::Wav) == "C:\\out\\song.wav");
    CHECK(core::withExportExtension("song.wav", ExportContainer::Mp3) == "song.mp3");          // another audio extension is replaced
    CHECK(core::withExportExtension("song.aif", ExportContainer::Aiff) == "song.aif");        // .aif is fine for AIFF
    CHECK(core::withExportExtension("song.MP3", ExportContainer::Mp3) == "song.MP3");
    CHECK(core::withExportExtension("my.take.2", ExportContainer::Wav) == "my.take.2.wav");    // a dot inside the name is not an extension
}


static void testMidiInterchange() {
    // the file layer: tempo map, time signature changes and markers survive a write / read
    dsp::MidiSong song;
    song.tempos = {{0, 100}, {8, 140}, {16, 90}};
    song.meters = {{0, 4, 4}, {8, 3, 4}, {17, 6, 8}};
    song.markers = {{0, "Intro"}, {8, "Verse"}, {16.5, "Chorus"}};
    dsp::MidiTrack t; t.name = "Piano"; t.notes.push_back({0, 60, 100, 0, 1}); t.notes.push_back({0, 64, 90, 9.5, 2});
    song.tracks.push_back(t);
    const auto bytes = dsp::writeMidiFile(song);
    dsp::MidiSong back; std::string err;
    CHECK(dsp::readMidiFile(bytes, back, err));
    CHECK(back.tempos.size() == 3 && std::fabs(back.tempos[1].beat - 8) < 1e-6 && std::fabs(back.tempos[1].bpm - 140) < 0.01 && std::fabs(back.bpm - 100) < 0.01);
    CHECK(back.meters.size() == 3 && back.meters[1].num == 3 && back.meters[1].den == 4 && back.meters[2].num == 6 && back.meters[2].den == 8 && std::fabs(back.meters[2].beat - 17) < 1e-6);
    CHECK(back.markers.size() == 3 && back.markers[0].name == "Intro" && back.markers[2].name == "Chorus" && std::fabs(back.markers[2].beat - 16.5) < 1e-6);
    CHECK(back.tracks.size() == 1 && back.tracks[0].name == "Piano" && back.tracks[0].notes.size() == 2);
    // the conductor track is first and carries the name "Temp"; a file without any of these still reads (and writes the start values)
    dsp::MidiSong plain; plain.bpm = 87; plain.num = 7; plain.den = 8; plain.tracks.push_back(t);
    dsp::MidiSong plainBack;
    CHECK(dsp::readMidiFile(dsp::writeMidiFile(plain), plainBack, err) && std::fabs(plainBack.bpm - 87) < 0.01 && plainBack.num == 7 && plainBack.den == 8 && plainBack.markers.empty());

    // the session layer: export from a project with tempo changes, import into an empty project
    Session a;
    const int ti = a.addTrack(TrackType::Instrument, "Lead");
    const int ri = a.addMidiRegion(ti, 0, 16);
    a.setRegionNotes(ti, ri, {{60, 0, 1, 100}, {62, 12, 2, 90}});
    a.setTempoPoints({{0, 110, false}, {8, 150, false}, {16, 95, true}, {24, 120, false}}, false);
    a.setMeterPoints({{1, 4, 4}, {3, 3, 4}});
    a.addMarker(0, "Start"); a.addMarker(8, "Drop");
    const std::string file = tmpDir() + "/interchange.mid";
    std::string e2;
    CHECK(a.exportMidiFile(file, &e2));
    Session b;
    for (int i = b.trackCount() - 1; i >= 0; --i) b.removeTrack(i);               // an empty project adopts the tempo map
    CHECK(b.importMidiFile(file, &e2));
    const auto tp = b.tempoMap()->tempoPoints();
    CHECK(tp.size() >= 4 && std::fabs(tp[0].bpm - 110) < 0.01);
    bool has150 = false, has95 = false;
    for (const auto& p : tp) { has150 |= std::fabs(p.beat - 8) < 1e-6 && std::fabs(p.bpm - 150) < 0.01; has95 |= std::fabs(p.beat - 16) < 1e-6 && std::fabs(p.bpm - 95) < 0.01; }
    CHECK(has150 && has95);
    CHECK(std::fabs(b.tempoMap()->bpmAt(20) - a.tempoMap()->bpmAt(20)) < 0.5);                           // the glide became steps: close to the original
    const auto mp = b.tempoMap()->meterPoints();
    CHECK(mp.size() == 2 && mp[1].bar == 3 && mp[1].num == 3 && mp[1].den == 4);
    const auto mk = b.markers();
    CHECK(mk.size() == 2 && mk[0].name == "Start" && mk[1].name == "Drop" && std::fabs(mk[1].beat - 8) < 1e-6);
}

static void testCoalescedUndo() {
    Session a;
    for (int i = a.trackCount() - 1; i >= 0; --i) a.removeTrack(i);
    const std::string wavFile = makeWav("coalesce.wav", 440.f, kSampleRate);   // one second of audio
    a.importAudio(wavFile, -1, 0);
    int t = -1;
    for (int i = 0; i < a.trackCount(); ++i) if (!a.snapshot()[size_t(i)].regions.empty() && !a.snapshot()[size_t(i)].regions[0].midi) t = i;
    CHECK(t >= 0);
    if (t < 0) return;
    const std::size_t before = a.undoDepth();
    for (int i = 1; i <= 50; ++i) a.setRegionAudio(t, 0, float(i) * 0.1f, i * 10, i * 20);   // a slider drag: 50 changes in a few milliseconds
    CHECK(a.undoDepth() == before + 1);                                                      // ... are one undo step
    CHECK(std::fabs(a.snapshot()[size_t(t)].regions[0].gainDb - 5.f) < 1e-4f);
    a.undo();
    CHECK(a.snapshot()[size_t(t)].regions[0].gainDb == 0.f && a.snapshot()[size_t(t)].regions[0].fadeIn == 0);   // undo takes back the whole drag
    a.redo();
    CHECK(std::fabs(a.snapshot()[size_t(t)].regions[0].gainDb - 5.f) < 1e-4f);
    a.undo();
    a.setRegionAudio(t, 0, 1.f, 0, 0);                                                       // right after an undo: a new step again
    CHECK(a.undoDepth() == before + 1);
    a.setRegionMute(t, 0, true);                                                              // another kind of change ends the drag
    a.setRegionAudio(t, 0, 2.f, 0, 0);
    CHECK(a.undoDepth() == before + 3);
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    a.setRegionAudio(t, 0, 3.f, 0, 0);                                                       // a pause of more than 0.6 s: a new step
    CHECK(a.undoDepth() == before + 4);
    a.setRegionAudio(t, 0, 4.f, 0, 0, false);                                                // 'undoStep' false never makes one
    CHECK(a.undoDepth() == before + 4);
}

static void testTrimAudioRegion() {
    Session a;
    for (int i = a.trackCount() - 1; i >= 0; --i) a.removeTrack(i);
    const std::string wavFile = makeWav("trim.wav", 440.f, 4 * kSampleRate);   // four seconds
    a.importAudio(wavFile, -1, 0);
    int t = -1;
    for (int i = 0; i < a.trackCount(); ++i) if (!a.snapshot()[size_t(i)].regions.empty() && !a.snapshot()[size_t(i)].regions[0].midi) t = i;
    CHECK(t >= 0);
    if (t < 0) return;
    const Region r0 = a.snapshot()[size_t(t)].regions[0];
    const double beatPerSec = a.tempoMap()->frameToBeat(double(kSampleRate));
    CHECK(!a.trimRegionStart(t, 0, r0.start));                          // at the start: nothing to cut
    CHECK(!a.trimRegionStart(t, 0, r0.start + 99.0));                   // outside the region
    CHECK(!a.trimRegionEnd(t, 0, r0.start + 99.0));
    CHECK(!a.trimRegionStart(t, 5, 1.0) && !a.trimRegionStart(-1, 0, 1.0));
    const std::size_t d0 = a.undoDepth();
    a.setRegionAudio(t, 0, 0.f, 3 * kSampleRate / 2, 0);                // a long fade in (1.5 s)
    CHECK(a.trimRegionStart(t, 0, r0.start + 1.0 * beatPerSec));        // cut the first second
    const Region r1 = a.snapshot()[size_t(t)].regions[0];
    CHECK(std::llabs(r1.offset - (r0.offset + kSampleRate)) <= 1 && std::llabs(r1.frames - (r0.frames - kSampleRate)) <= 1);
    CHECK(std::fabs(r1.start - (r0.start + 1.0 * beatPerSec)) < 1e-6);   // the region starts where the cut was: the audio stays in place
    CHECK(std::llabs(r1.fadeIn - kSampleRate / 2) <= 1);                 // the fade in loses the part that was cut
    CHECK(r1.srcFrames == r1.frames);
    CHECK(a.undoDepth() == d0 + 2);
    CHECK(a.trimRegionEnd(t, 0, r1.start + 2.0 * beatPerSec));           // keep two seconds
    const Region r2 = a.snapshot()[size_t(t)].regions[0];
    CHECK(std::llabs(r2.frames - 2 * kSampleRate) <= 1 && r2.offset == r1.offset);
    CHECK(!a.trimRegionEnd(t, 0, r2.start + 3.0 * beatPerSec));         // not a trim: that would make it longer
    a.setRegionAudio(t, 0, 0.f, 0, 3 * kSampleRate);                     // a fade out that is longer than what is left ...
    CHECK(a.trimRegionEnd(t, 0, r2.start + 1.0 * beatPerSec));
    CHECK(a.snapshot()[size_t(t)].regions[0].fadeOut <= a.snapshot()[size_t(t)].regions[0].frames);   // ... is cut to the region
    a.undo(); a.undo(); a.undo();
    const Region r3 = a.snapshot()[size_t(t)].regions[0];
    CHECK(r3.offset == r1.offset && r3.frames == r1.frames);             // undo goes back step by step
    a.undo(); a.undo();
    const Region r4 = a.snapshot()[size_t(t)].regions[0];
    CHECK(r4.offset == r0.offset && r4.frames == r0.frames && std::fabs(r4.start - r0.start) < 1e-9);
    // a stretched region cannot be trimmed
    a.setRegionFlex(t, 0, false, 0.0, 3.0);   // transposed by 3 semitones
    CHECK(!a.trimRegionStart(t, 0, r0.start + 1.0 * beatPerSec));
}

static void testMidiChannels() {
    Session a;
    for (int i = a.trackCount() - 1; i >= 0; --i) a.removeTrack(i);   // a new project starts with default tracks
    const int t1 = a.addTrack(TrackType::Instrument, "Auto A");
    const int t2 = a.addTrack(TrackType::Instrument, "Chosen");
    const int t3 = a.addTrack(TrackType::Instrument, "Auto B");
    const int td = a.addTrack(TrackType::Drummer, "Drums");
    for (int ti : {t1, t2, t3, td}) {
        const int ri = a.addMidiRegion(ti, 0, 4);
        a.setRegionNotes(ti, ri, {{60, 0, 1, 100}});
    }
    a.setTrackMidiChannel(t2, 3);
    a.setTrackMidiChannel(99, 5);                          // a track that does not exist is ignored
    CHECK(a.snapshot()[size_t(t2)].midiChannel == 3);
    a.setTrackMidiChannel(t1, 40);                         // clamped to 1..16
    CHECK(a.snapshot()[size_t(t1)].midiChannel == 16);
    a.setTrackMidiChannel(t1, 0);                          // back to automatic
    a.undo();
    CHECK(a.snapshot()[size_t(t1)].midiChannel == 16);     // undo restores the previous value
    a.setTrackMidiChannel(t1, 0);
    const std::string file = tmpDir() + "/channels.mid";
    std::string err;
    CHECK(a.exportMidiFile(file, &err));
    std::vector<std::uint8_t> bytes;
    {
        FILE* f = std::fopen(file.c_str(), "rb");
        CHECK(f != nullptr);
        if (f) { int c; while ((c = std::fgetc(f)) != EOF) bytes.push_back(std::uint8_t(c)); std::fclose(f); }
    }
    dsp::MidiSong song;
    CHECK(dsp::readMidiFile(bytes, song, err));
    CHECK(song.tracks.size() == 4);
    auto channelOf = [&](const std::string& name) {
        for (const auto& mt : song.tracks) if (mt.name == name && !mt.notes.empty()) return mt.notes[0].channel;
        return -1;
    };
    CHECK(channelOf("Auto A") == 0);       // automatic: 1, 2, ... in track order, skipping the chosen channel 3 and the drum channel
    CHECK(channelOf("Chosen") == 2);
    CHECK(channelOf("Auto B") == 1);
    CHECK(channelOf("Drums") == 9);

    // the choice is saved in the project and survives a copy of the track
    const std::string proj = tmpDir() + "/channels.mdaw";
    CHECK(a.saveProject(proj, &err));
    Session b;
    CHECK(b.loadProject(proj, &err));
    int found = 0;
    for (const Track& t : b.snapshot()) {
        if (t.name == "Chosen") { CHECK(t.midiChannel == 3); ++found; }
        if (t.name == "Auto A") { CHECK(t.midiChannel == 0); ++found; }
    }
    CHECK(found == 2);
    const int copy = b.duplicateTrack(1);
    (void) copy;
    int withThree = 0;
    for (const Track& t : b.snapshot()) if (t.midiChannel == 3) ++withThree;
    CHECK(withThree == 2);

    // a file whose track plays on one channel gives the track that channel; exporting again keeps it
    Session c;
    for (int i = c.trackCount() - 1; i >= 0; --i) c.removeTrack(i);
    CHECK(c.importMidiFile(file, &err));
    int ch3 = 0;
    for (const Track& t : c.snapshot()) if (t.name == "Chosen") ch3 = t.midiChannel;
    CHECK(ch3 == 3);
    // the file version: written as 4, read back; a newer file still opens but is reported
    {
        CHECK(Session::projectFileVersion() == 4);
        Session v;
        CHECK(v.loadedFileVersion() == 0 && !v.loadedFromNewerVersion());
        CHECK(v.loadProject(proj, &err) && v.loadedFileVersion() == 4 && !v.loadedFromNewerVersion());
        std::string text;
        {
            FILE* f = std::fopen(proj.c_str(), "rb");
            CHECK(f != nullptr);
            if (f) { int ch; while ((ch = std::fgetc(f)) != EOF) text.push_back(char(ch)); std::fclose(f); }
        }
        const std::regex re("\"version\"\\s*:\\s*4");
        CHECK(std::regex_search(text, re));
        const std::string newer = tmpDir() + "/newer.mdaw", old = tmpDir() + "/old.mdaw";
        auto put = [&](const std::string& path, const std::string& body) { FILE* f = std::fopen(path.c_str(), "wb"); CHECK(f != nullptr); if (f) { std::fwrite(body.data(), 1, body.size(), f); std::fclose(f); } };
        put(newer, std::regex_replace(text, re, "\"version\": 99"));
        CHECK(v.loadProject(newer, &err) && v.loadedFileVersion() == 99 && v.loadedFromNewerVersion());
        CHECK(v.snapshot().size() == 4);                      // the known parts load as usual
        put(old, std::regex_replace(text, re, "\"zz\": 0"));   // no version at all (a very old file)
        CHECK(v.loadProject(old, &err) && v.loadedFileVersion() == 0 && !v.loadedFromNewerVersion());
    }
    // with more than 15 instrument tracks the automatic channels run out and wrap to channel 1 instead of failing
    Session many;
    for (int i = many.trackCount() - 1; i >= 0; --i) many.removeTrack(i);
    for (int i = 0; i < 17; ++i) {
        const int ti = many.addTrack(TrackType::Instrument, "T" + std::to_string(i));
        const int ri = many.addMidiRegion(ti, 0, 4);
        many.setRegionNotes(ti, ri, {{60, 0, 1, 100}});
    }
    const std::string f2 = tmpDir() + "/many.mid";
    CHECK(many.exportMidiFile(f2, &err));
}

static void testMixerGroups() {
    Session s;
    const int a = s.addTrack(TrackType::Instrument, "A"), b = s.addTrack(TrackType::Instrument, "B"), c = s.addTrack(TrackType::Instrument, "C");
    s.setVolume(a, 0.8f); s.setVolume(b, 0.4f); s.setVolume(c, 0.6f);
    s.setPan(a, 0.f); s.setPan(b, 0.2f);
    s.setTrackGroup(a, 1); s.setTrackGroup(b, 1);                     // c stays out of the group
    // volume keeps the ratio between the members: a 0.8 -> 0.4 halves b too
    s.setVolume(a, 0.4f);
    CHECK(std::fabs(s.snapshot()[size_t(a)].volume - 0.4f) < 1e-6f && std::fabs(s.snapshot()[size_t(b)].volume - 0.2f) < 1e-6f);
    CHECK(std::fabs(s.snapshot()[size_t(c)].volume - 0.6f) < 1e-6f);
    // pan keeps the difference, and is clamped
    s.setPan(a, 0.5f);
    CHECK(std::fabs(s.snapshot()[size_t(b)].pan - 0.7f) < 1e-6f);
    s.setPan(a, 1.0f);
    CHECK(std::fabs(s.snapshot()[size_t(b)].pan - 1.0f) < 1e-6f);
    // sends, mute and solo are shared
    s.setSendA(a, 0.3f);
    CHECK(std::fabs(s.snapshot()[size_t(b)].sendA - 0.3f) < 1e-6f && s.snapshot()[size_t(c)].sendA == 0.f);
    s.setMute(b, true);
    CHECK(s.snapshot()[size_t(a)].mute && s.snapshot()[size_t(b)].mute && !s.snapshot()[size_t(c)].mute);
    s.setSolo(a, true);
    CHECK(s.snapshot()[size_t(b)].solo && !s.snapshot()[size_t(c)].solo);
    // a member at zero cannot be scaled: it is set to the same value
    s.setVolume(b, 0.f);
    CHECK(s.snapshot()[size_t(a)].volume == 0.f);
    s.setVolume(a, 0.5f);
    CHECK(std::fabs(s.snapshot()[size_t(b)].volume - 0.5f) < 1e-6f);
    // linking off: only the track itself
    s.setGroupLinking(false);
    s.setVolume(a, 0.9f); s.setMute(a, false);
    CHECK(std::fabs(s.snapshot()[size_t(b)].volume - 0.5f) < 1e-6f && s.snapshot()[size_t(b)].mute && !s.snapshot()[size_t(a)].mute);
    s.setGroupLinking(true);
    // out-of-range numbers are clamped; undo restores the group
    s.setTrackGroup(c, 99);
    CHECK(s.snapshot()[size_t(c)].group == 8);
    s.undo();
    CHECK(s.snapshot()[size_t(c)].group == 0);
    // groups are saved with the project
    const std::string file = tmpDir() + "/groups.mdaw";
    std::string err;
    CHECK(s.saveProject(file, &err));
    Session t;
    CHECK(t.loadProject(file, &err));
    CHECK(t.snapshot().size() == s.snapshot().size() && t.snapshot()[size_t(a)].group == 1 && t.snapshot()[size_t(b)].group == 1 && t.snapshot()[size_t(c)].group == 0);
    // a mix snapshot recall is not a group edit: it restores every track exactly
    s.setGroupLinking(true);
    const int snap = s.saveMixSnapshot();
    s.setVolume(a, 0.2f);
    s.recallMixSnapshot(snap);
    CHECK(std::fabs(s.snapshot()[size_t(a)].volume - 0.9f) < 1e-6f && std::fabs(s.snapshot()[size_t(b)].volume - 0.5f) < 1e-6f);
}

static void testRevisionAndPeaks() {
    Session s;
    const int t = s.addTrack(TrackType::Instrument, "R");
    const auto r0 = s.revision();
    s.setVolume(t, 0.5f);
    CHECK(s.revision() > r0);                       // every edit moves the revision (the UI copies the model only then)
    const auto r1 = s.revision();
    std::vector<std::pair<float, float>> pk;
    s.trackPeaks(pk);
    CHECK(pk.size() == size_t(s.trackCount()) && pk[size_t(t)].first == 0.f && pk[size_t(t)].second == 0.f);
    CHECK(s.revision() == r1);                      // reading peaks does not count as a change
}


static void testFileTypes() {
    using core::FileKind;
    CHECK(core::classifyFile("C:\\music\\Song.MP3") == FileKind::Audio);
    CHECK(core::classifyFile("/home/me/take 1.wav") == FileKind::Audio);
    CHECK(core::classifyFile("loop.AIFF") == FileKind::Audio && core::classifyFile("a.flac") == FileKind::Audio && core::classifyFile("a.m4a") == FileKind::Audio);
    CHECK(core::classifyFile("a.ogg") == FileKind::Audio && core::classifyFile("a.opus") == FileKind::Audio);
    CHECK(core::classifyFile("song.mdaw") == FileKind::Project && core::classifyFile("SONG.MDAW") == FileKind::Project);
    CHECK(core::classifyFile("tune.mid") == FileKind::Midi && core::classifyFile("tune.MIDI") == FileKind::Midi && core::classifyFile("k.kar") == FileKind::Midi);
    CHECK(core::classifyFile("notes.txt") == FileKind::Unknown && core::classifyFile("noextension") == FileKind::Unknown);
    CHECK(core::classifyFile("dir.mp3/file") == FileKind::Unknown && core::classifyFile("trailingdot.") == FileKind::Unknown);   // the dot must belong to the file name
    CHECK(core::fileExtension("a.b/c.TXT") == ".txt" && core::fileExtension("a.b\\c") == "");
    // the filters list every kind, the 'all supported' entry first
    const std::string all = core::openFilter();
    CHECK(all.rfind("All supported files|", 0) == 0);
    CHECK(all.find("*.mdaw") != std::string::npos && all.find("*.mp3") != std::string::npos && all.find("*.mid") != std::string::npos && all.find("*.aiff") != std::string::npos);
    CHECK(core::audioFilter().find("*.mdaw") == std::string::npos && core::audioFilter().find("*.flac") != std::string::npos);
    CHECK(core::midiFilter().find("*.midi") != std::string::npos);
    CHECK(core::supportedExtensionsText().find("mp3") != std::string::npos && core::supportedExtensionsText().find("mdaw") == 0);
    // every audio extension of the list is accepted by the classifier
    for (const auto& e : core::audioExtensions()) CHECK(core::classifyFile("x" + e) == FileKind::Audio);
}

static void testAiff() {
    const std::string dir = tmpDir();
    makeDirs(dir);
    std::vector<float> pcm;
    std::string err;
    // big-endian AIFF at 44.1 kHz: values and length survive
    CHECK(core::readAiffFile(writeTestAiff(dir + "/a.aif", false, false, 44100, 1000), pcm, &err));
    CHECK(pcm.size() == 2000 && std::fabs(pcm[200] - 0.25f) < 1e-4f && std::fabs(pcm[201] + 0.25f) < 1e-4f);
    // AIFF-C 'sowt' (little-endian samples)
    CHECK(core::readAiffFile(writeTestAiff(dir + "/b.aifc", true, true, 44100, 500), pcm, &err));
    CHECK(pcm.size() == 1000 && std::fabs(pcm[100] - 0.25f) < 1e-4f && std::fabs(pcm[101] + 0.25f) < 1e-4f);
    // another sample rate is resampled to 44.1 kHz: half the rate -> twice the frames
    CHECK(core::readAiffFile(writeTestAiff(dir + "/c.aif", false, false, 22050, 1000), pcm, &err));
    CHECK(pcm.size() > 3900 && pcm.size() < 4100);
    // not an AIFF file / missing file
    CHECK(!core::readAiffFile(dir + "/a.aif.missing", pcm, &err));
    { std::ofstream f(dir + "/junk.aif", std::ios::binary); f << "this is not an aiff file at all"; }
    CHECK(!core::readAiffFile(dir + "/junk.aif", pcm, &err) && !err.empty());
}

static void testMoveRegions() {
    using S = Session;
    S s;
    s.setBpm(120);
    const int a = s.addTrack(TrackType::Instrument, "A");
    const int b = s.addTrack(TrackType::Instrument, "B");
    const int d = s.addTrack(TrackType::Drummer, "D");
    const int au = s.addTrack(TrackType::Audio, "Au");
    const int r0 = s.addMidiRegion(a, 4, 4), r1 = s.addMidiRegion(a, 8, 2), r2 = s.addMidiRegion(a, 12, 1);
    (void)r2;
    // one region to the next track, 2 beats later
    auto out = s.moveRegions({{a, r0}}, 2.0, 1);
    CHECK(out.size() == 1 && out[0].first == b && out[0].second == 0);
    CHECK(s.snapshot()[size_t(b)].regions[0].start == 6.0);
    CHECK(s.snapshot()[size_t(a)].regions.size() == 2);
    // a group: the others keep their place after the erase; the group moves together and never before beat 0
    const auto snapA = s.snapshot()[size_t(a)].regions;   // starts 8 and 12 (indices shifted by the erase)
    CHECK(snapA.size() == 2 && snapA[0].start == 8.0 && snapA[1].start == 12.0);
    out = s.moveRegions({{a, 0}, {a, 1}}, -100.0, 0);
    CHECK(out.size() == 2);
    const auto ra = s.snapshot()[size_t(a)].regions;
    CHECK(ra.size() == 2 && ra[0].start == 0.0 && ra[1].start == 4.0);                    // clamped, spacing kept
    // a MIDI region cannot go to an Audio track: it stays and only moves in time; a normal MIDI region may go to a Drummer track
    out = s.moveRegions({{a, 0}}, 1.0, au - a);
    CHECK(out.size() == 1 && out[0].first == a && s.snapshot()[size_t(a)].regions[size_t(out[0].second)].start == 1.0);
    out = s.moveRegions({{a, 0}}, 0.0, d - a);
    CHECK(out.size() == 1 && out[0].first == d);
    // invalid input
    CHECK(s.moveRegions({{99, 0}, {a, 77}}, 1.0, 1).empty());
    CHECK(s.moveRegions({}, 1.0, 1).empty());
    // undo (one checkpoint by the caller) restores everything
    s.checkpoint();
    s.moveRegions({{b, 0}}, 5.0, -1);
    s.undo();
    CHECK(s.snapshot()[size_t(b)].regions.size() == 1);
    (void)r1;
    // group clipboard, mute and ripple delete
    {
        S m;
        const int t1 = m.addTrack(TrackType::Instrument, "I1"), t2 = m.addTrack(TrackType::Instrument, "I2"), ta = m.addTrack(TrackType::Audio, "Au");
        m.addMidiRegion(t1, 4, 2); m.addMidiRegion(t1, 8, 2); m.addMidiRegion(t1, 12, 2); m.addMidiRegion(t2, 6, 2);
        CHECK(m.copyRegions({{t1, 0}, {t2, 0}}) && m.hasClipboard());
        const auto pasted = m.pasteRegions(t1, 20.0);
        CHECK(pasted.size() == 2 && pasted[0].first == t1 && pasted[1].first == t2);
        CHECK(m.snapshot()[size_t(t1)].regions[size_t(pasted[0].second)].start == 20.0);
        CHECK(m.snapshot()[size_t(t2)].regions[size_t(pasted[1].second)].start == 22.0);      // spacing kept (6 - 4 = 2)
        m.undo();
        CHECK(m.snapshot()[size_t(t2)].regions.size() == 1);
        CHECK(m.pasteRegions(ta, 0.0).empty());                                                // MIDI does not fit an audio track
        CHECK(m.pasteRegions(t2, 0.0).size() == 1);                                            // second one would land past the last MIDI track? no: t2 + 1 = audio, skipped
        CHECK(m.muteRegions({{t1, 0}, {t1, 1}, {9, 9}}, true) == 2);
        CHECK(m.snapshot()[size_t(t1)].regions[0].mute && m.snapshot()[size_t(t1)].regions[1].mute && !m.snapshot()[size_t(t1)].regions[2].mute);
        m.undo();
        CHECK(!m.snapshot()[size_t(t1)].regions[0].mute);
        // ripple delete: regions at 4-6, 8-10, 12-14 on t1; delete the first two -> the third moves 4 beats earlier
        const double gone = m.rippleDeleteRegions({{t1, 0}, {t1, 1}});
        CHECK(std::fabs(gone - 4.0) < 1e-9);
        const auto after = m.snapshot()[size_t(t1)].regions;
        CHECK(after.size() == 1 && std::fabs(after[0].start - 8.0) < 1e-9);
        m.undo();
        CHECK(m.snapshot()[size_t(t1)].regions.size() == 3);
        CHECK(m.rippleDeleteRegions({}) == 0.0);
    }
    // several regions deleted / duplicated in one undo step
    {
        S m;
        const int t = m.addTrack(TrackType::Instrument, "M");
        m.addMidiRegion(t, 0, 2); m.addMidiRegion(t, 4, 2); m.addMidiRegion(t, 8, 2); m.addMidiRegion(t, 12, 2);
        CHECK(m.removeRegions({{t, 1}, {t, 3}, {t, 1}, {t, 9}}) == 2);
        const auto rs = m.snapshot()[size_t(t)].regions;
        CHECK(rs.size() == 2 && rs[0].start == 0.0 && rs[1].start == 8.0);
        m.undo();
        CHECK(m.snapshot()[size_t(t)].regions.size() == 4);
        const auto dup = m.duplicateRegions({{t, 0}, {t, 2}});
        CHECK(dup.size() == 2 && m.snapshot()[size_t(t)].regions.size() == 6);
        CHECK(m.snapshot()[size_t(t)].regions[size_t(dup[0].second)].start == 2.0);
        m.undo();
        CHECK(m.snapshot()[size_t(t)].regions.size() == 4);
        CHECK(m.removeRegions({}) == 0 && m.duplicateRegions({{5, 5}}).empty());
    }
}

int main() {
    testJson(); testText(); testWav(); testSaveLoad(); testUndo(); testClipboardAndDuplicate(); testAutosave();
    testExport(); testBounce(); testAutomationPan(); testMidiOps(); testMidiOps2(); testSessionRegions(); testSessionTransforms();
    testMidiOps3(); testTempoDetect(); testRegionCommands(); testConvertPattern(); testSessionNewTransforms(); testV09(); testV012(); testAppPaths(); testFileTypes(); testAiff(); testMoveRegions(); testRevisionAndPeaks(); testMixerGroups(); testMidiChannels(); testTrimAudioRegion(); testCoalescedUndo(); testMidiInterchange(); testExportFormats2(); testResampler(); testId3(); testExportFormats(); testExportMp3AndStems(); testStemsMasterEffects(); testMidiImportOptions(); testMp3Gapless(); testLogicPackage(); testBplistAndLogicChunks(); testLogicFixtures(); testLogicTrackNames();
    std::printf("core fails=%d\n", fails);
    return fails;
}
