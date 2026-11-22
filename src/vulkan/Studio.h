// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Vulkan UI, layer 3: the DAW itself. Studio draws every screen (transport, arrangement, mixer, editors, browser, inspector,
// dialogs, command palette) with the immediate-mode Gui and talks to core::Session. All commands live in one registry, which
// feeds the menus, the keyboard shortcuts, the command palette (Ctrl+K) and the shortcut sheet (F1).
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "../core/I18n.h"
#include "../core/MidiOps.h"
#include "../core/Session.h"
#include "Gui.h"

namespace vkui {

struct PluginEntry {
    std::string id, name, vendor;
    bool instrument = false;
};

/// Everything the program around the UI provides (file dialogs, devices, settings, plug-ins).
struct Host {
    std::function<std::string(const std::string& title, const std::string& filter, bool save)> fileDialog;   // filter "Name|*.a;*.b"
    std::function<std::string(const std::string& title)> folderDialog;
    std::function<void()> quit;
    std::function<void(const std::string&)> setTitle;
    std::string loopFolder = "loops";
    std::map<std::string, std::string> settings;
    std::function<std::vector<std::pair<std::string, std::string>>(bool input)> audioDevices;   // (id, name); id "" = system default
    std::function<std::vector<std::string>()> midiDevices;
    std::function<bool()> devicesReady;     // optional: false while the audio / MIDI device lists are still being read in the background
    std::function<void()> refreshDevices;   // optional: read the lists again in the background
    std::function<std::vector<PluginEntry>()> plugins;
    std::function<void()> rescanPlugins;
    std::function<bool(const std::shared_ptr<dsp::ParamSet>&)> openPluginEditor;
    std::function<std::vector<std::pair<std::string, std::string>>()> languages;
    std::function<void(const std::map<std::string, std::string>&)> applySettings;
    std::function<void(const std::map<std::string, std::string>&)> storeSettings;   // merge + save a few keys (the layout) without re-applying the audio settings
    std::string dataDir;
    std::vector<std::string> iconFontDirs;   // where the Material Symbols fonts are looked for (see IconFont.h); empty = leave as set
    bool portable = false;
};

struct Command {
    std::string category, label, shortcut;   // shortcut as shown ("Ctrl+Z")
    int key = 0, mods = 0;                   // 0 = no keyboard shortcut
    std::function<void()> run;
    std::function<bool()> enabled;
    std::function<bool()> checked;
};

/// One row of a parameter dialog.
struct DlgField {
    enum Kind { Number, Slider, Check, Choice, Text, Label } kind = Number;
    std::string label;
    double value = 0, min = 0, max = 100, step = 1;
    int decimals = 0;
    std::string suffix;
    bool checked = false;
    std::vector<std::string> choices;
    int index = 0;
    std::string text;
    static DlgField number(std::string l, double v, double lo, double hi, double step = 1, int dec = 0, std::string suffix = {});
    static DlgField slider(std::string l, double v, double lo, double hi, int dec = 2, std::string suffix = {});
    static DlgField check(std::string l, bool on);
    static DlgField choice(std::string l, std::vector<std::string> items, int index);
    static DlgField input(std::string l, std::string text);
    static DlgField note(std::string text);
};

enum class Tool { Pointer, Pencil, Scissors, Eraser, Mute };
enum class DockTab { Mixer, Editor, Steps, Devices, Snapshots, Automation, Drummer };

/// The grid choices of the toolbar and the piano roll (0 = Auto, which follows the zoom level).
inline constexpr const char* kGridNames[8] = {"Auto", "Bar", "1/2", "1/4", "1/8", "1/16", "1/32", "1/8 Triplet"};
inline constexpr double kGridBeats[8] = {0, 4, 2, 1, 0.5, 0.25, 0.125, 1.0 / 3.0};

class Studio {
public:
    Studio(core::Session& session, Host host);
    void setTranslations(std::shared_ptr<const core::TranslationTable> table) { m_table = std::move(table); }

