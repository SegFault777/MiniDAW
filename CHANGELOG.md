# Changelog

All notable changes to this project are documented in this file.

## [0.12] - 2026-11-07

### Added
- **The Vulkan version (`MiniDAWVulkan.exe`)**: a new user interface written from scratch and drawn by the GPU (`src/vulkan/`). Layers:
  `DrawList` (one vertex format, anti-aliased shapes by signed distance fields), `Fonts` (glyph atlas; GDI glyphs on Windows, built-in font in tests),
  `Gui` (immediate-mode toolkit: buttons, sliders, knobs, faders, text fields, dropdowns, menus, tooltips), `Studio` (the DAW screens) and `VkRenderer`
  (Vulkan backend, `vulkan-1.dll` loaded at run time). `SoftRaster` runs a draw list through the arithmetic of the fragment shader on the CPU, so the whole UI is tested
  and screenshotted without a GPU. Per-monitor DPI aware, `Interface scale` preference.
- **Command registry and palette**: one table of ~165 commands feeds the menus, shortcuts, the `Ctrl+K` command palette and the `F1` shortcut sheet.
- **Ten functions borrowed from other DAWs** (engine in `src/core`, tested in `core_tests.cpp`): Logic Pro *Adapt Project Tempo to Region* and *Groove Template*;
  Ableton Live *Capture MIDI* and *Fold / Scale Highlight* (+ Transform tools Stretch, Strum, Double Pattern); FL Studio *Chord Stamp* and *Riff Machine*;
  Cubase *Logical Editor* and *MixConsole Snapshots* (saved in the project file); Pro Tools *Tab to Transient* and *Beat Detective*.
- **Piano roll in the style of Logic Pro and Ableton Live**: real piano keys, velocity colours, note names, region bar, Auto grid, Quantize button, Functions menu,
  chord name display, paintable velocity lane, ghost notes, zoom sliders, info line, `B` / `Q` / arrow / `Ctrl+D` keys.
- **Vulkan UI, best of five DAWs**: overview strip with Start/End/Length counter (Pro Tools, Ableton), track number chips (Cubase, Pro Tools), track height presets (Pro Tools),
  browser tabs All / Loops / Inst / FX / Project (FL Studio) where *Project* is the clip list (Pro Tools), mixer strips tinted with the track colour (FL Studio) with a coloured
  name plate, numeric dB field, peak hold and dB scale (Logic, Ableton, Pro Tools), mixer filter and narrow strips (Logic, Cubase), track count and fps in the status bar.
