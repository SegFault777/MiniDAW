# MiniDAW architecture

MiniDAW is moving from "a Qt application" to "an engine with a user interface on top". Version 0.7 completed
the first step (the engine no longer depends on Qt), version 0.8 the second (audio, MIDI and decoding no
longer depend on Qt either).

```
┌──────────────────────────────────────────────────────────────┐
│ User interface (Qt Widgets)   MainWindow, Timeline, PianoRoll,│
│                               Panels, Controls, Browser ...   │
├──────────────────────────────────────────────────────────────┤
│ Qt adapter                    Engine.cpp  hooks, signals,     │
│                               QSettings, QTranslator shell,   │
│                               VST3 + plug-ins (QLibrary)      │
├──────────────────────────────────────────────────────────────┤
│ platform (no Qt)              src/platform/  WASAPI audio,    │
│                               winmm MIDI, Media Foundation    │
│                               decoding, silent test device    │
├──────────────────────────────────────────────────────────────┤
│ core::Session (no Qt)         src/core/                       │
│   project model, undo, tempo, transport, recording, rendering,│
│   bounce / export, project files, autosave, MIDI operations   │
├──────────────────────────────────────────────────────────────┤
│ dsp (no Qt)                   src/dsp/  synth, drum kit,      │
│   effects, tempo map, time stretch, Drummer, MIDI files,      │
│   audio operations, MiniDAW plug-in API (plugin-sdk)          │
└──────────────────────────────────────────────────────────────┘
```

## Rules

* `src/dsp`, `src/core` and `src/platform` include only the C++ standard library (plus the Windows API in
  the Windows-only platform files) and each other. No `Q...` type may appear there. `tests/run.sh` compiles them with a plain `g++` on a machine without Qt, which enforces this.
* The core never talks to hardware or windows. It asks the platform layer through `core::Session::Hooks`:
  decode a file, start / stop the microphone, create a VST3 effect, translate a text, tell the user
  something, notify that the model changed.
* The user interface only uses `Engine`, which forwards to the `Session` (the same API as before v0.7)
  and turns hook calls into Qt signals. `QtBridge.h` converts `std::string` / `core::Color` to `QString` /
  `QColor` for drawing.
* Threads: the audio thread calls `Session::fillInt16` and `pushRecorded`; everything else runs on the user
  interface thread. The model is guarded by one mutex, the tempo map is an immutable shared pointer that is
  swapped atomically.

## Where things are

| Directory | Contents |
| --- | --- |
| `src/core/Base.h`, `Model.h` | constants, `Text` (translated text with `%1`), tracks, regions, notes, markers |
| `src/core/Session.*` | the engine: model edits, undo / redo, transport, recording, `renderBlock`, master bus |
| `src/core/ProjectIO.cpp` | project files (save, load, autosave), MIDI file import / export, audio import, bounce to WAV |
| `src/core/Features.cpp`, `MidiOps.*` | Bounce in Place, arpeggiator, quantize, scale quantize, humanize, chord trigger, legato, transpose, velocity ramp, repeat / join regions, tempo detection, pattern to MIDI |
| `src/core/Json.*`, `Files.*`, `AudioIO.*` | JSON, UTF-8 file helpers, WAV reader / writer, resampler, waveform peaks |
| `src/core/I18n.*` | translation table parser (used by the Qt `Translator` shell) |
| `src/platform/` | `Audio.*` + `AudioWasapi.cpp` (devices, output / input streams, silent "null" device), `Midi.*` (winmm), `Decode.*` (WAV + Media Foundation) |
| `src/Engine.*` | Qt adapter: owns the platform streams and a decode worker thread, hooks, signals |
| `src/*.cpp` (other) | the user interface |
| `tests/` | Qt-free tests of the DSP, the plug-in API and the whole core |

## What still depends on Qt

| Area | Qt classes it needs |
| --- | --- |
| Widgets: main window, timeline, piano roll, mixer / inspector, knobs, browser, preferences | `QWidget`, `QPainter`, `QMenu`, `QDialog` ... |
| Settings, language shell, plug-in lists | `QSettings`, `QTranslator`, `QLibrary` |
| VST3 host glue | `QString`, `QLibrary` |

Audio devices, MIDI input and file decoding no longer use Qt (v0.8). Replacing `QSettings` is left to the
UI toolkit step because only the user interface uses it.

## Next step towards a fully own engine

**Own UI toolkit** (Win32 + Direct2D): drawing primitives, a small widget set (button, slider, combo box,
menu, dialog, text field), then the screens one by one: Preferences, Browser, Mixer, Piano Roll,
Timeline, Main window. Because the screens only use `Engine`, each screen can be ported separately
while the others still run on Qt.
