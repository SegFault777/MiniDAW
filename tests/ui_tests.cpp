// Tests and screenshots of the Qt-free user interface (src/ui) on a fake project, rendered headless.
#include <cmath>
#include <cstdio>
#include <string>
#include "../src/core/Files.h"
#include "../src/core/Session.h"
#include "../src/ui/Ui.h"
int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
using namespace core;

static std::string wav(const char* name, double secs) {
    std::vector<float> pcm(size_t(secs * 44100) * 2);
    for (size_t i = 0; i < pcm.size() / 2; ++i) {
        const double t = double(i) / 44100.0, env = 0.15 + 0.85 * std::fabs(std::sin(t * 3.1)) * std::exp(-std::fmod(t, 0.5) * 3.0);
        pcm[2 * i] = pcm[2 * i + 1] = float(env * std::sin(t * 2 * 3.14159 * 220 + 3 * std::sin(t * 17)));
    }
    const std::string p = std::string("/tmp/minidaw_ui_") + name;
    std::string e; writeWavFile(p, pcm, &e);
    return p;
}

static void buildDemo(Session& s) {
    while (s.trackCount() > 0) s.removeTrack(0);
    s.setBpm(112);
    const int vox = s.addTrack(TrackType::Audio, "Lead Vocal");
    s.importAudio(wav("vox.wav", 14), vox, 0);
    const int voxb = s.addTrack(TrackType::Audio, "Vocal Double");
    s.importAudio(wav("vox2.wav", 8), voxb, 4);
    const int keys = s.addTrack(TrackType::Instrument, "Keys");
    int r = s.addMidiRegion(keys, 0, 8);
    std::vector<Note> notes;
    const int chords[4][3] = {{60, 64, 67}, {57, 60, 64}, {53, 57, 60}, {55, 59, 62}};
    for (int bar = 0; bar < 8; ++bar) for (int k = 0; k < 3; ++k) notes.push_back({chords[bar % 4][k] + (k == 2 ? 12 : 0), bar + 0.0, 1.6, 90});
    for (int i = 0; i < 16; ++i) notes.push_back({72 + (i * 5) % 9, i * 0.5, 0.4, 70 + (i % 4) * 10});
    s.setRegionNotes(keys, r, notes);
    s.addMidiRegion(keys, 12, 4);
    const int drm = s.addTrack(TrackType::Drummer, "Drummer");
    s.addDrummerRegion(drm, 0, 4);
    s.addDrummerRegion(drm, 16, 4);
    const int bass = s.addTrack(TrackType::Instrument, "Bass");
    r = s.addMidiRegion(bass, 4, 12);
    std::vector<Note> bn;
    for (int i = 0; i < 24; ++i) bn.push_back({36 + (i % 3) * 3 + (i % 8 == 7 ? 7 : 0), i * 0.5, 0.45, 100});
    s.setRegionNotes(bass, r, bn);
    s.setVolume(vox, 0.9f); s.setVolume(keys, 0.62f); s.setPan(keys, -0.4f); s.setPan(bass, 0.25f);
    s.setMute(voxb, true); s.setSolo(drm, false); s.setArmed(vox, true);
    s.addInsert(vox, "chorus"); s.addInsert(vox, "filter");
}

int main(int argc, char** argv) {
    Session s;
    buildDemo(s);
    ui::NativeUi u(s);
    u.resize(1280, 780);
    ui::Canvas c(1280, 780);
    u.paint(c);
    CHECK(u.selectedTrack() == -1);
    c.savePpm(argc > 1 ? argv[1] : "/tmp/minidaw_ui.ppm");

    // -- transport buttons: clicking Play starts, clicking again pauses
    auto clickAt = [&](int x, int y, int b = 0) { u.mouseDown(x, y, b, 0); u.mouseUp(x, y, b, 0); u.paint(c); };
    const int playX = 1280 / 2 - 290 + 40 + 40 + 22, playY = 27;
    clickAt(playX, playY); CHECK(s.isPlaying());
    clickAt(playX, playY); CHECK(!s.isPlaying());

    // -- select and drag a region (track 0 lane, region at beat 0)
    const int laneY0 = 54 + 32 + 26;
    clickAt(236 + 40, laneY0 + 30);
    CHECK(u.selectedTrack() == 0 && u.selectedRegion() == 0);
    // drag the 'Vocal Double' region (track 1, starts at beat 4, 40 px per beat) by 2 beats
    const int x4 = 236 + 4 * 40 + 30, y1 = laneY0 + 60 + 30;
    u.mouseDown(x4, y1, 0, 0); u.mouseMove(x4 + 40, y1, 0); u.mouseMove(x4 + 80, y1, 0); u.mouseUp(x4 + 80, y1, 0, 0);
    CHECK(std::fabs(s.snapshot()[1].regions[0].start - 6.0) < 1e-9);
    s.undo(); CHECK(std::fabs(s.snapshot()[1].regions[0].start - 4.0) < 1e-9);

    // -- mute / solo buttons in the header, fader double-click, ruler seek
    const int mx = 236 - 3 * 27 - 6 + 12, my = laneY0 + 17;
    clickAt(mx, my); CHECK(s.snapshot()[0].mute); clickAt(mx, my); CHECK(!s.snapshot()[0].mute);
    u.mouseDown(236 + 200, 54 + 32 + 10, 0, 0); u.mouseUp(236 + 200, 54 + 32 + 10, 0, 0);
    CHECK(std::fabs(s.positionBeats() - 5.0) < 0.01);

    // -- keyboard
    CHECK(u.key(' ', 0) && s.isPlaying()); u.key(' ', 0); CHECK(!s.isPlaying());
    u.key('X', 0); CHECK(!u.mixerVisible()); u.key('X', 0); CHECK(u.mixerVisible());
    clickAt(236 + 40, laneY0 + 30);
    const size_t before = s.snapshot()[0].regions.size();
    u.key(ui::KeyDelete, 0); CHECK(s.snapshot()[0].regions.size() == before - 1);
    u.key('Z', ui::ModCtrl); CHECK(s.snapshot()[0].regions.size() == before);

    // -- menus
    clickAt(10 + 44 + 2 + 44 + 2 + 20, 54 + 16);   // Functions
    CHECK(u.menuOpen());
    u.key(ui::KeyEscape, 0); CHECK(!u.menuOpen());
    clickAt(60, 54 + 16);   // Edit
    CHECK(u.menuOpen());
    c.savePpm("/tmp/minidaw_ui_menu.ppm");
    u.key(ui::KeyEscape, 0);

    // -- region functions through the menu actions (MIDI region on 'Keys')
    clickAt(236 + 40, laneY0 + 120 + 30);
    CHECK(u.selectedTrack() == 2 && u.selectedRegion() == 0);
    u.paint(c);
    c.savePpm("/tmp/minidaw_ui_sel.ppm");
    std::printf("ui fails=%d\n", fails);
    return fails ? 1 : 0;
}