- Other UI ideas: tool box (`1`-`5`), snap + grid menu with Auto, workspaces (`F5` / `F6` / `F7`), maximize panel, Ripple Delete (Pro Tools Shuffle), `Alt`+drag duplicate, hover info line.
- **Icon style** (Vulkan version): File > Preferences > Icon style chooses Material Symbols Outlined (default), Rounded, Sharp or the Classic icons, applied at once. The Material Symbols icons that MiniDAW uses are built into the program (a 20 KB subset, `IconFontData.cpp`, made by `tools/subset-material-symbols.py`); complete font files in a `fonts` folder replace them. Also used for the marks of the controls (drop-down arrow, check mark, sub-menu arrow, section fold marks, close buttons). Drawn by a built-in TrueType reader (`TrueType.*`, no new dependency); a missing icon falls back to the classic drawing. Apache-2.0 notice: `THIRD-PARTY.md`, `licenses/`.
- **Tutorial**: 3 new pages (mixer groups, bringing files in, exporting for other DAWs), 18 pages in all; translated into ko/ja/zh_CN/es (ja/zh_CN/es need a native review). Duplicate keys removed from the translation tables.
- **MIDI import options** (`MidiImportOptions`): File > Import MIDI File... asks for the tempo / time signature policy (adopt if empty, use the file's, keep the project's) and whether a file track that uses several MIDI channels is split into one track per channel. Both UIs; drops and the command line keep the old behaviour.
- **Dry stems**: new option "Stems: keep reverb and echo sends" in both Bounce dialogs (`ExportOptions::stemSends`); off = the aux returns (Rev, Echo) stay out of the stems, the insert effects of the tracks stay in.
- **Stems without the master chain**: new option "Stems: keep master volume and limiter" in both Bounce dialogs (`ExportOptions::masterEffects`); off = stems carry only the track effects. The master volume and limiter are restored after the export.
- **Logic Pro projects (.logicx)** (`core/LogicImport`, `LogicArrangement`, `LogicProjectData`, `Bplist`): File > Import Logic Pro Project Folder..., drag and drop and the command line accept a `.logicx` folder. Reads the tempo map, time signatures, markers, audio regions (file, position, trim, length) and MIDI regions with notes from ProjectData; track names from the arrange list (logicxkit findings, see `THIRD-PARTY.md`). Byte layouts from LogicProFormatWriter (MIT); checked against its decoder on 89 fixtures and on a real Logic Pro X 10.5 project (see `docs/dev/logicx.md`). The Session got `importAudioPlacements` (one decode per file, many regions). MIDI regions get their exact length from the region record. Native UI: File > Import Logic Pro Project Folder...
- **Trim audio regions at the playhead**: `Session::trimRegionStart/End` (audio only, not for stretched or transposed regions; the audio keeps its place, the region only gets shorter; the fade in / out are cut to what is left; one undo step each). Region > Trim Start to Playhead / Trim End to Playhead, and two buttons in the audio editor (a click in the waveform sets the playhead). The ruler no longer shows "-0 s".
- **Fix: dragging a region gain / fade slider in the inspector made one undo step per frame** (about sixty a second, which pushed the older history out of the 60 steps). `Session::checkpointCoalesced(tag)`: a change with the same tag within 0.6 s joins the previous undo step; used by `setRegionAudio`. One drag is one step; a pause, another kind of change, undo or redo ends it.
- **Audio editor** (Editor tab with an audio region selected; E): the whole region as a waveform with a time ruler, the audio as it is (dim) and as it is heard with gain and fades (bright), the fade-in and fade-out curves with a round handle each (drag to change, whole milliseconds; double-click removes the fade), the playhead, a click in the waveform moves the playhead, Normalize and Reverse buttons. A drag is one undo step. Zoom 1x-64x with the +/- buttons or Ctrl + wheel (around the mouse), the wheel scrolls when zoomed; a handle moves the fade by what the mouse moves, so at 4x a pixel is a quarter of the time. Until now the Editor tab was empty for audio regions.
- **Preferences reads the audio / MIDI device lists from a cache** that the host fills in a worker thread (`Host::refreshDevices`, `Host::devicesReady`; `DeviceCache` in `VkMain.cpp`): enumerating devices can block for seconds on some drivers (Bluetooth, virtual cables) and froze the window. The lists are read at the start and again each time Preferences is opened; if they are not there yet, a message asks to try again in a moment instead of waiting.
- **IME typing (Korean / Japanese / Chinese) in the Vulkan UI**: the text being composed is drawn inside the text field at the caret, underlined in the accent colour, and joins the text when the IME commits it (`Gui::setPreedit`, `Gui::imeCaret`; `WM_IME_STARTCOMPOSITION / COMPOSITION / ENDCOMPOSITION` in `VkMain.cpp`). The IME windows (composition, candidates) are put at the caret. While no text field has the focus the window's input context is taken away (`syncIme`), so with a Korean keyboard in Hangul mode the shortcuts (Space, R, M, S ...) keep working. The last syllable that arrives together with Enter is no longer lost. Links `imm32.lib`. The Win32 part could not be compiled or tried on Linux: only the field drawing and the commit order are tested (`vulkan_tests.cpp`).
- **Tutorial page "Preferences and handy extras"** (19 pages in all): Preferences, the MIDI channel of a track, remembered layout and the palette. "Try it" opens Preferences. Translated into ko/ja/zh_CN/es (ja/zh_CN/es need a native review).
- **Fix: Preferences / Tutorial froze the window for up to 20 s ("Not responding")**: every new glyph is rasterized through GDI (Windows font linking is slow for Korean / Japanese), and a dialog or a tutorial page brings hundreds of new glyphs in one frame. `Fonts::setFrameBudgetMs()` / `beginFrame()` / `takePending()`: the Vulkan UI now spends at most 8 ms per frame on new glyphs; the rest is drawn empty (not cached) and the next frame is drawn at once, so the window answers all the time and the text fills in over a few frames. The budget is off by default (tests, other UIs).
- **Project file version 4, newer files are reported**: `kProjectVersion` is 4 (mixer groups, mix snapshots, MIDI channels were added in v0.12; older programs ignore these fields). `Session::loadedFileVersion()` / `loadedFromNewerVersion()`: a project saved by a newer MiniDAW still opens, and both UIs say that some settings may be missing instead of "Opened ...".
- **Tests**: glyph atlas overflow (cleared once, flag consumed, no overlap, huge glyph skipped), file version.
- **MIDI channel per track** (`Track::midiChannel`, `Session::setTrackMidiChannel`): the Inspector of an instrument track has a *MIDI Ch* drop-down (Auto, 1-16) and the palette has `MIDI Channel: ...` commands. Export MIDI uses the chosen channel; *Auto* numbers the instrument tracks 1, 2, 3 ... skipping channel 10 and the chosen channels. Saved in the project (`midiChannel`), undoable, kept by *Duplicate Track*, and set from the file when a MIDI file with one channel per track is imported. Native UI: Track > MIDI Channel... (instrument tracks).
- **Gapless MP3** (`core/Mp3Gapless`): exported MP3 files get an Info frame (Xing / LAME header) with encoder delay and padding, so they decode to exactly the project length (checked with ffmpeg: 2.000 s in, 2.000 s out). Imported MP3 files with such a header have the decoder delay removed (skipped when the decoder already did it).
- Installers (Native and Vulkan): task *Add MiniDAW to the "Open with" list* for mp3, wav, aiff, flac, m4a, ogg and mid files (does not take over the default program).
- **Portable mode** (`core/AppPaths`): a file `MiniDAW.portable` next to the program keeps settings, autosaves and plug-ins in a `data` folder. Portable zips for Native, Vulkan and Qt.
- Native UI: features that only the Qt version had - **Aux A / Aux B effect editing**, *Play Test Tone*, *Choose Loops Folder...* / *Add VST3 Folder...* (folder picker), **Recover Autosaved Project**, **Open Recent > Clear Menu**,
  *Browser: Preview on Click*, an **unsaved-changes prompt** for New / Open / Recover, a message when the MIDI input cannot be opened.