    /// Runs one frame: handles the input collected in 'gui' and draws everything into gui's draw list.
    void frame(Gui& gui, double time);
    /// True while something moves (playback, meters, animations, a toast): the shell keeps drawing.
    bool wantsFrame() const;
    void invalidate() { m_modelDirty = true; }   // the session changed (may be called from any thread)
    void message(const std::string& text);
    void openFile(const std::string& path);
    /// Opens any supported file: a project (after the unsaved-changes question), an audio file or a MIDI file (imported into the
    /// current project). Used by File > Open, drag and drop and "open with".
    void openAny(const std::string& path);
    void importLogic(const std::string& folder);   ///< audio of a Logic Pro package (.logicx folder)
    void importAudioFile(const std::string& path);
    int selectedTrack() const { return m_selTrack; }
    bool quitRequested() const { return m_quit; }
    core::Session& session() { return m_s; }
    const std::vector<Command>& commands() const { return m_commands; }
    bool runCommand(const std::string& label);   // by label (tests, palette)
    Host& host() { return m_host; }

    // ---- state the tests and the shell read ----
    Tool tool() const { return m_tool; }
    bool dialogOpen() const { return m_dlg.open; }
    bool paletteOpen() const { return m_paletteOpen; }
    bool helpOpen() const { return m_helpOpen; }
    bool tutorialOpen() const { return m_tutOpen; }
    int tutorialPage() const { return m_tutPage; }
    static int tutorialPageCount();
    void openTutorial(int page = 0);
    DockTab dockTab() const { return m_dock; }
    int selectedRegion() const { return m_selRegion; }
    /// Automation lane: 0 volume, 1 pan, 2 send A, 3 send B, 4 project tempo.
    void setAutomationParam(int p) { m_autoParam = std::clamp(p, 0, 4); }
    Rect autoLaneRect() const { return m_autoLane; }
    /// Every selected region as (track, region): the multi-selection, or just the primary one.
    std::vector<std::pair<int, int>> selectionList() const;
    bool regionSelected(int t, int r) const;
    void selectRegion(int track, int region) { m_selTrack = track; m_selRegion = region; }
    double ppb() const { return m_ppb; }
    double snapGrid() const;
    bool followPlayhead() const { return m_follow; }
    bool browserVisible() const { return m_showBrowser; }
    bool inspectorVisible() const { return m_showInspector; }
    bool dockVisible() const { return m_showDock; }
    std::string toastText() const { return m_toast; }
    const std::vector<core::Track>& tracks() const { return m_tracks; }
    DlgField* dialogField(int i) { return i >= 0 && i < int(m_dlg.fields.size()) ? &m_dlg.fields[size_t(i)] : nullptr; }
    int dialogFieldCount() const { return int(m_dlg.fields.size()); }
    std::string dialogTitle() const { return m_dlg.title; }
    void acceptDialog() { if (m_dlg.open) finishDialog(true); }
    Rect arrangementRect() const { return m_rArr; }
    Rect dockRect() const { return m_rDock; }
    double audioEditorZoom() const { return m_aeZoom; }
    Rect audioEditorWave() const { return m_aeWave; }   // the waveform area of the audio editor (empty when it is not shown)
    int fullSnapshotCount() const { return m_fullSnapshots; }
    /// The layout the user arranged (panels, sizes, zoom, snap...): saved between sessions through Host::storeSettings.
    std::map<std::string, std::string> layoutSettings() const;
    void applyLayout(const std::map<std::string, std::string>& s);
    bool autoScrolling() const { return m_autoScrolling; }
    Rect browserRect() const { return m_rBrowser; }
    Rect inspectorRect() const { return m_rInspector; }
    Rect transportRect() const { return m_rTransport; }
    Rect lanesRect() const { return m_rLanes; }
    Rect overviewRect() const { return m_rOverview; }
    int browserTab() const { return m_browserTab; }
    int mixerFilter() const { return m_mixFilter; }
    bool mixerNarrow() const { return m_mixNarrow; }
    float peakHold(int trackId) const { auto it = m_peakHold.find(trackId); return it == m_peakHold.end() ? 0.f : it->second; }
    double scrollBeats() const { return m_scrollX; }
    std::string selectionText() const;   // Pro Tools counter: Start / End / Length of the region or cycle
    float trackHeight() const { return m_trackH; }
    double beatToX(double beat) const { return double(m_rLanes.x) + (beat - m_scrollX) * m_ppb; }
    double xToBeat(double x) const { return (x - double(m_rLanes.x)) / m_ppb + m_scrollX; }

private:
    // ---------------------------------------------------------------- layout and chrome (Studio.cpp)
    void refreshModel();
    void layout(float w, float h);
    void drawMenuBar();
    void drawTransport();
    void drawStatusBar();
    void drawOverlays();
    void handleShortcuts();
    void registerCommands();
    void addCmd(const char* category, const char* label, const char* shortcut, int key, int mods, std::function<void()> run, std::function<bool()> enabled = {}, std::function<bool()> checked = {});
    std::vector<MenuItem> menuFor(const std::string& category);
    std::vector<MenuItem> itemsFor(const std::vector<std::string>& labels);
    void drawPalette();
    void drawHelpSheet();
    void drawToast();
    const std::string& tr(const std::string& s) const;
    /// Translates 'pattern' (English, with %1) and fills in the argument.
    std::string fmt(const std::string& pattern, const std::string& a1, const std::string& a2 = std::string()) const {
        std::string r = tr(pattern);
        if (size_t i = r.find("%1"); i != std::string::npos) r.replace(i, 2, a1);
        if (size_t i = r.find("%2"); i != std::string::npos) r.replace(i, 2, a2);
        return r;
    }
    Id id(const std::string& s) { return m_g->id(s); }
    Id id(const std::string& s, int n) { return m_g->id(s, n); }
    void setHint(const std::string& s) { m_hint = s; }
    bool splitter(const std::string& name, const Rect& r, float& value, bool vertical, float lo, float hi, float sign);
    std::string positionText() const;
    std::string timeText() const;
    std::string gridName() const;

