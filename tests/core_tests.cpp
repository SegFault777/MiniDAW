// Tests of the Qt-free core: JSON, text, WAV files, Session (project model, undo, save / load, autosave,
// export, bounce in place) and the MIDI operations.
#include <cmath>
#include <cstdio>
#include <fstream>
#include "../src/core/Files.h"
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

int main() {
    testJson(); testText(); testWav(); testSaveLoad(); testUndo(); testClipboardAndDuplicate(); testAutosave();
    testExport(); testBounce(); testAutomationPan(); testMidiOps(); testMidiOps2(); testSessionRegions(); testSessionTransforms();
    testMidiOps3(); testTempoDetect(); testRegionCommands(); testConvertPattern(); testSessionNewTransforms();
    std::printf("core fails=%d\n", fails);
    return fails;
}