- `build.sh all` builds all three programs; `shaders`, `vulkan`, `vulkan-release`, `native-deploy` (installer payload + portable folder) targets; a `vulkan` CI job.
- Vulkan interface: **multi-region selection** (Shift / Ctrl+click, rubber band, Ctrl+A), moving regions **between tracks** (dragging over another track moves them there when they fit), group move, group delete and Alt+drag duplicate, each as one undo step. Cut / Copy / Paste (spacing and track layout kept), Mute and Ripple Delete also work on the whole selection.
- Vulkan interface: **tempo lane** in the Automation tab (Parameter > Tempo (BPM)): add, drag, ramp (double-click a point) and delete tempo changes; each edit can be undone.
- Vulkan interface: **Drummer tab** (`Ctrl+7`, or double-click a Drummer region): style, complexity, loudness, swing, fills, bars, seed ("Roll New Variation") and a preview of the generated pattern, one row per drum. Slider drags are one undo step.
- Vulkan interface: **Help > Tutorial...** - a 15-page built-in tutorial (`src/vulkan/StudioTutorial.cpp`), translated into Korean, Japanese, Simplified Chinese and Spanish.
- **Exchange with other DAWs**: Bounce / Export Audio now writes **WAV 16 / 24 / 32-bit float, AIFF 16 / 24-bit and MP3** at 44.1 / 48 / 88.2 / 96 kHz (MP3: 44.1 / 48), with **ID3 tags** and the choice of bit rate (`ExportOptions`, `core/ExportFormats`, `core/Id3`). MP3 is encoded by Windows (Media Foundation, `platform/Mp3Encode.cpp`; `Session::Hooks::encodeMp3`). **Stems**: one file for every track, all the same length (`Session::exportStems`). Export at other sample rates uses a new high quality windowed-sinc converter (`core::resampleStereo`), which now also converts imported 48 / 96 kHz files.
- **File types**: File > Open, drag and drop and "open with" accept projects, audio files (wav, aiff, mp3, m4a, aac, flac, wma, ogg, opus ...) and MIDI files (`core/FileTypes`, `openAny`); AIFF is read by the core without a codec (`core::readAiffFile`).
- **MIDI**: export and import now carry the whole tempo map, time signature changes and markers (conductor track named "Temp"), like Logic Pro and the other DAWs.
- Vulkan version: `vulkan-settings.ini` is created at the first start and written again when the program closes (with the layout); before, it only appeared after a setting had changed. The Preferences window and the About window now show the real path of the file.
- Vulkan version: an **installer** (`installer/MiniDAW-Vulkan.iss`, `build.sh vulkan-installer`; `vulkan-release` now builds it) next to the portable zip, with its own AppId and folder so it installs beside the Native version. The portable copy is now made from the install folder like in the Native version, so the installer never contains the `MiniDAW.portable` marker.
- Build: VST3 hosting now links with the current Steinberg SDK. `build.sh` adds the SDK files that moved (`threadchecker_win32.cpp`, `commonstringconvert.cpp`, the file defining `IPlugFrame::iid`) by searching for them, and `Vst3Native.cpp` defines `IPlugFrame::iid` itself when no SDK file does. Before, the link failed (LNK2019) and the build silently continued without VST3.
- Vulkan interface: the window draws itself while its edge is dragged (WM_SIZE / WM_PAINT render immediately), so the newly exposed area follows the mouse instead of staying black until the button is released.
- **Mixer groups** (Vulkan interface): eight groups (A to H); grouped tracks share fader ratio, pan and send offsets, mute and solo. Alt bypasses. Saved in the project file (`Track::group`, `Session::setTrackGroup`).
- Vulkan interface comfort: the layout (panels, sizes, bottom tab, track height, zoom, Snap, Follow) is remembered between sessions (`Host::storeSettings`, `Studio::layoutSettings`); floating toast notifications; middle-mouse drag pans the view; edge scrolling while dragging regions or a rubber band.
- Performance (Vulkan interface): while the song plays, only the meters are refreshed instead of copying the whole project every frame; the full copy is made when the project changes (`Session::revision()`, `Session::trackPeaks()`) or while recording.
- Windows build fixes found by CI: `small` macro of `<windows.h>` (Theme.h), `std::min` with `LONG` / `int` in VkMain.cpp, `NOMINMAX` redefinition warnings.
- CI: the GLSL compiler for the Vulkan job is installed from the LunarG Vulkan SDK (Chocolatey), with a build from the glslang source as the fallback.
- Translations of the Vulkan interface (166 new texts) in Korean, Japanese, Simplified Chinese and Spanish; messages with numbers now use `%1` patterns (`Studio::fmt`).
- English documentation: all of `docs/` (`user/`, `dev/`, `project/`), the root `README.md` and `ARCHITECTURE.md`. `docs/dev/` and `docs/project/` were rewritten to describe the three executables, `src/vulkan`, portable mode and the new build targets; `docs/project/ui-design.md` now compares five DAWs.

