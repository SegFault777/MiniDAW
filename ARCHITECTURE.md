# MiniDAW architecture

MiniDAW is moving from "a Qt application" to "an engine with a user interface on top". Version 0.7 completed
the first step: the engine no longer depends on Qt.

```
┌──────────────────────────────────────────────────────────────┐
│ User interface (Qt Widgets)   MainWindow, Timeline, PianoRoll,│
│                               Panels, Controls, Browser ...   │
├──────────────────────────────────────────────────────────────┤
│ Platform layer (Qt)           Engine.cpp  audio output/input, │
│                               file decoding, QSettings,       │
│                               translations, VST3 + plug-ins   │
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

* `src/dsp` and `src/core` include only the C++ standard library and each other. No `Q...` type may appear
  there. `tests/run.sh` compiles them with a plain `g++` on a machine without Qt, which enforces this.
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
| `src/core/Features.cpp`, `MidiOps.*` | Bounce in Place, arpeggiator, quantize with swing, scale quantize, humanize, chord trigger |
| `src/core/Json.*`, `Files.*`, `AudioIO.*` | JSON, UTF-8 file helpers, WAV reader / writer, resampler, waveform peaks |
| `src/Engine.*` | Qt adapter: audio devices, `QAudioDecoder`, hooks, signals |
| `src/*.cpp` (other) | the user interface |
| `tests/` | Qt-free tests of the DSP, the plug-in API and the whole core |

## What still depends on Qt (about 5,700 lines)

| Area | Lines | Qt classes it needs |
| --- | --- | --- |
| Widgets: main window, timeline, piano roll, mixer / inspector, knobs, browser, preferences | ~3,600 | `QWidget`, `QPainter`, `QMenu`, `QDialog` ... |
| Audio devices and decoding | ~350 | `QAudioSink`, `QAudioSource`, `QAudioDecoder`, `QMediaDevices` |
| Settings, translations, plug-in lists | ~300 | `QSettings`, `QTranslator`, `QLibrary` |
| VST3 host glue | ~470 | `QString`, `QLibrary` |

## Next steps towards a fully own engine

1. **Platform layer without Qt**: WASAPI output / input, WinMM MIDI, a single-header decoder library for
   MP3 / FLAC / OGG, a settings file and the translation tables read by the core. The `Hooks` interface is
   the seam; each piece can replace its Qt counterpart on its own.
2. **Own UI toolkit** (Win32 + Direct2D): drawing primitives, a small widget set (button, slider, combo box,
   menu, dialog, text field), then the screens one by one: Preferences, Browser, Mixer, Piano Roll,
   Timeline, Main window. Because the screens only use `Engine`, each screen can be ported separately
   while the others still run on Qt.
