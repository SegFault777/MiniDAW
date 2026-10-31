// Tests of the Qt-free platform layer (the silent backend, decoding, MIDI stubs) wired to the core Session the
// same way the Qt Engine does it, and of the translation tables.
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include "../src/core/Files.h"
#include "../src/core/I18n.h"
#include "../src/core/Session.h"
#include "../src/platform/Audio.h"
#include "../src/platform/Decode.h"
#include "../src/platform/Midi.h"
int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
using namespace core;

static void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

static void testDevices() {
    const auto outs = platform::audioOutputs(), ins = platform::audioInputs();
    CHECK(outs.size() == 1 && outs[0].id == "null" && outs[0].isDefault);
    CHECK(ins.size() == 1 && ins[0].id == "null");
    CHECK(platform::midiInputs().empty() && platform::openMidiInput(0, [](int, int, int) {}) == nullptr);
}

struct Counter {
    std::atomic<long> frames{0};
    std::atomic<int> calls{0};
};

static void testOutputStream() {
    // a raw stream pulls audio at about the real-time rate
    Counter c;
    std::string err;
    auto out = platform::openOutput("", 1024, [&](std::int16_t* o, int n) { for (int i = 0; i < n * 2; ++i) o[i] = 0; c.frames += n; ++c.calls; }, &err);
    CHECK(out != nullptr && !out->failed() && out->status().find("44100 Hz") != std::string::npos);
    sleepMs(400);
    out.reset();                                              // stops and joins
    CHECK(c.frames > 44100 * 0.3 && c.frames < 44100 * 0.6);  // ~0.4 s of audio
    const long after = c.frames;
    sleepMs(60); CHECK(c.frames == after);                    // really stopped

    // the Session drives it exactly like the Qt Engine does
    Session s;
    int t = s.addTrack(TrackType::Instrument, "Keys");
    int r = s.addMidiRegion(t, 0, 4);
    s.setRegionNotes(t, r, {{60, 0, 2, 100}});
    std::atomic<float> peak{0};
    out = platform::openOutput("null", 512, [&](std::int16_t* o, int n) {
        s.fillInt16(o, n);
        float p = peak; for (int i = 0; i < n * 2; ++i) p = std::max(p, std::fabs(o[i] / 32768.f)); peak = p;
    }, &err);
    CHECK(out != nullptr);
    s.play();
    sleepMs(500);
    CHECK(s.isPlaying() && s.positionBeats() > 0.5 && s.positionBeats() < 1.6);   // 120 bpm: 0.5 s = 1 beat
    CHECK(peak > 0.01f);                                                          // the synth was heard
    s.stop();
    out.reset();
}

static void testRecording() {
    Session s;
    std::unique_ptr<platform::AudioStream> in;
    Session::Hooks h;
    h.startInput = [&] { std::string e; in = platform::openInput("", [&](const float* st, int n) { s.pushRecorded(std::vector<float>(st, st + size_t(n) * 2)); }, &e); };
    h.stopInput = [&] { in.reset(); };
    s.setHooks(h);
    while (s.trackCount() > 0) s.removeTrack(0);
    int t = s.addTrack(TrackType::Audio, "Mic");
    s.setArmed(t, true);
    auto out = platform::openOutput("", 512, [&](std::int16_t* o, int n) { s.fillInt16(o, n); }, nullptr);
    CHECK(s.startRecording());
    CHECK(in != nullptr);
    sleepMs(600);
    s.stopRecording();
    CHECK(in == nullptr);
    const auto tr = s.snapshot();
    CHECK(tr[0].regions.size() == 1 && tr[0].regions[0].frames > 44100 * 0.35 && tr[0].regions[0].frames < 44100 * 0.8);
    out.reset();
}

static std::string wavFile(const char* name, int frames) {
    std::vector<float> pcm(size_t(frames) * 2);
    for (int i = 0; i < frames; ++i) pcm[2*i] = pcm[2*i+1] = 0.3f * std::sin(float(i) * 0.1f);
    const std::string p = std::string("/tmp/minidaw_platform_") + name;
    std::string e; CHECK(writeWavFile(p, pcm, &e));
    return p;
}