### Changed
- **Executable names**: Native is now `MiniDAW.exe` (was `MiniDAWNative.exe` in the build folder), Qt is `MiniDAWQt.exe` (was `MiniDAW.exe`), Vulkan is `MiniDAWVulkan.exe`.
- Versions bumped to 0.12 in all version strings.

### Not done yet
- Windows-only code (`VkMain.cpp`, `VkRenderer.cpp`, the Win32 changes) and the shaders were not compiled on Windows / a GPU in this session.

## [0.11] - 2026-11-04

### Changed
- **The native (Qt-free) version is now the product.** `build.sh native-release` builds it, creates the portable zip
  (`MiniDAW-windows-x64-portable.zip`) and the Inno Setup installer (`installer/MiniDAW.iss`, which upgrades an old
  Qt installation and removes its leftover files). The Qt version is only built as `MiniDAW-Qt-windows-x64-portable.zip`
  (`build.sh qt`; its installer script is `installer/MiniDAW-Qt.iss`). The CI workflow has the jobs `tests` (Linux),
  `native` (installer, portable zip, release) and `qt`.
- **UI redesign** after comparing Logic Pro and Ableton Live (see `docs/project/ui-design.md`): the loop browser is now a
  searchable **Browser** on the right with loops, instruments and effects; items can be **dragged onto tracks** (loops and
  instruments also onto an empty part of the arrangement). *Smart Controls* became **Devices**: a chain of cards with
  rotary **knobs**, bypass / remove / editor buttons, pages for many parameters and an "Add an effect" card.
  Three **track heights** (View menu), a **hover help line** in the status bar, `I` opens the Inspector tab.