    // ---------------------------------------------------------------- arrangement (StudioArrange.cpp)
    void drawArrangement();
    void drawRuler();
    void drawOverview();   // Pro Tools overview / Ableton arrangement overview: the whole project in a thin strip
    void drawTrackHeaders();
    void drawLanes();
    void drawRegion(const core::Track& t, int ti, int ri, const Rect& lane, bool selected, bool hovered);
    void laneInteraction();
    void trackHeaderMenu(int track);
    void regionMenu(int track, int region);
    void emptyLaneMenu(int track, double beat);
    void zoomToFit();
    void zoomAt(double mouseX, double factor);
    void ensureVisible(double beat);
    double snapBeat(double beat, bool force = false) const;
    double gridBeats() const;
    float laneY(int track) const { return m_rLanes.y + float(track) * m_trackH - m_scrollY; }
    int trackAtY(float y) const;
    bool hasRegion() const;
    bool hasMidiRegion() const;
    bool hasAudioRegion() const;
    const core::Region* region() const;
    template <class F> void withRegion(F f) { if (hasRegion()) f(m_selTrack, m_selRegion); }
    void splitAtPlayhead();
    void tabTransient(bool forward);
    void addTrack(core::TrackType type);
    void deleteSelectedRegion();
    void dropPayload();
    bool hitRegion(float mx, float my, int& track, int& region, bool& rightEdge) const;
    void drawWaveform(const core::Region& r, const Rect& body, Color c, double beats);

    // ---------------------------------------------------------------- dock: mixer, devices, snapshots, inspector, browser (StudioDock.cpp)
    void drawDock();
    void drawMixer(const Rect& r);
    void drawChannel(const Rect& r, int track);
    void drawDevices(const Rect& r);
    void drawSnapshots(const Rect& r);
    void drawInspector(const Rect& r);
    void drawBrowser(const Rect& r);
    void drawProjectList(const Rect& r);
    void reloadLoops();
    struct BrowserItem { int kind; std::string id, name, sub, path; double bpm; };
    std::vector<BrowserItem> browserItems();
    void addInsertTo(int track, const std::string& id);

    // ---------------------------------------------------------------- editors (StudioEditors.cpp)
    void drawEditor(const Rect& r);
    void trimAtPlayhead(bool start);   // Region > Trim Start / End to Playhead (audio regions)
    void drawAudioEditor(const Rect& r);   // waveform of the selected audio region with draggable fade handles
    void drawPianoRoll(const Rect& r);
    void drawSteps(const Rect& r);
    void drawAutomation(const Rect& r);
    void drawDrummer(const Rect& r);
    std::vector<MenuItem> groupMenuItems(int ti);   // the Group submenu of a track: No Group, A .. H
    static Color groupColor(int group);
    bool m_drumCkpt = false;   // one undo point per slider drag
    bool m_wasRunning = false, m_peaksLive = false;
    bool m_panning = false, m_midPrev = false, m_autoScrolling = false;   // middle-mouse pan, edge scrolling while dragging
    float m_panX = 0, m_panY = 0;
    std::map<std::string, std::string> m_lastLayout;
    double m_layoutCheck = 0;
    std::uint64_t m_lastRev = ~std::uint64_t(0);
    std::vector<std::pair<float, float>> m_peakScratch;
    int m_fullSnapshots = 0;   // how many whole-model copies were taken (tests)
    void auditionNote(int track, int pitch);
    double prGridBeats() const;
    void applyToNotes(const std::function<std::vector<core::Note>(const std::vector<core::Note>&)>& fn);
    void applyWorkspace(int which);
    void rippleDelete();

