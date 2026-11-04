# MiniDAW

A lightweight, fast digital audio workstation for Windows, written without any UI framework (its own
software-rendered UI toolkit on Win32) and inspired by the workflow of Logic Pro. Documentation:
[docs/](docs/README.md) (user manual, developer guide, project documents). It stays approachable for beginners and still offers what a producer expects:
a multitrack arrangement, MIDI editing, a built-in synthesizer and **Drummer**, a **Loop Browser**,
channel strips with insert effects and sends, **VST3 plug-ins**, **tempo and time-signature tracks**,
**time stretching**, automation, recording and bouncing.

> MiniDAW is an independent open-source project. It is not affiliated with, endorsed by or
> sponsored by Apple Inc. "Logic Pro" is a trademark of Apple Inc.
> VST is a trademark of Steinberg Media Technologies GmbH.

**Version 0.11** · License: GPL-3.0-or-later

## Features

### Arrangement
- Multitrack **Tracks** area with a ruler that follows the time signature, a smart snap grid,
  zoom (Ctrl + wheel) and scrolling.
- Four track types: **Audio**, **Software Instrument**, **Drummer** and **Drum Machine**.
- Regions: move, resize, split at the playhead, duplicate, delete, and **cut / copy / paste** (`Ctrl+X` /
  `Ctrl+C` / `Ctrl+V`, or right-click a region or an empty part of a lane). Drag audio or MIDI files from
  Explorer or from the Loop Browser straight onto the timeline.
- **Duplicate Track** (`Ctrl+Shift+D`) copies a track with its regions, instrument and effects.
- **Cycle** range (right-drag on the ruler), metronome (accents follow the time signature) and an
  optional one-bar **count-in**. **Volume automation** per track. Undo / redo for edits and tempo changes.

### What is new in v0.11
- **The native version is now the product**: `MiniDAW-<version>-setup.exe` installs it (and upgrades an old Qt
  installation); the Qt version only remains as a portable zip.
- **Plug-ins without Qt**: MiniDAW plug-ins (`plugin-sdk`) and **VST3** instruments and effects, with their own editor windows.
- **New look and workflow, designed from a comparison of Logic Pro and Ableton Live** (`docs/project/ui-design.md`): a searchable
  **Browser** with drag and drop (loops, instruments, effects), a **Devices** tab with a chain of cards and knobs, three track
  heights, a hover help line in the status bar.
- Piano roll **multi-selection** (Shift-click, rubber band, `Ctrl+A`), copy / paste / duplicate of notes.
- **Documentation** in [`docs/`](docs/README.md): user manual, developer guide and project documents.

### What is new in v0.10
- The native version (`MiniDAWNative.exe`, no Qt) now has almost everything of the Qt version: piano roll, step
  sequencer, Drummer editor, inspector, smart controls, automation and tempo lanes, markers, loop browser, all
  parameter dialogs, preferences, recent projects, tap tempo, musical typing, MIDI keyboard input, autosave and
  recovery, and all four languages. Only VST3 / plug-in SDK hosting is still Qt-only.

### What is new in v0.9
- **Native user interface without Qt (experimental)**: `MiniDAWNative.exe` is built next to `MiniDAW.exe`. It has
  its own look (slate colours, teal accent), the transport and a LCD, an arrangement with waveforms and note
  previews, a mixer, menus and shortcuts, and it opens and saves the same `.mdaw` projects. It does not have the
  piano roll, the drum step editor, the inspector, preferences, automation lanes and plug-in windows yet, so the
  Qt version stays the main program for now.
- Eight more **Logic Pro style editing features**: Fix Overlaps, Remove Duplicate Notes, Fixed Note Length,
  Retrograde, Invert Pitches, Scale Velocity, **Strip Silence** and **Measure Loudness** (LUFS). They are in the
  native *Functions* menu.

### What is new in v0.8
- **Platform layer without Qt** (`src/platform/`): own WASAPI audio output / input, winmm MIDI input and
  Media Foundation decoding; the *Qt Multimedia* module is no longer required. Saved audio device choices from
  older versions are ignored once and the system default device is used.