- Command line: `MiniDAW.exe project.mdaw` opens a project (file association).

### Added
- **MiniDAW plug-ins without Qt** (`platform/PluginHost`, tested on Linux) and **VST3 hosting without Qt**
  (`native/Vst3Native`, built when `VST3_SDK_DIR` is set; falls back to a build without VST3 if it fails): instruments and
  effects, their own editor windows (`E` button), plug-in state in the project file, plug-in scan in the background,
  Preferences: VST3 folders and *Rescan plug-ins*.
- Piano roll **multi-selection**: Shift-click, Shift-drag rubber band, `Ctrl+A`; move, resize, change velocity and delete the
  selection together; `Ctrl+C` / `Ctrl+X` / `Ctrl+V` / `Ctrl+D` for notes.
- About 150 new texts in the Korean, Japanese, Chinese and Spanish translations; messages with names use `%1` patterns.
- **Documentation** in `docs/` (`user/`, `dev/`, `project/`).

### Not done yet
- The native version is not DPI aware (Windows scales it, slightly blurry on high-resolution screens).
- No clip launcher (session view), no score view. See `docs/project/roadmap.md`.
- `src/native/*` (Win32 shell, VST3 host) is only verified by the Windows CI build.

## [0.10_1] - 2026-11-03

### Fixed
- Native build (MSVC): `Win32App.cpp` included `windows.h` without `NOMINMAX`, so the `min` / `max` macros broke the
  members called `min` and `max` in `dsp/Param.h` (C4003, C2059) and everything after it. `NOMINMAX` and
  `WIN32_LEAN_AND_MEAN` are now defined before the include (and passed on the compiler command line of the native
  target). Also silenced the `fopen` / `sscanf` deprecation warnings and a double-to-int conversion warning.

## [0.10] - 2026-11-03

### Added (native version, `MiniDAWNative.exe`)
- **Dialogs** (`ui/Dialog`): a modal panel with number, slider, check box, choice, text and button fields. Used by
  Transpose, Crescendo, Repeat, Quantize (grid, strength, swing, lengths), Scale Quantize, Humanize, Arpeggiate,
  Chord Trigger, Fixed Note Length, Scale Velocity, Strip Silence, Detect Tempo, Convert Pattern to MIDI, Tempo,
  Time Signature, Bounce (range, format, dither, normalize, tail), Rename and Preferences.
- **Bottom panel with tabs**: Mixer, **Inspector** (track colour, volume, pan, sends, instrument presets, insert
  effects with bypass / remove, region name / mute / gain / fades / reverse / normalize / follow tempo / transpose),
  **Smart Controls** (every parameter of the instrument and of each insert), **Piano Roll** (add, move, resize,
  delete notes, velocity lane, keyboard with audition, zoom and scroll), **Step Sequencer**, **Drummer** editor
  (style, complexity, loudness, swing, fills, new variation, bars), **Automation** (volume, pan, sends and the
  project tempo lane) and the **Loop Browser** (folder scan, tempo from the file name, preview, add at the playhead).
- Markers on the ruler (add, jump, drag, rename, delete), cycle range by Shift-dragging the ruler, tap tempo,
  musical typing, recent projects, Cut / Mute region, Duplicate / Move / Rename track, Bounce in Place.
- **Preferences**: output / input device, buffer size, recording latency compensation, MIDI keyboard, language,
  autosave interval and loops folder, stored in `%LOCALAPPDATA%\MiniDAW\native-settings.ini`. Autosave and crash
  recovery use the same files as the Qt version.
- **Translations**: every drawn text is looked up in the `translations/*.tsv` files (`ui::Canvas::setTranslations`),
  so the whole UI follows the language setting without touching the drawing code. About 140 new texts were added
  to the Korean, Japanese, Chinese and Spanish files.
