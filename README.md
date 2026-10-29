# MiniDAW

A lightweight, fast digital audio workstation for Windows, built with Qt 6 and inspired by the
workflow of Logic Pro. It stays approachable for beginners and still offers what a producer expects:
a multitrack arrangement, MIDI editing, a built-in synthesizer and **Drummer**, a **Loop Browser**,
channel strips with insert effects and sends, **VST3 plug-ins**, **tempo and time-signature tracks**,
**time stretching**, automation, recording and bouncing.

> MiniDAW is an independent open-source project. It is not affiliated with, endorsed by or
> sponsored by Apple Inc. "Logic Pro" is a trademark of Apple Inc.
> VST is a trademark of Steinberg Media Technologies GmbH.

**Version 0.4_1** · License: GPL-3.0-or-later

## Features

### Arrangement
- Multitrack **Tracks** area with a ruler that follows the time signature, a smart snap grid,
  zoom (Ctrl + wheel) and scrolling.
- Four track types: **Audio**, **Software Instrument**, **Drummer** and **Drum Machine**.
- Regions: move, resize, split at the playhead, duplicate, delete. Drag audio or MIDI files from
  Explorer or from the Loop Browser straight onto the timeline.
- **Cycle** range (right-drag on the ruler), metronome (accents follow the time signature) and an
  optional one-bar **count-in**. **Volume automation** per track. Undo / redo for edits and tempo changes.

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
- **Bounce to WAV** (16-bit, 44.1 kHz).

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
| New audio / instrument / drum machine track | `Ctrl+Alt+A` / `S` / `D` | New Drummer track | `Ctrl+Alt+R` |
| Undo / redo | `Ctrl+Z` / `Ctrl+Shift+Z` | Import audio | `Ctrl+I` |
| Save / open | `Ctrl+S` / `Ctrl+O` | Bounce | `Ctrl+B` |
| Preferences | `Ctrl+,` | | |

Musical Typing: `A W S E D F T G Y H U J K O L P ;` play a chromatic keyboard starting on C;
`Z` / `X` shift the octave. In the Piano Roll press `Q` to quantize. Double-click an empty part of an
instrument lane to create a MIDI region, or of a Drummer lane to create a Drummer region.

## Installing

Download `MiniDAW-<version>-setup.exe` from the [Releases](../../releases) page and run it. The
installer places MiniDAW in `Program Files`, adds a Start menu entry and can associate `.mdaw` files.
A portable ZIP is published next to every installer.

### No sound?
Open *Preferences > Audio*. The status line shows which device and format MiniDAW opened. Choose
another output device or a larger buffer and press *Play Test Tone*. MiniDAW tries the device's own
format automatically if 44.1 kHz / 16-bit is not accepted.

### VST3 plug-ins
Installers built by the release workflow include VST3 hosting. MiniDAW scans
`C:\Program Files\Common Files\VST3` and `%LOCALAPPDATA%\Programs\Common\VST3` at start-up; add more
folders in *Preferences > Plug-ins*. VST3 effects appear in the *Audio FX* slot menus and VST3
instruments in the Inspector's instrument menu. Plug-in state is saved with the project.

## Building from source

MiniDAW is built by a single shell script, `build.sh` (no CMake required).

Requirements: Windows 10/11, Visual Studio 2022 or newer (MSVC x64 tools), Git Bash (or MSYS2),
Qt 6.5 or newer (Qt 6.8 recommended) with the *Qt Multimedia* module, and optionally
[Inno Setup 6](https://jrsoftware.org/isinfo.php) for the installer.

```bash
# In Git Bash started from the "x64 Native Tools Command Prompt" (so that cl.exe is on PATH):
export QT_DIR=C:/Qt/6.8.3/msvc2022_64
export VST3_SDK_DIR=C:/dev/vst3sdk     # optional: git clone --recurse-submodules https://github.com/steinbergmedia/vst3sdk
./build.sh            # moc + compile + link  -> build/MiniDAW.exe
./build.sh deploy     # windeployqt           -> dist/MiniDAW and a portable ZIP
./build.sh installer  # Inno Setup            -> installer-out/MiniDAW-<version>-setup.exe
./build.sh all        # everything above
./build.sh clean
```

Without `VST3_SDK_DIR` (or if the SDK fails to build) MiniDAW is built without VST3 hosting. The VST3
SDK is available under GPL-3.0, which is compatible with MiniDAW's license. Set `MINIDAW_VERSION` to
override the version string. The GitHub Actions workflow in `.github/workflows/build.yml` runs
`bash build.sh all` and attaches the installer and the portable ZIP to a release when a tag such as
`v0.4_1` is pushed.

## Architecture

| Path | Purpose |
|---|---|
| `src/dsp/` | Qt-free DSP: parameters, effects, synth, drum kit, Drummer generator, tempo map, WSOLA stretcher, MIDI files |
| `src/Engine.*`, `src/Project.cpp` | Project model, real-time mixer, transport, recording, undo, save / load, audio device handling |
| `src/Timeline.*`, `src/PianoRoll.*` | Arrangement, tempo track and MIDI editors |
| `src/Panels.*`, `src/Controls.*`, `src/Browser.*` | Inspector, mixer, step sequencer, Drummer editor, loop browser, knobs, faders, meters |
| `src/MainWindow.*`, `src/Preferences.*`, `src/Theme.*`, `src/Translator.*`, `src/MidiInput.*` | Control bar, menus, settings, theme, languages, MIDI keyboards |
| `src/vst3/` | VST3 host (compiled only when the VST3 SDK is available) |
| `translations/`, `loops/` | Language files and sample loops shipped next to the executable |

Audio runs on a dedicated thread. The mixer renders any time range of the project from a pure function
of the playback position, so playback, looping and offline bouncing share one code path. Musical
positions are stored in beats and converted to audio frames through the tempo map, so tempo ramps and
time-signature changes affect everything consistently. The audio thread shares the project with the UI
through a short mutex-protected section; effect and synth parameters are lock-free atomics.

## Known limitations and roadmap

- Output goes through Qt Multimedia (WASAPI shared mode, roughly 50 ms latency). ASIO or exclusive mode
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
