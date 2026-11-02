# Changelog

All notable changes to this project are documented in this file.

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
  autosave interval and loops folder, stored in `%LOCALAPPDATA%\MiniDAW\settings.ini`. Autosave and crash
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