- `Session::renameRegion`; the CI workflow builds the native version in its own job.

### Not done yet
- VST3 and plug-in SDK (`.mdplugin`) hosting and their windows are only in the Qt version; the native version has
  the built-in instruments and effects.
- Multi-selection, rubber-band selection and copy / paste of notes in the piano roll; per-step velocity in the step
  sequencer; Score and Musical Typing keyboard display; drag and drop of loops onto a lane.
- Qt is still built, because of the plug-in hosting above.

## [0.9] - 2026-11-02

### Added
- **Qt-free user interface (experimental, `MiniDAWNative.exe`)**: `src/ui` is a software-rendered UI toolkit (pixel
  canvas, anti-aliased shapes and text) with its own look: control bar with transport and LCD, toolbar menus
  (File, Edit, Functions, View), arrangement with ruler / track headers / regions with waveforms and note
  previews, drag to move and resize regions, snap, zoom, a mixer with insert slots, pan knobs, dB faders and
  level meters, context menus and keyboard shortcuts. `src/native/Win32App.cpp` is the whole Windows shell
  (window, GDI text, file dialogs, drag and drop of audio files, WASAPI output, microphone recording). The UI
  is tested headless (`tests/ui_tests.cpp` renders screenshots and replays mouse and keyboard input).
  Build it with `bash build.sh native`; `bash build.sh all` builds it next to the Qt version.
- Eight more **Logic Pro style editing features** (also in the native *Functions* menu): **Fix Overlaps**,
  **Remove Duplicate Notes**, **Fixed Note Length**, **Retrograde**, **Invert Pitches**, **Scale Velocity**,
  **Strip Silence** (cuts quiet parts out of an audio region) and **Measure Loudness** (integrated LUFS,
  ITU-R BS.1770 / EBU R128). They live in the Qt-free core (`MidiOps`, `dsp/AudioAnalysis`) and are covered by tests.
- `tools/gen_font.py` generates the built-in anti-aliased font tables (`src/ui/FontData.cpp`, Liberation Sans,
  SIL Open Font License 1.1).

### Not done yet
- The Qt version is still the complete editor and is still built: piano roll, drum step editor, inspector,
  preferences, plug-in windows, automation lanes, markers, tempo editing and the loop browser exist only
  there. The native UI does not have them yet, and its texts are English only. Qt will be removed once the
  native UI reaches parity.
- The new v0.9 editing features have no dialogs in the Qt version.

## [0.8_1] - 2026-11-01

### Fixed
- Windows build: `MainWindow.cpp` used `selectedMidiRegion` before its definition (C3861); `platform/Audio.cpp`
  was missing `#include <algorithm>` (C2039 for `std::clamp`). Added the same missing standard includes to a few
  other source files.

## [0.8] - 2026-11-01

### Changed
- **Platform layer without Qt** (`src/platform/`): audio output and input use WASAPI shared mode directly
  (event driven, converted to 44.1 kHz stereo), MIDI keyboards use winmm, and MP3 / AAC / M4A / FLAC / WMA
  are decoded with Media Foundation (WAV is read by the core). The Qt Multimedia module is no longer needed.
  A silent "null" audio device makes the whole audio path testable without hardware.
- Translation files are parsed by the Qt-free `core::I18n`; `Translator` is only a thin shell around it.
- `Engine` no longer uses `QAudioSink`, `QAudioSource`, `QAudioDecoder` or `QThread`: it owns the platform
  streams, a decode worker thread and a 3 second device poll.
- Saved audio device ids changed from the Qt format to WASAPI endpoint ids. An old device choice in the
  preferences is ignored and the system default device is used (no error). OGG still does not open with
  Media Foundation's default codecs (same as before).

### Added
- Seven **Logic Pro style editing features**: **Legato**, **Transpose…**, **Crescendo / Decrescendo…**
  (Edit > MIDI Transform), **Repeat Region…** (`Ctrl+R`), **Join Regions** (`Ctrl+J`), **Detect Tempo…**
  for audio regions (a Smart Tempo like analysis: follow the project tempo or set the project tempo) and
  **Convert Pattern to MIDI Region** for drum machine tracks (Track menu).
