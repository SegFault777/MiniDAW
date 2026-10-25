# Changelog

All notable changes to this project are documented in this file.

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
- Positions are stored in beats so MIDI follows tempo changes.
- Audio output now runs continuously so instruments can be played while the transport is stopped.

## [0.1] - 2026-10-24

### Added
- Initial multitrack timeline with drag-and-drop audio import, 16-step drum machine,
  basic mixer, metronome, loop and WAV export.
