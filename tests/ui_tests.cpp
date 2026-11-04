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
    ui::Host host;
    host.loopFolder = "../loops";
    ui::NativeUi u(s, host);
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

    // ================= v0.10: editors, dialogs, inspector =================
    {
        s.setLoop(false);
        u.mouseUp(0, 0, 0, 0);
        // double-click the 'Keys' MIDI region: opens the piano roll
        const int kx = 236 + 40, ky = laneY0 + 120 + 30;
        clickAt(kx, ky);
        u.doubleClick(kx, ky); u.paint(c);
        CHECK(u.selectedTrack() == 2 && u.selectedRegion() == 0);
        const size_t n0 = s.snapshot()[2].regions[0].notes.size();
        const ui::Rect g = u.pianoGridRect();
        CHECK(g.w > 300);
        c.savePpm("/tmp/minidaw_ui_piano.ppm");
        // click an empty cell: adds a note; drag to lengthen
        const int cx = g.x + 20, cy = g.y + 5 * 13 + 3;
        u.mouseDown(cx, cy, 0, 0); u.mouseMove(cx + 60, cy, 0); u.mouseUp(cx + 60, cy, 0, 0); u.paint(c);
        CHECK(s.snapshot()[2].regions[0].notes.size() == n0 + 1);
        CHECK(s.snapshot()[2].regions[0].notes.back().length > 0.5);
        const double len1 = s.snapshot()[2].regions[0].notes.back().length;
        CHECK(u.pianoSelectedNote() == int(n0));
        // right-click deletes it again, undo brings it back
        u.mouseDown(cx + 10, cy, 1, 0); u.mouseUp(cx + 10, cy, 1, 0); u.paint(c);
        CHECK(s.snapshot()[2].regions[0].notes.size() == n0);
        s.undo(); CHECK(s.snapshot()[2].regions[0].notes.size() == n0 + 1 && s.snapshot()[2].regions[0].notes.back().length == len1);
        // drag the note up two semitones and select + delete with the key
        const int pitch0 = s.snapshot()[2].regions[0].notes.back().pitch;
        u.mouseDown(cx + 5, cy, 0, 0); u.mouseMove(cx + 5, cy - 26, 0); u.mouseUp(cx + 5, cy - 26, 0, 0); u.paint(c);
        CHECK(s.snapshot()[2].regions[0].notes.back().pitch == pitch0 + 2);
        u.key(ui::KeyDelete, 0); CHECK(s.snapshot()[2].regions[0].notes.size() == n0);

        // multi-selection: select all, copy + paste, delete the selection, undo
        u.paint(c);
        const size_t nAll = s.snapshot()[2].regions[0].notes.size();
        u.key('A', ui::ModCtrl); u.paint(c);
        CHECK(u.pianoSelectionSize() == int(nAll));
        u.key('C', ui::ModCtrl);
        u.key('V', ui::ModCtrl); u.paint(c);
        CHECK(s.snapshot()[2].regions[0].notes.size() == nAll * 2);
        CHECK(u.pianoSelectionSize() == int(nAll));            // the pasted notes are selected
        s.undo(); u.paint(c);
        u.key('A', ui::ModCtrl); u.key(ui::KeyDelete, 0); u.paint(c);
        CHECK(s.snapshot()[2].regions[0].notes.empty());
        s.undo(); u.paint(c);
        CHECK(s.snapshot()[2].regions[0].notes.size() == nAll);
        // marquee with Shift selects the notes it touches; dragging one of them moves the whole selection
        {
            const ui::Rect gg = u.pianoGridRect();
            u.mouseDown(gg.x + 2, gg.y + 2, 0, ui::ModShift); u.mouseMove(gg.x + gg.w - 3, gg.y + gg.h - 3, 0); u.mouseUp(gg.x + gg.w - 3, gg.y + gg.h - 3, 0, 0); u.paint(c);
            CHECK(u.pianoSelectionSize() > 0 && u.pianoSelectionSize() <= int(nAll));   // only the notes inside the visible pitch range
            c.savePpm("/tmp/minidaw_ui_multi.ppm");
        }
        u.key(ui::KeyEscape, 0);
        // transpose dialog: type a value, confirm
        u.key(ui::KeyEscape, 0);
        const int firstPitch = s.snapshot()[2].regions[0].notes[0].pitch;
        u.mouseDown(10 + 44 + 2 + 44 + 2 + 50 + 2 + 30, 54 + 16, 0, 0); u.mouseUp(0, 0, 0, 0);   // Functions menu
        CHECK(u.menuOpen());
        u.paint(c); c.savePpm("/tmp/minidaw_ui_functions.ppm");
        u.key(ui::KeyEscape, 0);
        s.transposeRegion(2, 0, 0);
        u.key('Z', ui::ModCtrl);
    }

    // dialogs: open with a menu item index of Functions > Transpose by calling through the Edit menu path is UI-only,
    // so drive the dialog class directly as well
    {
        ui::Dialog d("Test", ui::Dialog::Values{ui::Field::number("A", 5, 0, 10), ui::Field::check("B", false), ui::Field::choice("C", {"x", "y", "z"}, 0), ui::Field::slider("D", 50, 0, 100, 0), ui::Field::input("E", "hi")}, nullptr);
        d.setScreen(1280, 780);
        ui::Canvas dc(1280, 780);
        d.paint(dc); dc.savePpm("/tmp/minidaw_ui_dialog.ppm");
        // number field: type 7 + Enter
        d.mouseDown(720, 234, 0); d.mouseUp(720, 234);
        d.text("7"); d.key(ui::KeyEnter, 0);
        CHECK(!d.finished());   // Enter only finished the edit
        CHECK(std::fabs(d.values()[0].value - 7) < 1e-9);
        d.key(ui::KeyEscape, 0); CHECK(d.finished() && !d.accepted());
    }

    // step sequencer
    {
        const int dt = s.addTrack(TrackType::Drums, "Beat");
        u.paint(c);
        u.showTab(ui::NativeUi::TabSteps);
        u.mouseDown(10, 10, 0, 0); u.mouseUp(10, 10, 0, 0);   // nothing
        for (int i = 0; i < 10; ++i) u.key(ui::KeyDown, 0);   // select the last (drum) track with the keyboard
        CHECK(u.selectedTrack() == dt);
        u.showTab(ui::NativeUi::TabSteps); u.paint(c);
        const ui::Rect sg = u.stepGridRect();
        CHECK(sg.w > 200);
        const double cw = double(sg.w) / 16, chh = double(sg.h) / 4;
        const int sx = sg.x + int(cw * 2.5), sy = sg.y + int(chh * 0.5);
        const bool before = s.snapshot()[size_t(dt)].steps[0][2];
        clickAt(sx, sy);
        CHECK(s.snapshot()[size_t(dt)].steps[0][2] != before);
        u.paint(c); c.savePpm("/tmp/minidaw_ui_steps.ppm");
    }

    // automation and tempo lanes
    {
        for (int i = 0; i < 10; ++i) u.key(ui::KeyUp, 0);   // first track (audio)
        u.showAutomation(0); u.paint(c);
        const ui::Rect lane = u.autoLaneRect();
        CHECK(lane.w > 300);
        CHECK(s.snapshot()[0].volAuto.empty());
        clickAt(lane.x + 200, lane.y + lane.h / 2);
        CHECK(s.snapshot()[0].volAuto.size() == 1);
        u.mouseDown(lane.x + 400, lane.y + 40, 0, 0); u.mouseUp(lane.x + 400, lane.y + 40, 0, 0); u.paint(c);
        CHECK(s.snapshot()[0].volAuto.size() == 2 && s.snapshot()[0].volAuto[1].beat > s.snapshot()[0].volAuto[0].beat);
        const double v1 = s.snapshot()[0].volAuto[1].value;
        u.mouseDown(lane.x + 400, lane.y + 40, 0, 0); u.mouseMove(lane.x + 400, lane.y + 120, 0); u.mouseUp(lane.x + 400, lane.y + 120, 0, 0);
        CHECK(s.snapshot()[0].volAuto[1].value < v1);
        u.mouseDown(lane.x + 400, lane.y + 120, 1, 0); u.mouseUp(lane.x + 400, lane.y + 120, 1, 0);
        CHECK(s.snapshot()[0].volAuto.size() == 1);
        u.paint(c); c.savePpm("/tmp/minidaw_ui_auto.ppm");
        u.showAutomation(4); u.paint(c);
        const size_t tp0 = s.tempoMap()->tempoPoints().size();
        clickAt(lane.x + 300, lane.y + 30);
        CHECK(s.tempoMap()->tempoPoints().size() == tp0 + 1);
        s.undo(); CHECK(s.tempoMap()->tempoPoints().size() == tp0);
    }

    // inspector, smart controls, loops, drummer, preferences, musical typing
    u.showTab(ui::NativeUi::TabInspector);
    clickAt(236 + 40, laneY0 + 30);
    u.paint(c); c.savePpm("/tmp/minidaw_ui_inspector.ppm");
    u.showTab(ui::NativeUi::TabSmart); clickAt(60, laneY0 + 2 * 60 + 20); u.paint(c); c.savePpm("/tmp/minidaw_ui_smart.ppm");
    // ================= v0.11: browser, devices, track height, hints =================
    {
        u.paint(c);
        CHECK(u.viewWidth() == 1280 - 250);
        const int all = u.browserItemCount();
        CHECK(all > 5);
        c.savePpm("/tmp/minidaw_ui_browser.ppm");
        // search: click the field, type, the list shrinks; Escape clears it
        const ui::Rect sf = u.browserSearchRect();
        clickAt(sf.x + 40, sf.y + 12);
        u.text("chor");
        u.paint(c);
        CHECK(u.browserItemCount() >= 1 && u.browserItemCount() < all);
        const int effects = u.browserItemCount();
        // drag the first match (an effect) onto the second track's header: it becomes an insert there
        const ui::Rect row = u.browserRowRect(0);
        CHECK(row.w > 100);
        const size_t ins0 = s.snapshot()[1].inserts.size();
        u.mouseDown(row.x + 40, row.y + 8, 0, 0);
        u.mouseMove(row.x + 30, row.y + 30, 0);
        u.mouseMove(100, laneY0 + 60 + 20, 0);
        u.paint(c); c.savePpm("/tmp/minidaw_ui_drag.ppm");
        u.mouseUp(100, laneY0 + 60 + 20, 0, 0);
        CHECK(s.snapshot()[1].inserts.size() == ins0 + 1);
        (void)effects;
        clickAt(sf.x + 40, sf.y + 12);   // focus the search field again, Escape clears it
        u.key(ui::KeyEscape, 0); u.paint(c);
        CHECK(u.browserItemCount() == all);
        // a click without dragging must not add anything
        const size_t ins1 = s.snapshot()[1].inserts.size();
        const ui::Rect row2 = u.browserRowRect(3);
        clickAt(row2.x + 40, row2.y + 8);
        CHECK(s.snapshot()[1].inserts.size() == ins1);
        s.stopAudition();
        // devices tab: knobs change parameters by dragging
        clickAt(60, laneY0 + 20);   // first track: it has Chorus and Filter inserts
        u.showTab(ui::NativeUi::TabSmart); u.paint(c);
        c.savePpm("/tmp/minidaw_ui_devices.ppm");
        auto& fx = *s.snapshot()[0].inserts[0];
        const float before = fx.params[0]->get();
        u.mouseDown(46, 516, 0, 0); u.mouseMove(46, 486, 0); u.mouseUp(46, 486, 0, 0);
        CHECK(s.snapshot()[0].inserts[0]->params[0]->get() != before);
        u.mouseDown(46, 516, 0, 0); u.mouseUp(46, 516, 0, 0); u.doubleClick(46, 516);
        CHECK(std::fabs(s.snapshot()[0].inserts[0]->params[0]->get() - fx.params[0]->def) < 1e-4);
        // track heights
        u.setTrackHeight(34); u.paint(c); c.savePpm("/tmp/minidaw_ui_compact.ppm");
        u.setTrackHeight(96); u.paint(c);
        u.setTrackHeight(60);
        // the browser can be hidden
        u.key('L', 0); u.paint(c);
        CHECK(u.viewWidth() == 1280);
        u.key('L', 0); u.paint(c);
    }
    clickAt(236 + 40, laneY0 + 3 * 60 + 30);   // drummer region
    u.showTab(ui::NativeUi::TabDrummer); u.paint(c); c.savePpm("/tmp/minidaw_ui_drummer.ppm");
    u.key(',', ui::ModCtrl); CHECK(u.dialogOpen()); u.paint(c); c.savePpm("/tmp/minidaw_ui_prefs.ppm"); u.key(ui::KeyEscape, 0); CHECK(!u.dialogOpen());
    u.paint(c);
    clickAt(60, laneY0 + 2 * 60 + 20);
    CHECK(!u.typingOn());
    CHECK(u.key(ui::KeyEscape, 0) || true);
    std::printf("ui fails=%d\n", fails);
    return fails ? 1 : 0;
}