- Tests for the platform layer (`tests/platform_tests.cpp`), the new MIDI operations and the tempo detector.

### Fixed
- Tempo detection no longer reports half the tempo of fast clicks (160 BPM was found as 80 BPM) and no longer
  confuses a pattern with a hi-hat on every 8th note for a tempo three grid steps long (a 100 BPM drum loop
  was found as 133 BPM). Very busy material with several competing pulses can still be wrong; the dialog lets
  you decide whether to use the result.

## [0.7] - 2026-10-31

### Changed
- **Qt-free core.** The engine was split into a platform-independent `core::Session` (`src/core/`: project
  model, tracks and regions, undo, tempo, transport, recording, offline rendering, bounce, project files,
  autosave, import / export) and a thin Qt layer. `Engine` keeps the same public API for the user interface
  and now only provides audio devices, file decoding (`QAudioDecoder`), plug-in hosts and Qt signals.
- Project files are read and written with MiniDAW's own JSON implementation (`core::Json`); files are
  handled with `std::filesystem` and UTF-8 paths (`core::Files`); WAV files are read and written without Qt
  (`core::AudioIO`). The file format is unchanged (still version 3).
- The model types (`Track`, `Region`, `Note`, `Marker`, ...) use `std::string` and `core::Color` instead of
  `QString` and `QColor`; `QtBridge.h` converts for the user interface.
- Offline rendering, bounce and export now run on the core without a sound device. Export streams to disk.

### Added
- **Bounce in Place** (`Ctrl+Alt+B`, Track menu and track-header menu).
- **Quantize Region…** with strength and swing (`Ctrl+Shift+Q`).
- **Arpeggiate Region…** (`Ctrl+Shift+A`): six patterns, rate, octave range and gate.
- **Scale Quantize…**, **Humanize…** and **Chord Trigger…** (Edit > MIDI Transform).
- `tests/core_tests.cpp`: JSON, text formatting, WAV files, save / load round trip, undo, clipboard, track
  duplication, autosave and recovery, export formats, cycle export, normalization, bounce in place (the bounced
  track renders like the original), pan automation and every MIDI operation. `tests/run.sh` builds and runs
  everything with g++ or clang++; a new GitHub Actions workflow (`tests.yml`) runs it on Linux.

## [0.6] - 2026-10-30

### Added
- **Pan, Send A and Send B automation** in addition to volume (*Edit > Automation Parameter*); pan automation
  uses the pan law per sample. Saved in the project file.
- **Autosave and crash recovery**: autosaves to the local app-data folder while the project has unsaved
  changes (interval in *Preferences > General*, default 3 minutes). A leftover lock file after a crash makes
  MiniDAW offer the last autosave. New recordings are written once and reused by later autosaves.
- **Markers** (`M`, `Alt+Left`, `Alt+Right`): strip above the ruler, add / rename / delete / drag, saved in the
  project and covered by undo.
- **Move Track Up / Down** and **Track Color** in the track header menu.
- **Mute Region** (`Ctrl+M` and region menu), dimmed on the timeline and saved in the project.
- **Chorus** and **Filter** insert effects.
- **Open Recent** (8 projects) and **Tap Tempo** (`Shift+T`).
- **MiniDAW plug-in SDK**: `plugin-sdk/mdplugin.h` (plain C API, version 1), `dsp::NativeEffect` host wrapper,
  loader for the `plugins` folders and two example plug-ins (Tremolo, Bitcrusher) that are built and shipped
  by `build.sh deploy`. Unit tests load the examples with `dlopen`.

### Changed
- The ruler is 12 px taller to make room for the marker strip.
- Project files are still version 3; v0.6 files open in v0.5 except for the new automation lanes, markers
  and muted regions, which older versions ignore.

## [0.5_1] - 2026-10-29

### Fixed
- Build failure in `MainWindow.cpp` (missing `QPushButton` include used by the new Bounce dialog).

## [0.5] - 2026-10-29

### Added
- **Cut / Copy / Paste** for regions (`Ctrl+X`, `Ctrl+C`, `Ctrl+V`) and a region context menu. Audio regions
  paste on Audio tracks, MIDI regions on Software Instrument and Drummer tracks; regions that follow the
  tempo are re-stretched for the tempo at the paste position.