- Seven more **Logic Pro style editing features**:
  1. **Legato** (*Edit > MIDI Transform*): extends every note up to the next one.
  2. **Transpose…**: moves the notes by -48 to +48 semitones.
  3. **Crescendo / Decrescendo…**: velocity ramp from a start to an end velocity over the region.
  4. **Repeat Region…** (`Ctrl+R`): puts N copies directly after the selected region.
  5. **Join Regions** (`Ctrl+J`): merges a region with the next one (MIDI, or neighbouring parts of one recording).
  6. **Detect Tempo…** (*Edit*): analyses an audio region and lets you follow the project tempo or set the project tempo.
  7. **Convert Pattern to MIDI Region** (*Track*): turns the step pattern of a drum machine track into a MIDI region.

### What is new in v0.7
- **Qt-free core** (first step towards MiniDAW's own engine): the whole music engine, project model, undo,
  project files (own JSON reader / writer), autosave, bounce and MIDI operations now live in `src/core/` and
  `src/dsp/` and use only the C++ standard library. Qt remains for the user interface, audio devices and file
  decoding. The core has its own test suite that runs without Qt. See [docs/dev/architecture.md](docs/dev/architecture.md).
- Six **Logic Pro style editing features**:
  1. **Bounce in Place** (`Ctrl+Alt+B`): renders a track with instrument, effects, fader and pan to a new
     audio track and mutes the original.
  2. **Quantize with Strength and Swing** (*Edit > MIDI Transform > Quantize Region…*, `Ctrl+Shift+Q`).
  3. **Arpeggiator** (`Ctrl+Shift+A`): turns chords into arpeggios (up, down, up & down, down & up, random,
     as played; rate, octave range, gate).
  4. **Scale Quantize**: moves the notes into a key and scale (12 scales, key C to B).
  5. **Humanize**: randomises timing and velocity of programmed notes.
  6. **Chord Trigger**: turns every note into a chord (major, minor, diminished, augmented, sus2, sus4,
     major 7, minor 7, dominant 7, octave) with inversions.
  The MIDI transformations work on the selected MIDI region, are one undo step each and are applied to the
  notes (they are not real-time MIDI plug-ins).

### What is new in v0.6
- **Pan and send automation** next to volume: *Edit > Automation Parameter* chooses which lane `A` edits.
- **Autosave and crash recovery**: the project is autosaved while it has unsaved changes (default every
  3 minutes, *Preferences > General*). After a crash MiniDAW offers to restore the last autosave.
- **Markers**: `M` adds a marker at the playhead, double-click the marker strip above the ruler to add or
  rename one, drag to move it, `Alt+Left` / `Alt+Right` jump between markers.
- **Move tracks** up and down (`Ctrl+Alt+Up` / `Down`) and choose a **track color** from the header menu.
- **Mute regions** (`Ctrl+M`) without deleting them.
- **Chorus** and **Filter** (low cut / high cut with resonance) insert effects.
- **Open Recent** menu and **Tap Tempo** (`Shift+T`).
- **MiniDAW plug-in SDK**: write your own audio effects as a small shared library and load them from the
  `plugins` folder. See [`plugin-sdk/README.md`](plugin-sdk/README.md); a tremolo and a bitcrusher are included.

### What is new in v0.5
- **Cut, copy and paste regions** between tracks and projects; pasting follows the tempo at the new position.
- **Duplicate Track** with its own copy of the instrument and the insert effects.
- **Audio input device** and **recording latency compensation** in *Preferences > Audio*.
- **Region gain, fade in / fade out, Reverse and Normalize** for audio regions (Inspector). Fades are drawn
  on the region and are saved with the project.
- **Bounce dialog**: whole project or cycle range, 16-bit, 24-bit or 32-bit float WAV, TPDF dither,
  normalization to a chosen peak level and an optional reverb / echo tail.

### Tempo, time signature and time stretching (v0.4)
- **Tempo track** (`G`): click to add tempo points, drag to change them, right-click for *Ramp to Next
  Point*. Tempo ramps glide linearly between points.
- **Time signature** changes at any bar (`Transport > Time Signature…` or click the signature in the
  LCD): 2/4, 3/4, 6/8, 7/8, 5/4 and so on. Click, count-in, ruler and bar numbers follow.
- Edit the tempo at the playhead directly in the LCD.
- **Time stretch and pitch shift** (WSOLA): audio regions can *Follow Tempo* (enter the source tempo
  in the Inspector) and be transposed by ±24 semitones. Loops with a tempo in their name, such as
  `Funk Bass 100 bpm.wav`, follow the project tempo automatically.

### Instruments, Drummer and MIDI (v0.3)
- Built-in 16-voice subtractive **synthesizer** (two band-limited oscillators, sub oscillator,
  resonant filter, ADSR, six presets) and a synthesized **drum kit**.
- **Drummer**: pick a style (Rock, Pop, Hip-Hop, House, Funk, Disco), move the XY pad between
  *Simple / Complex* and *Soft / Loud*, add swing and fills, or press *New Variation*.
- **MIDI keyboards** (any USB or class-compliant device, including the sustain pedal), **Musical
  Typing** (`Ctrl+K`) and MIDI recording. **Import and export Standard MIDI Files.**
- **Piano Roll**: draw, move and resize notes, velocity lane, grid and quantize.
- 16-step **Step Sequencer**.
- **VST3 instruments and effects** with their own editor windows and saved state (see below).
- **Loop Browser** (`O`): search the loops folder, preview, double-click or drag to add. Sample loops
  are included.

### Mixing
- Inspector with the channel strip of the selected track, and a Logic-style **Mixer**: audio FX slots,
  sends, pan, dB fader with level meter, mute / solo / record-enable and coloured name tags.
- Insert effects: **Channel EQ**, **Compressor**, **Echo**, **Reverb**, **Overdrive** and any VST3 effect.
- Two aux sends (Reverb and Echo buses) and a master **Limiter**.
- **Bounce to WAV** (16 / 24-bit or 32-bit float, 44.1 kHz; whole project or cycle range).

### Languages
English, Korean (한국어), Japanese (日本語), Simplified Chinese (简体中文) and Spanish (Español). The language
follows the system by default and can be chosen in *Preferences > General*. Translations are plain text
files in `translations/`: add `fr.tsv` (copy `ko.tsv`, translate the right-hand column, change the
`#name` line) and it appears in the list. Untranslated texts fall back to English.

### Projects
- Save and open `.mdaw` projects (JSON). Recordings are stored next to the project file.

## Keyboard shortcuts

| Action | Shortcut | Action | Shortcut |
|---|---|---|---|
| Play / pause | `Space` | Record | `R` |
| Stop (twice = go to start) | `.` | Go to beginning | `Return` |
| Cycle on / off | `C` | Metronome | `K` |
| Inspector | `I` | Loop Browser | `O` |
| Mixer | `X` | Smart Controls | `B` |
| Piano Roll | `P` | Step Sequencer | `Shift+S` |
| Drummer editor | `Shift+D` | Show / hide editors | `E` |
| Tempo track | `G` | Volume automation | `A` |
| Split at playhead | `Ctrl+T` | Duplicate region | `Ctrl+D` |
| Delete | `Del` | Musical Typing | `Ctrl+K` |
| Cut / copy / paste region | `Ctrl+X` / `Ctrl+C` / `Ctrl+V` | Duplicate track | `Ctrl+Shift+D` |
| Add marker | `M` | Previous / next marker | `Alt+Left` / `Alt+Right` |
| Bounce in place | `Ctrl+Alt+B` | Arpeggiate region | `Ctrl+Shift+A` |
| Quantize region (strength, swing) | `Ctrl+Shift+Q` | Repeat / join regions | `Ctrl+R` / `Ctrl+J` |
| Mute region | `Ctrl+M` | Move track up / down | `Ctrl+Alt+Up` / `Down` |
| Tap tempo | `Shift+T` | Automation on / off | `A` |
| New audio / instrument / drum machine track | `Ctrl+Alt+A` / `S` / `D` | New Drummer track | `Ctrl+Alt+R` |
| Undo / redo | `Ctrl+Z` / `Ctrl+Shift+Z` | Import audio | `Ctrl+I` |
| Save / open | `Ctrl+S` / `Ctrl+O` | Bounce | `Ctrl+B` |
| Preferences | `Ctrl+,` | | |

Musical Typing: `A W S E D F T G Y H U J K O L P ;` play a chromatic keyboard starting on C;
`Z` / `X` shift the octave. In the Piano Roll press `Q` to quantize. Double-click an empty part of an
instrument lane to create a MIDI region, or of a Drummer lane to create a Drummer region.

## Installing

Download `MiniDAW-<version>-setup.exe` from the [Releases](../../releases) page and run it. The
installer places MiniDAW in `Program Files` (or in your own folder without administrator rights), adds a Start
menu entry and can associate `.mdaw` files; it upgrades an older Qt installation. A portable ZIP
(`MiniDAW-windows-x64-portable.zip`) is published next to every installer, and the old Qt version only as
`MiniDAW-Qt-windows-x64-portable.zip`. See [docs/user/installation.md](docs/user/installation.md).

### No sound?
Open *File > Preferences...* and choose another output device or a larger buffer. The status line shows
the device MiniDAW opened. More in [docs/user/troubleshooting.md](docs/user/troubleshooting.md).

### Plug-ins
MiniDAW plug-ins (`plugin-sdk/`) are loaded from `<install folder>\plugins` and `%LOCALAPPDATA%\MiniDAW\plugins`.
VST3 plug-ins are scanned in `C:\Program Files\Common Files\VST3` and `%LOCALAPPDATA%\Programs\Common\VST3` (more
folders in *Preferences*); installers built with the VST3 SDK host them. They appear in the **Browser** (Effects /
Instruments); their own windows open from the *E* button of a device card. See
[docs/user/plugins.md](docs/user/plugins.md).

## Building from source

MiniDAW is built by a single shell script, `build.sh` (no CMake required). The native version needs **no Qt**.

Requirements: Windows 10/11, Visual Studio 2022 or newer (MSVC x64 tools), Git Bash (or MSYS2), and optionally
[Inno Setup 6](https://jrsoftware.org/isinfo.php) for the installer. The old Qt version additionally needs Qt 6.5+.

```bash
# In Git Bash started from the "x64 Native Tools Command Prompt" (so that cl.exe is on PATH):
export VST3_SDK_DIR=C:/dev/vst3sdk     # optional: git clone --recurse-submodules https://github.com/steinbergmedia/vst3sdk
./build.sh native-release   # build + portable folder / ZIP + installer -> installer-out/MiniDAW-<version>-setup.exe
./build.sh native           # only build/MiniDAWNative.exe
./build.sh qt               # the old Qt version as a portable ZIP (needs QT_DIR)
./build.sh clean
```

The tests need only `g++`: `bash tests/run.sh` (also on Linux). Without `VST3_SDK_DIR` (or if the SDK fails to
build) MiniDAW is built without VST3 hosting. Set `MINIDAW_VERSION` to override the version string. The GitHub
Actions workflow in `.github/workflows/build.yml` runs the tests, builds the installer and attaches it and the portable
ZIP to a release when a tag such as `v0.11` is pushed. Details: [docs/dev/building.md](docs/dev/building.md).

## Architecture

| Path | Purpose |
|---|---|
| `src/dsp/` | Qt-free DSP: parameters, effects, synth, drum kit, Drummer generator, tempo map, WSOLA stretcher, MIDI files, tempo detection, loudness |
| `src/core/` | Qt-free engine: `Session`, project files, MIDI operations, translations |
| `src/platform/` | WASAPI audio, winmm MIDI, Media Foundation decoding, MiniDAW plug-in host |
| `src/ui/` | Qt-free software-rendered UI toolkit and the whole user interface |
| `src/native/` | The Windows shell (`Win32App.cpp`) and the VST3 host (`Vst3Native.cpp`) |
| `src/*.cpp`, `src/Engine.*`, `src/vst3/` | The old Qt user interface (kept as a portable build only) |
| `translations/`, `loops/`, `plugin-sdk/` | Language files, sample loops, plug-in SDK shipped next to the executable |
| `docs/` | User manual (`user/`), developer guide (`dev/`), project documents (`project/`) |

See [docs/dev/architecture.md](docs/dev/architecture.md) for the layers, the rules and the threads.

## Known limitations and roadmap

- Output uses MiniDAW's own WASAPI backend (shared mode, roughly 50 ms latency). ASIO or exclusive mode
  is planned.
- VST3 parameters are edited in the plug-in's own window; they are not yet exposed as automation lanes.
  Plug-in state is stored, but a plug-in that is missing on another computer is skipped when loading.
- Time stretching is a single constant ratio per region (taken at the region start) rather than a
  tempo-curve-aware flex.
- Drummer uses the built-in drum kit; no sampler or per-drum editing yet.
- No time-stretched recording, comping, score editor or audio-unit hosting. Planned: loop-browser tempo
  detection, more automation targets, Smart Tempo.

## Contributing

Issues and pull requests are welcome. Please keep the DSP code free of Qt dependencies and write code
comments in English. `bash tests/run.sh` runs the DSP unit tests (no Qt required). By contributing you agree that your work is licensed under the GPL.

## License

MiniDAW is free software: you can redistribute it and/or modify it under the terms of the
GNU General Public License as published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version. See [LICENSE](LICENSE) for the full text.
MiniDAW links against Qt, which is available under the LGPL v3 / GPL. The sample loops in `loops/`
were synthesized for this project and are released under the same license.