static void testDecode() {
    std::vector<float> pcm; std::string err;
    CHECK(platform::decodeFile(wavFile("a.wav", 5000), pcm, &err) && pcm.size() == 10000);
    const std::string odd = wavFile("b.dat", 3000);                     // a WAV with another extension is still read
    CHECK(platform::decodeFile(odd, pcm, &err) && pcm.size() == 6000);
    CHECK(!platform::decodeFile("/tmp/minidaw_platform_missing.wav", pcm, &err) && !err.empty());
    writeFile("/tmp/minidaw_platform_c.mp3", "not really an mp3");
    CHECK(!platform::decodeFile("/tmp/minidaw_platform_c.mp3", pcm, &err));
    const auto ext = platform::decodableExtensions(); CHECK(!ext.empty() && ext[0] == ".wav");
    // importing through a Session without a decode hook uses the same WAV reader
    Session s; while (s.trackCount() > 0) s.removeTrack(0);
    s.importAudio(wavFile("d.wav", 44100), -1, 0);
    CHECK(s.trackCount() == 1 && s.snapshot()[0].regions[0].frames == 44100);
}

static void testI18n() {
    TranslationTable t;
    CHECK(t.parse("\xEF\xBB\xBF#name\tFran\xC3\xA7" "ais\r\n# comment\r\nHello\tBonjour\r\nTwo\\nLines\tDeux\\nlignes\r\nTab\\tx\tT\\\\y\r\n\r\nbad line without tab\r\n\tno key\r\nEmpty\t\r\n"));
    CHECK(t.languageName() == "Fran\xC3\xA7" "ais" && t.size() == 3);
    CHECK(*t.find("Hello") == "Bonjour" && *t.find("Two\nLines") == "Deux\nlignes" && *t.find("Tab\tx") == "T\\y");
    CHECK(t.find("Nothing") == nullptr && t.translate("Nothing") == "Nothing" && t.find("Empty") == nullptr);
    CHECK(!t.parse("# only comments\n"));

    // the real translation files
    const std::string dir = "../translations";
    const auto langs = findLanguages(dir);
    CHECK(langs.size() == 4);
    bool hasKo = false;
    for (const auto& l : langs) {
        TranslationTable tt;
        CHECK(tt.load(l.path) && tt.size() > 150);
        if (l.code == "ko") { hasKo = true; CHECK(l.name == "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4"); CHECK(tt.find("Bounce in Place") != nullptr); }
        CHECK(tt.find("Chord Trigger…") != nullptr);                           // the newest strings are in every language
        for (const char* key : {"%1 copy", "Marker %1"}) { const std::string* v = tt.find(key); CHECK(v && v->find("%1") != std::string::npos); }   // placeholders survive
    }
    CHECK(hasKo);
    CHECK(pickLanguage(langs, "ko_KR") == "ko" && pickLanguage(langs, "zh_TW") == "zh_CN" && pickLanguage(langs, "ja") == "ja"
          && pickLanguage(langs, "es_MX") == "es" && pickLanguage(langs, "de_DE") == "" && pickLanguage(langs, "zh_CN") == "zh_CN");
    CHECK(findLanguages("/nonexistent_dir_xyz").empty());

    // the Session translates its messages with a table when there is no hook
    auto table = std::make_shared<TranslationTable>();
    table->parse("This track has no regions to bounce.\tKeine Regionen zum Bouncen.\n%1 copy\t%1 Kopie\n");
    Session s; s.setTranslations(table);
    std::string heard;
    Session::Hooks h; h.message = [&](const std::string& m) { heard = m; }; s.setHooks(h);
    while (s.trackCount() > 0) s.removeTrack(0);
    int pt = s.addTrack(TrackType::Instrument, "Piano");
    CHECK(s.bounceTrackInPlace(pt) == -1 && heard == "Keine Regionen zum Bouncen.");
    s.duplicateTrack(pt); CHECK(s.snapshot()[1].name == "Piano Kopie");
}

int main() {
    testDevices(); testOutputStream(); testRecording(); testDecode(); testI18n();
    std::printf("platform fails=%d\n", fails);
    return fails;
}
