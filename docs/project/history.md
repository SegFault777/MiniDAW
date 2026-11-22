# History (summary)

| Version | What was done |
| --- | --- |
| v0.4_1 to v0.5_1 | Region copy / paste, track duplication, audio input and recording latency compensation, bounce dialog, build fixes |
| v0.6 | Pan / send automation, autosave and recovery, markers, track move / colour, chorus and filter, recent projects, tap tempo, plug-in SDK |
| v0.7 | **The Qt-free core was split off** (`Session`, `Json`, `Files`, `AudioIO`, `MidiOps`), six Logic-style functions |
| v0.8 / 0.8_1 | **The Qt-free platform layer** (WASAPI, winmm, Media Foundation), seven edit functions (legato, transpose, crescendo, repeat, join, tempo detection, pattern to MIDI), build fixes |
| v0.9 | **The Qt-free UI toolkit** and the first screen of the Native app (control bar, arrangement, mixer, menus), eight edit functions |
| v0.10 / 0.10_1 | Dialogs, inspector, piano roll, step sequencer, Drummer, automation and tempo lane, loop browser, preferences and translations added to Native. Fix of the `min` / `max` macros of `windows.h` |
| v0.11 | **Native became the product:** installer, MiniDAW plug-ins and VST3 hosted without Qt, UI redesign from the Logic / Ableton comparison (browser with drag and drop, device-chain knobs, track heights, help bar), multi-select in the piano roll, documents reorganised |
| **v0.12** | **Executables renamed** (Native = `MiniDAW.exe`, Qt = `MiniDAWQt.exe`), **portable mode**, documents translated to English, **a new GPU interface built on Vulkan** (`MiniDAWVulkan.exe`) with workspaces, a command palette and a Logic / Ableton-style piano roll, ten functions taken from five DAWs, Qt-only features added to Native; later in the cycle: Logic Pro project import, a 19-page tutorial, icon styles (Material Symbols), a **waveform editor for audio regions** (fade handles, zoom, trim at the playhead), the MIDI channel of a track, IME typing, project file version 4, coalesced undo steps, and a glyph time budget with a background device-list cache against freezes |

## What we learned
- Work that is slow only on the target system (GDI glyphs for Korean, device enumeration) cannot be found on the test machine. Give it a budget or move it off the UI thread by design, and test the design with a deliberately slow fake.
- Splitting the layers and **widening the part that is tested on Linux** narrowed most build failures to `include` and macro problems in Windows-only files.
- If the screen is also drawn into pixels in memory, it can be **tested without a window and checked by eye through screenshots**. The Vulkan UI keeps this by rendering on the CPU with a twin of its shader.
- Looking translations up "where it is drawn" removed the need to write translation code for each new screen.
- One command table for menus, shortcuts, palette and help sheet keeps them from ever disagreeing.
