# Architecture

MiniDAW moved from "a Qt program" to **"screens on top of an engine"**. There are now three front ends over the same Qt-free engine:

| Executable | Source | What it is |
| --- | --- | --- |
| `MiniDAW.exe` | `src/ui` + `src/native` | The **Native** version: software-rendered, Qt-free, installer and portable package |
| `MiniDAWVulkan.exe` | `src/vulkan` | The **Vulkan** interface: written from scratch, drawn by the GPU, workspaces and a command palette |
| `MiniDAWQt.exe` | `src/*.cpp`, `src/Engine.*`, `src/vst3/` | The old **Qt** version, kept as a portable build for comparison; no longer extended |

```
┌─────────────────────────────────────────────────────────────────────┐
│ ui (no Qt)        src/ui/      Canvas, NativeUi, Dialog, editors,    │
│                                browser, translations                 │
│ vulkan (no Qt)    src/vulkan/  DrawList, Fonts, Gui, Studio*, the     │
│                                SoftRaster twin, VkRenderer, shaders   │
├─────────────────────────────────────────────────────────────────────┤
│ native (Windows)  src/native/Win32App.cpp  window, GDI fonts, file   │
│                   dialogs, settings, autosave; Vst3Native.cpp         │
│ VkMain (Windows)  src/vulkan/VkMain.cpp    window, input, GDI glyphs, │
│                   audio / MIDI / plug-ins / settings                  │
├─────────────────────────────────────────────────────────────────────┤
│ platform (no Qt)  src/platform/  WASAPI, winmm MIDI, Media Foundation │
│                                  decoding, MiniDAW plug-ins           │
├─────────────────────────────────────────────────────────────────────┤
│ core::Session     src/core/  model, undo, tempo, transport,           │
│                              recording, rendering, project files,     │
│                              MIDI operations, AppPaths                │
├─────────────────────────────────────────────────────────────────────┤
│ dsp               src/dsp/   synth, drum kit, effects, tempo map,     │
│                              time stretch, Drummer, analysis, plug-in │
│                              API                                      │
└─────────────────────────────────────────────────────────────────────┘
```

## Rules
1. `src/dsp`, `src/core`, `src/platform`, `src/ui` and `src/vulkan` use **only the C++ standard library** (Windows-only files in `src/platform`, `src/native` and `src/vulkan/VkMain.cpp` / `VkRenderer.cpp` may use the Windows API / Vulkan). No `Q...` types. `tests/run.sh` compiles these directories with plain `g++`, so the rule is enforced by compilation.
2. The core knows neither hardware nor windows. It talks to the outside only through `core::Session::Hooks` (file decoding, starting / stopping the microphone, creating effects and instruments, saving / restoring plug-in state, messages, the "model changed" notification).
3. A UI calls only the public API of `core::Session`. It never touches the internals of `Session`.
4. Threads: the audio thread calls only `Session::fillInt16` and `pushRecorded`; everything else runs on the UI thread. The model is protected by one mutex and the tempo map is replaced as a whole through an immutable `shared_ptr`. Decoding and plug-in scanning threads hand their results to the UI thread with `PostMessage`.
5. **One command registry per UI.** In the Vulkan interface a feature is added once in `StudioCommands.cpp`; menus, palette, `F1` sheet and shortcuts are generated from it.

## Directory map
| Location | Contents |
| --- | --- |
| `src/core/Base.h`, `Model.h` | Constants, `Text`, tracks, regions, notes, markers |
| `src/core/Session.*` | The engine: editing, undo / redo, transport, recording, `renderBlock`, master bus |
| `src/core/ProjectIO.cpp` | Project files (save / load / autosave, mix snapshots), MIDI files, audio import, bounce |
| `src/core/Features.cpp`, `MidiOps.*` | Bounce in Place, arpeggiator, quantize, scale quantize, humanize, chord trigger, legato, transpose, velocity ramp, repeat / join, tempo detection, pattern to MIDI, fix overlaps, remove duplicates, fixed length, reverse, invert, velocity scale, strip silence, loudness; stamps, riffs, Logical Editor, groove, stretch, strum, double pattern, chord detection |
| `src/core/Studio.cpp` | Session extensions of v0.12: adapt project tempo to region, groove templates, Capture MIDI, riff, Logical Editor, mix snapshots, Tab to Transient, Beat Detective |
| `src/core/AppPaths.*` | Folder resolution for the installed and the portable mode (`MiniDAW.portable` marker) |
| `src/core/Json.*`, `Files.*`, `AudioIO.*`, `I18n.*` | JSON, UTF-8 files, WAV / resampler / peaks, translation table |
| `src/dsp/` | Synth, effects, `TempoMap`, stretch, `Drummer`, `TempoDetect`, `AudioAnalysis` (loudness, silence, transients), `NativeEffect` (plug-in API wrapper) |
| `src/platform/` | `Audio*` (WASAPI / null device), `Midi*`, `Decode*`, `PluginHost*` (MiniDAW plug-ins) |
| `src/ui/` | `Canvas`, `FontData`, `Ui*` (NativeUi), `Dialog`; see [ui-toolkit.md](ui-toolkit.md) |
| `src/native/` | `Win32App.cpp`, `Vst3Native.*` |
| `src/vulkan/` | `Draw`, `Text`, `SoftRaster`, `Gui`, `Theme.h`, `Icons.h`, `Studio*`, `VkRenderer`, `VkMain`, `shaders/` |
| `plugin-sdk/` | The plug-in C API (`mdplugin.h`) and examples |
| `translations/` | `ko`, `ja`, `zh_CN`, `es` (`*.tsv`, "English<TAB>translation") |
| `installer/` | Inno Setup scripts (`MiniDAW.iss` = Native, `MiniDAW-Qt.iss` = old Qt) |
| `tests/` | All Qt-free tests (`run.sh`); `tests/mock/vulkan/` holds a fake `vulkan.h` for compile checks |
| `tools/` | `gen_font.py` (generates the built-in font table) |

## The project file
A project is one JSON file (`ProjectIO.cpp`, `save` / `load`), with the audio next to it. The top-level `version` is `kProjectVersion` (**4** since v0.12; `Session::projectFileVersion()`). New optional fields (`group`, `snapshots`, `midiChannel`) are written only when they differ from the default and are ignored by older programs, which is why the number only moves when a new program should *warn*: `Session::loadedFromNewerVersion()` is true when the file's number is higher than the program's, and both UIs say that some settings may be missing. A file without `version` reads as 0 (very old). Raise `kProjectVersion` when a change would make an older program misread a file, not for every new optional field, and change the `projectFileVersion() == 4` check in `core_tests.cpp` with it.

## Data flow at a glance
```
mouse / keys (Win32) -> NativeUi::mouseDown ... / Studio frame loop -> Session::setVolume / moveRegion / ...
                                                  | (model changes, Hooks::changed)
timer (30 fps) -> NativeUi::tick() -> paint(Canvas) -> SetDIBitsToDevice        (Native)
frame loop     -> Studio -> DrawList -> VkRenderer (GPU)  /  SoftRaster (tests) (Vulkan)
audio device thread -> Session::fillInt16 (sound) / pushRecorded (recording)
```
