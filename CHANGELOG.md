# Changelog

All notable changes to this project are documented in this file.

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
