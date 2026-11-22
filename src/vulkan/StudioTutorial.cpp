// MiniDAW Vulkan interface: the built-in tutorial (Help > Tutorial...). Nineteen short pages in a modal window.
// The texts are plain English: Studio::tr translates them through translations/*.tsv like every other text.
#include <algorithm>
#include <cstdio>
#include "Icons.h"
#include "Studio.h"
#include "Theme.h"

namespace vkui {
using namespace theme;

namespace {

struct TutorialPage {
    const char* title;
    std::vector<const char*> lines;   // one paragraph per entry (wrapped when drawn)
    const char* tryCommand;           // a command label the "Try it" button runs (nullptr: no button)
};

const std::vector<TutorialPage>& pages() {
    static const std::vector<TutorialPage> p = {
        {"Welcome to MiniDAW",
         {"MiniDAW is a light, fast music studio: record, sequence, edit and mix in one window.",
          "This tutorial has 19 short pages. Use the Next and Back buttons or the Left and Right arrow keys. Esc closes it, and you can open it again any time from Help > Tutorial.",
          "You do not need to remember anything: Ctrl+K opens a palette that finds every command by name."},
         nullptr},
        {"The window at a glance",
         {"The menu bar and the transport (play, record, tempo, position) are at the top.",
          "The big area in the middle is the arrangement: track headers on the left, the timeline with its regions on the right.",
          "The browser (loops, instruments, effects) and the inspector sit at the sides, and the bottom panel switches between Mixer, Editor, Steps, Devices, Snapshots, Automation and Drummer."},
         nullptr},
        {"Playing and moving around",
         {"Space starts and stops playback. Click the ruler to move the playhead.",
          "Scroll with the mouse wheel to move through the song and hold Ctrl while scrolling to zoom. The Zoom to Fit Project command shows the whole song.",
          "Turn Follow Playhead on to keep the playhead in view while the song plays."},
         nullptr},
        {"Adding tracks",
         {"Use the Track menu or the + Add Track button to add a track. There are four kinds: Audio (recordings and loops), Instrument (MIDI with a synth), Drums (a step-sequenced drum machine) and Drummer (a drummer that plays for you).",
          "Click a track header to select it. Right-click it for more choices: rename, colour, duplicate or delete."},
         nullptr},
        {"Getting sound quickly: the browser",
         {"The browser on the right lists loops, instruments and effects. Type in the search box to filter them.",
          "Drag a loop onto the arrangement to place it, drag an instrument onto a track to change its sound, or drag an effect onto a track to add it to its chain.",
          "Clicking an item previews it, so you can listen before you drag."},
         nullptr},
        {"The Drummer: instant drums",
         {"Add a Drummer track and double-click its lane to create a region. A drummer starts playing at once.",
          "Open the Drummer tab (Ctrl+7) to choose a style and change the Complexity, Loudness, Swing and Fills. Roll New Variation gives you a new pattern with the same settings.",
          "Every change can be undone with Ctrl+Z."},
         "Show Drummer"},
        {"Recording",
         {"Select a track and arm it for recording, then press R. Audio tracks record from your audio input, instrument tracks record what you play on a MIDI keyboard.",
          "No MIDI keyboard? Musical typing turns the computer keys A W S E D F T G Y H U J K into piano keys, and Z and X change the octave.",
          "The metronome and the count-in are in the transport bar. Capture MIDI saves what you played even when you forgot to press record."},
         nullptr},
        {"Editing regions: the five tools",
         {"Press 1 for the Pointer (select and move), 2 for the Pencil (draw), 3 for the Scissors (cut), 4 for the Eraser and 5 to Mute.",
          "With the Pointer, drag a region to move it, drag its right edge to change its length and hold Alt while dragging to copy it.",
          "Snap keeps everything on the grid. Press G to turn it on or off."},
         nullptr},
        {"Working with several regions",
         {"Shift-click or Ctrl-click adds regions to the selection. Drag on an empty part of a lane to draw a rubber band, or press Ctrl+A to select everything.",
          "Drag any selected region to move the whole group, even onto other tracks. Alt-drag duplicates the group.",
          "Cut, copy, paste, delete, mute and ripple delete all work on the whole selection, and each one is a single undo step."},
         nullptr},
        {"The piano roll",
         {"Select a MIDI region and press E to open the Editor tab. Draw notes with the Pencil, move them by dragging, and resize them at their edges.",
          "The velocity of each note is shown by its colour. Fold hides the keys you do not use, Highlight marks the notes of a scale, and ghost notes show the other regions of the track.",
          "Stamps place chords, and the Riff Machine writes a new melody in a scale. Q quantizes the selected notes."},
         "Show Editor"},
        {"The step sequencer",
         {"A Drums track has a step grid instead of a piano roll. Click a step to switch it on, or drag across many steps to paint them.",
          "Each row is one drum sound. Drag in the velocity strip to make single hits louder or softer.",
          "Open it with Ctrl+3."},
         "Show Step Sequencer"},
        {"The mixer and effects",
         {"The Mixer (Ctrl+1) has one strip per track: a fader for the volume, a pan control, mute and solo, two sends (reverb and echo) and a level meter. Narrow makes the strips thinner to fit more of them.",
          "The Devices tab (Ctrl+4) shows the effect chain of the selected track as cards with knobs. Use + Add Effect to insert an effect, and drag a card to change the order."},
         "Show Mixer"},
        {"Automation and tempo",
         {"The Automation tab (Ctrl+6) draws a curve for the volume, pan or a send of the selected track. Click to add a point, drag to move it and right-click to delete it.",
          "Choose Tempo (BPM) to draw tempo changes for the whole song. Double-click a point to let the tempo glide to the next one.",
          "The curve is followed during playback."},
         "Show Automation"},
        {"Snapshots and workspaces",
         {"A mixer snapshot remembers all levels, pans, sends, mutes and solos. Save one with Ctrl+Shift+Enter and bring it back with one click or Alt+1 to Alt+9. Open the tab with Ctrl+5.",
          "Workspaces change the whole layout with one key: F5 is for arranging, F6 for mixing and F7 for editing."},
         "Show Snapshots"},
        {"Mixer groups",
         {"Tracks in the same mixer group move together: faders, pans, sends, mute and solo. A drum kit recorded on six tracks is the classic example.",
          "In the mixer, click the small circle at the top of a channel strip and choose a group from A to H, or right-click a track in the arrangement and use Mixer Group. Every group has its own colour.",
          "Hold Alt while you change a control to change only that track. Ctrl+K and typing group b also puts the selected track in group B."},
         "Show Mixer"},
        {"Bringing files in",
         {"Drag an audio or MIDI file onto the window, or use File > Open. MiniDAW reads WAV, AIFF, MP3, M4A and FLAC audio and Standard MIDI Files.",
          "File > Import MIDI File asks what to do with the tempo map of the file, and whether a track that uses several channels becomes one track per channel.",
          "A Logic Pro project is a folder ending in .logicx: File > Import Logic Pro Project Folder brings in its tempo, markers, track names, audio regions and MIDI regions. Plug-ins, automation and the mixer are not read."},
         "Import MIDI File..."},
        {"Exporting for other DAWs",
         {"Bounce / Export Audio (Ctrl+B) writes WAV, AIFF or MP3 from 44.1 to 96 kHz. MP3 files get title, artist and album tags.",
          "Tick One file for every track to export stems. All files start at the same point and have the same length, so they line up at bar 1 in Logic Pro, Ableton Live or Cubase.",
          "Switch off Stems: keep master volume and limiter when the other program should do its own master processing. File > Export MIDI File also writes your tempo map, time signatures and markers."},
         "Bounce / Export Audio..."},
        {"Preferences and handy extras",
         {"Preferences (Ctrl+,) chooses the audio output and input, the buffer size, the language, the interface scale and the icon style. A smaller buffer lowers the delay, a larger one is safer on a slow computer.",
          "For another program you can choose the MIDI channel of an instrument track: select the track and use MIDI Ch in the inspector. Auto numbers the channels for you, and the drums always use channel 10.",
          "The layout of the window is remembered for the next start. Ctrl+K finds any command by name, also the ones that have no menu entry."},
         "Preferences..."},
        {"Saving, exporting and what next",
         {"Save your project with Ctrl+S. MiniDAW also keeps an automatic backup and offers to recover it if the program closes unexpectedly. Undo (Ctrl+Z) and Redo (Ctrl+Shift+Z) go back as far as you need.",
          "Use Bounce / Export Audio to render the song to a WAV file.",
          "Remember the two helpers: Ctrl+K finds any command by name, and F1 shows every keyboard shortcut. Hover over a control and the status bar at the bottom explains it.",
          "Start with a Drummer, add a few loops from the browser, record an idea, and enjoy making music."},
         "Command Palette..."},
    };
    return p;
}

// Word-wraps 's' into lines no wider than 'w'.
std::vector<std::string> wrapText(DrawList& dl, const std::string& s, float w, TextStyle st) {
    std::vector<std::string> out;
    std::string line, word;
    auto flush = [&] {
        if (word.empty()) return;
        const std::string trial = line.empty() ? word : line + " " + word;
        if (!line.empty() && dl.textWidth(trial, st) > w) { out.push_back(line); line = word; }
        else line = trial;
        word.clear();
    };
    for (char ch : s) { if (ch == ' ') flush(); else word += ch; }
    flush();
    if (!line.empty()) out.push_back(line);
    return out;
}

}  // namespace

int Studio::tutorialPageCount() { return int(pages().size()); }

void Studio::openTutorial(int page) {
    m_tutOpen = true;
    m_tutPage = std::clamp(page, 0, tutorialPageCount() - 1);
}

void Studio::drawTutorial() {
    Gui& g = *m_g;
    DrawList& dl = *m_dl;
    const auto& pg = pages();
    const int n = int(pg.size());
    m_tutPage = std::clamp(m_tutPage, 0, n - 1);
    const TutorialPage& page = pg[size_t(m_tutPage)];
    dl.rect({0, 0, dl.width(), dl.height()}, withAlpha(rgb(0, 0, 0), 160));
    const float pw = std::min(680.f, dl.width() - 40), ph = std::min(470.f, dl.height() - 40);
    const Rect panel{(dl.width() - pw) / 2, (dl.height() - ph) / 2, pw, ph};
    dl.rrect(panel, 12, bg1);
    dl.rrectStroke(panel, 12, 1, border);
    g.setInputEnabled(true);
    // header
    char buf[48];
    std::snprintf(buf, sizeof buf, "%s  -  %d / %d", tr("Tutorial").c_str(), m_tutPage + 1, n);
    dl.text(panel.x + 24, panel.y + 16, buf, textMute, small);
    if (g.iconButton(id("tut-close"), {panel.right() - 38, panel.y + 12, 24, 24}, icons::minus, tr("Close the tutorial (Esc)"))) m_tutOpen = false;
    dl.text(panel.x + 24, panel.y + 40, tr(page.title), accent, big);
    dl.hline(panel.x + 24, panel.right() - 24, panel.y + 74, line);
    // body
    float y = panel.y + 90;
    const float bw = panel.w - 48;
    for (const char* para : page.lines) {
        for (const std::string& ln : wrapText(dl, tr(para), bw, normal)) {
            dl.text(panel.x + 24, y, ln, text, normal);
            y += 20;
        }
        y += 10;
    }
    // page dots
    const float dotY = panel.bottom() - 74;
    const float dotsW = float(n) * 18.f;
    for (int i = 0; i < n; ++i) {
        const Rect hit{panel.x + (panel.w - dotsW) / 2 + float(i) * 18.f, dotY, 18, 18};
        const Interact it = g.interact(id("tut-dot", i), hit, false);
        dl.circle(hit.x + 9, hit.y + 9, i == m_tutPage ? 5.f : (it.hovered ? 4.5f : 3.5f), i == m_tutPage ? accent : textMute);
        if (it.clicked) m_tutPage = i;
        if (it.hovered) g.setCursor(Cursor::Hand);
    }
    // footer buttons
    const float by = panel.bottom() - 44;
    if (g.button(id("tut-back"), {panel.x + 24, by, 96, 30}, tr("Back"), ButtonKind::Normal, m_tutPage > 0)) --m_tutPage;
    const bool last = m_tutPage == n - 1;
    if (g.button(id("tut-next"), {panel.right() - 24 - 110, by, 110, 30}, tr(last ? "Done" : "Next"), ButtonKind::Accent)) {
        if (last) m_tutOpen = false; else ++m_tutPage;
    }
    if (page.tryCommand && g.button(id("tut-try"), {panel.right() - 24 - 110 - 8 - 110, by, 110, 30}, tr("Try it"))) {
        m_tutOpen = false;
        runCommand(page.tryCommand);
    }
    if (g.key(KeyEscape, 0, false, true)) m_tutOpen = false;
    else if (g.key(KeyRight, 0, false, true) && m_tutPage < n - 1) ++m_tutPage;
    else if (g.key(KeyLeft, 0, false, true) && m_tutPage > 0) --m_tutPage;
}

}  // namespace vkui