    // ---------------------------------------------------------------- dialogs (StudioDialogs.cpp)
    void openDialog(const std::string& title, std::vector<DlgField> fields, std::function<void(const std::vector<DlgField>&)> onOk, const std::string& okLabel = "OK");
    void drawDialog();
    void finishDialog(bool ok);
    void dlgQuantize(); void dlgScaleQuantize(); void dlgHumanize(); void dlgChordTrigger(); void dlgArpeggiator(); void dlgTranspose();
    void dlgVelocityRamp(); void dlgScaleVelocity(); void dlgFixedLength(); void dlgStripSilence(); void dlgNormalize(); void dlgFlex(); void dlgFades();
    void dlgTempo(); void dlgTimeSignature(); void dlgBounce(); void dlgImportMidi(); void dlgPreferences(); void dlgAbout(); void dlgRenameTrack(int t); void dlgRenameRegion();
    void dlgRiff(); void dlgLogical(); void dlgGroove(); void dlgAdaptTempo(); void dlgBeatDetective(); void dlgAux(int bus); void dlgSnapshotName(int index); void rollRiff();
    void guardUnsaved(std::function<void()> proceed);
    void doFile(const std::string& what);
    void addRecent(const std::string& path);
    bool previewOnClick() const;
    void tapTempo();
    void gotoMarker(int dir);

    // ---------------------------------------------------------------- data
    core::Session& m_s;
    Host m_host;
    Gui* m_g = nullptr;
    DrawList* m_dl = nullptr;
    std::shared_ptr<const core::TranslationTable> m_table;
    std::map<std::string, std::string> m_trCache;
    std::vector<core::Track> m_tracks;
    std::vector<core::Marker> m_markers;
    std::shared_ptr<const dsp::TempoMap> m_tempo;
    std::atomic<bool> m_modelDirty{true};
    double m_time = 0, m_lastModel = -1;
    float m_meterL = 0, m_meterR = 0;
    bool m_quit = false;

    // layout
    Rect m_aeWave;
    double m_aeZoom = 1, m_aeScroll = 0;   // the audio editor shows 1 / m_aeZoom of the region, starting at m_aeScroll (0..1)
    int m_aeKey = -1;
    double m_aeFade = 0;   // the fade (frames) that an audio editor handle drag is changing
    Rect m_rOverview, m_rMenu, m_rTransport, m_rStatus, m_rBrowser, m_rInspector, m_rDock, m_rArr, m_rRuler, m_rHeaders, m_rLanes, m_rBody;
    float m_browserW = 240, m_inspectorW = 264, m_dockH = 360, m_headerW = 216, m_trackH = 72;
    bool m_showBrowser = true, m_showInspector = true, m_showDock = true;

    // arrangement view
    double m_scrollX = 0, m_ppb = 44;
    float m_scrollY = 0;
    bool m_follow = true, m_snapOn = true;
    int m_snapIdx = 0;   // 0 = auto
    Tool m_tool = Tool::Pointer;
    int m_selTrack = 0, m_selRegion = -1;
    std::vector<std::pair<int, int>> m_multi, m_rubBase;   // region multi-selection (valid only while the primary selection is unchanged)
    int m_multiT = -1, m_multiR = -1;
    void setMulti(const std::vector<std::pair<int, int>>& list, std::pair<int, int> primary);
    enum class Drag { None, MoveRegion, ResizeRegion, Create, Cycle, Seek, Pan, Marker, Erase, Rubber } m_drag = Drag::None;
    int m_dragCurTrack = 0;
    double m_dragCurStart = 0;
    float m_rubX0 = 0, m_rubY0 = 0, m_rubX1 = 0, m_rubY1 = 0;
    int m_dragTrack = -1, m_dragRegion = -1;
    double m_dragBeat = 0, m_dragStart = 0, m_dragLen = 0, m_dragOrigX = 0;
    float m_dragMouseX = 0, m_dragMouseY = 0;
    bool m_dragMoved = false;
    double m_createStart = 0, m_createEnd = 0;
    int m_hoverTrack = -1, m_hoverRegion = -1;
    double m_hoverBeat = 0;
    std::string m_hint;

