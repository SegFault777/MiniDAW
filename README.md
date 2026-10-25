# MiniDAW

A lightweight, fast digital audio workstation for Windows, built with Qt 6 and inspired by the
workflow of Logic Pro. It is designed to be approachable for beginners while offering the
tools a producer expects: a multitrack arrangement, MIDI editing, a built-in synthesizer,
channel strips with insert effects and sends, automation, recording and bouncing.

> MiniDAW is an independent open-source project. It is not affiliated with, endorsed by or
> sponsored by Apple Inc. "Logic Pro" is a trademark of Apple Inc.

**Version 0.2** · License: GPL-3.0-or-later

## Features

### Arrangement
- Multitrack **Tracks** area with a ruler, smart snap grid, zoom (Ctrl + wheel) and scrolling.
- Three track types: **Audio**, **Software Instrument** and **Drum Machine**.
- Regions: move, resize, split at the playhead, duplicate, delete. Drag audio files from
  Explorer straight onto the timeline.
- **Cycle** range (right-drag on the ruler), metronome, optional one-bar **count-in**.
- **Volume automation** per track (press `A`, click to add breakpoints, right-click to delete).
- Unlimited-style **undo / redo**.

### Recording and instruments
- **Audio recording** into an armed audio track.
- **MIDI recording** from the computer keyboard using **Musical Typing** (`Ctrl+K`).
- Built-in 16-voice subtractive synthesizer: two band-limited oscillators, sub oscillator,
  resonant filter with envelope, ADSR and six presets.
- **Piano Roll** editor: draw, move and resize notes, velocity lane, grid and quantize.
- 16-step **Step Sequencer** with a synthesized drum kit.

### Mixing
- Inspector with a channel strip for the selected track (fader, pan, mute, solo, record enable).
- Full **Mixer** with level meters and a master fader.
- Insert effects: **Channel EQ**, **Compressor**, **Echo**, **Reverb**, **Overdrive**.
- Two aux sends (Reverb and Echo buses) and a master **Limiter**.
- **Bounce to WAV** (16-bit, 44.1 kHz).

### Projects
- Save and open `.mdaw` projects (JSON). Recordings are stored next to the project file.

## Keyboard shortcuts

| Action | Shortcut | Action | Shortcut |
|---|---|---|---|
| Play / pause | `Space` | Record | `R` |
| Stop (twice = go to start) | `.` | Go to beginning | `Return` |
| Cycle on / off | `C` | Metronome | `K` |
| Inspector | `I` | Mixer | `X` |
| Smart Controls | `B` | Piano Roll | `P` |
| Step Sequencer | `Shift+S` | Show / hide editors | `E` |
| Volume automation | `A` | Split at playhead | `Ctrl+T` |
| Duplicate region | `Ctrl+D` | Delete | `Del` |
| New audio / instrument / drum track | `Ctrl+Alt+A` / `S` / `D` | Musical Typing | `Ctrl+K` |
| Undo / redo | `Ctrl+Z` / `Ctrl+Shift+Z` | Import audio | `Ctrl+I` |
| Save / open | `Ctrl+S` / `Ctrl+O` | Bounce | `Ctrl+B` |

Musical Typing: `A W S E D F T G Y H U J K O L P ;` play a chromatic keyboard starting on C;
`Z` / `X` shift the octave. In the Piano Roll press `Q` to quantize.

## Installing

Download `MiniDAW-<version>-setup.exe` from the
[Releases](../../releases) page and run it. The installer places MiniDAW in
`Program Files`, adds a Start menu entry and can associate `.mdaw` files. A portable ZIP is
published next to every installer.

## Building from source

Requirements: Windows 10/11, Visual Studio 2022 Build Tools (MSVC), CMake 3.21+,
Ninja and Qt 6.5 or newer (Qt 6.8 recommended) with the *Qt Multimedia* module.

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.8.0/msvc2022_64
cmake --build build
windeployqt --release build\MiniDAW.exe
```

The GitHub Actions workflow in `.github/workflows/build.yml` builds the application, creates the
installer with [Inno Setup](https://jrsoftware.org/isinfo.php) and attaches both to a release when
a tag such as `v0.2` is pushed.

## Architecture

| Path | Purpose |
|---|---|
| `src/dsp/` | Qt-free DSP: lock-free parameters, effects and the synthesizer |
| `src/Engine.*`, `src/Project.cpp` | Project model, real-time mixer, transport, recording, undo, save / load |
| `src/Timeline.*`, `src/PianoRoll.*` | Arrangement and MIDI editors |
| `src/Panels.*`, `src/Controls.*` | Inspector, mixer, step sequencer, knobs and meters |
| `src/MainWindow.*`, `src/Theme.*` | Control bar, menus, shortcuts and the dark theme |

Audio runs on a dedicated thread. The mixer renders any time range of the project from a pure
function of the playback position, so playback, looping and offline bouncing share the same code
path. The audio thread shares the project with the UI through a short mutex-protected section;
effect and synth parameters are lock-free atomics.

## Known limitations and roadmap

- Output goes through Qt Multimedia (WASAPI shared mode, roughly 50 ms latency). ASIO or exclusive
  mode support is planned.
- Fixed 4/4 time signature and a single constant tempo; no tempo or marker tracks yet.
- No VST3 / AU plug-in hosting, MIDI hardware input, time stretching, comping or score editor.
- Planned: Drummer-style pattern generation, loop browser, flex time, more automation targets.

## Contributing

Issues and pull requests are welcome. Please keep the DSP code free of Qt dependencies and write
code comments in English. By contributing you agree that your work is licensed under the GPL.

## License

MiniDAW is free software: you can redistribute it and/or modify it under the terms of the
GNU General Public License as published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version. See [LICENSE](LICENSE) for the full text.
MiniDAW links against Qt, which is available under the LGPL v3 / GPL.