- **Duplicate Track** (`Ctrl+Shift+D` and the track-header menu) with independent copies of the instrument
  and insert effects (including VST3 plug-in state).
- **Audio input device** selection and **recording latency compensation** (-500 .. +500 ms) in Preferences.
- **Audio region editing** in the Inspector: gain (-60 .. +24 dB), fade in / fade out, Reverse and Normalize.
  Fades are drawn on the region, split regions keep the fades on their outer edges, and everything is saved
  in the project.
- **Bounce dialog** with whole-project or cycle-range export, 16-bit / 24-bit PCM and 32-bit float WAV,
  TPDF dither, normalization to a chosen peak level and an optional 2 s reverb / echo tail.
- Qt-free `dsp/AudioOps` module (fades, gain, reverse, recording offset, WAV encoding) with unit tests.

### Changed
- Project files are written as version 3 (older versions of MiniDAW ignore the new region fields).
- Translations (Korean, Japanese, Simplified Chinese, Spanish) cover the new texts.

## [0.4_1] - 2026-10-29

### Fixed
- Build failure in `Preferences.cpp` (missing `QComboBox`, `QAudioDevice` and `<algorithm>` includes)
  that broke the GitHub Actions Windows build.

## [0.4] - 2026-10-28

### Added
- **Tempo track** with step changes and linear ramps, editable in the Tracks area (`G`).
- **Time signature** changes at any bar; the ruler, bar numbers, metronome accents, count-in,
  Drummer and recorded regions follow the meter.
- **Time stretching and pitch shifting** (WSOLA): regions can follow the project tempo and be
  transposed. Loops that carry a tempo in their file name follow the project tempo automatically.
- Tempo and time-signature edits take part in undo / redo and in saved projects.
- Logic-style Mixer strips (FX slots, sends, pan, dB fader, name tags), track-type icons in the track
  headers, a Drummer editor with an XY pad, and a lighter LCD with tempo and time signature.

## [0.3] - 2026-10-28

### Added
- **VST3 hosting** for instruments and effects, with editor windows and saved plug-in state
  (compiled when the VST3 SDK is available).
- **Drummer** tracks: styles, complexity / loudness pad, swing, fills and variations.
- **MIDI keyboard input** (winmm) with sustain pedal; **import and export of Standard MIDI Files**.
- **Loop Browser** with search, preview, drag and drop, and sample loops.
- **Preferences**: audio device and buffer size, MIDI input, language, loops folder, VST3 folders.
- **Languages**: Korean, Japanese, Simplified Chinese and Spanish, from plain-text translation files.

### Fixed
- Playback could stay silent on devices that reject 44.1 kHz / 16-bit. MiniDAW now falls back to the
  device's preferred format (with conversion), reports the opened device in the status bar and
  offers a test tone.

### Changed
- Project files use the extension `.mdaw` and store the tempo and meter maps (projects from v0.2 still open).

## [0.2] - 2026-10-25

### Added
- Logic Pro-inspired interface: control bar with LCD display, Inspector, Tracks area and an
  editor area with Mixer, Smart Controls, Piano Roll and Step Sequencer.
- Software Instrument tracks with a built-in 16-voice subtractive synthesizer and presets.
- Piano Roll with draw / move / resize, velocity lane, grid selection and quantize.
- Audio and MIDI recording (MIDI via Musical Typing), count-in and metronome.
- Insert effects (Channel EQ, Compressor, Echo, Reverb, Overdrive), two aux sends and a master limiter.
- Volume automation, cycle range, region split / duplicate / resize.
- Undo / redo, project save / load (`.mdaw`), bounce to WAV.
- Windows installer built with Inno Setup and a portable ZIP, produced by GitHub Actions.
- GPL-3.0-or-later license, README and changelog.

### Changed
- The build is driven by `build.sh` (moc, MSVC and Inno Setup) instead of CMake.
- Positions are stored in beats so MIDI follows tempo changes.
- Audio output now runs continuously so instruments can be played while the transport is stopped.

## [0.1] - 2026-10-24

### Added
- Initial multitrack timeline with drag-and-drop audio import, 16-step drum machine,
  basic mixer, metronome, loop and WAV export.