    // dock
    DockTab m_dock = DockTab::Mixer;
    int m_devSel = 0;
    // browser
    std::vector<std::pair<std::string, std::string>> m_effectNames;
    struct LoopEntry { std::string path, name; double bpm = 0; };
    std::vector<LoopEntry> m_loops;
    bool m_loopsLoaded = false;
    std::string m_search;
    float m_browserScroll = 0;
    bool m_sectionClosed[3] = {false, false, false};
    int m_browserSel = -1;
    bool m_payActive = false;
    int m_payKind = 0;
    std::string m_payId, m_payName, m_payPath;
    float m_payX = 0, m_payY = 0;
    // inspector / mixer scroll
    float m_mixScroll = 0, m_inspScroll = 0;
    // mixer: Logic filter buttons + narrow strips, Pro Tools / Logic peak-hold readouts
    int m_mixFilter = 0;   // 0 all, 1 audio, 2 instruments, 3 drums + drummer
    bool m_mixNarrow = false;
    std::map<int, float> m_peakHold;
    float m_masterHold = 0;
    // browser tabs (FL Studio): 0 All, 1 Loops, 2 Instruments, 3 Effects, 4 Project (the Pro Tools clip list)
    int m_browserTab = 0;
    double m_frameMs = 0;
    // piano roll
    double m_prScroll = 0, m_prPpb = 80;
    float m_prScrollY = 0, m_prKeyH = 14;
    std::set<int> m_prSel;
    enum class PrDrag { None, Move, Resize, ResizeL, Rubber, Velocity, Erase, Region } m_prDrag = PrDrag::None;
    std::vector<core::Note> m_prOrig;
    double m_prDragStart = 0, m_prRubberX = 0, m_prRubberY = 0;
    int m_prDragPitch = 0, m_prAnchor = -1;
    bool m_prMoved = false;
    bool m_foldNotes = false, m_scaleHighlight = true, m_scaleSnap = false, m_ghostNotes = true;
    // piano roll, Logic Pro / Ableton Live style
    int m_prGridIdx = 0, m_prQuantIdx = 2, m_prColorMode = 0;   // grid 0 = auto; quantize value; 0 colour by velocity (Logic), 1 by track
    bool m_prSnap = true, m_prFocus = false, m_dockMax = false;
    float m_prVelH = 78;
    float m_prLastVelX = -1;
    double m_prHoverBeat = 0;
    int m_scaleRoot = 0, m_scaleType = 0;
    int m_stampType = -1;   // -1 = single notes, else a ChordParams::Type, 100 = diatonic triad, 101 = diatonic seventh
    int m_stampInv = 0;
    std::vector<core::Note> m_noteClip;
    int m_auditionTrack = -1, m_auditionPitch = -1;
    double m_auditionUntil = 0;
    // steps
    int m_stepVelDrag = -1;
    // automation
    int m_autoParam = 0, m_autoDragPoint = -1;
    Rect m_autoLane;
    // typing, tap tempo
    bool m_typing = false;
    int m_typingOctave = 4;
    std::set<int> m_typingDown;
    std::vector<double> m_taps;

    // commands, palette, overlays
    std::vector<Command> m_commands;
    bool m_paletteOpen = false, m_helpOpen = false, m_tutOpen = false;
    int m_tutPage = 0;
    void drawTutorial();
    std::string m_paletteQuery;
    int m_paletteSel = 0;
    float m_helpScroll = 0;
    std::string m_toast;
    double m_toastAt = -100;

    // dialog
    struct Dlg {
        bool open = false;
        std::string title, okLabel;
        std::vector<DlgField> fields;
        std::function<void(const std::vector<DlgField>&)> onOk;
        float scroll = 0;
        std::string extraLabel;               // optional third button (for example "Don't Save")
        std::function<void()> onExtra;
    } m_dlg;
    core::RiffParams m_riff;
    core::LogicalParams m_logical;
    bool m_reopenPrefs = false;
    std::function<void()> m_afterFrame;
    int m_renameTrack = -1;
    std::string m_renameText;
    std::string m_regionRename;
    int m_snapshotRename = -1;
    std::string m_snapshotName;
};

}  // namespace vkui
