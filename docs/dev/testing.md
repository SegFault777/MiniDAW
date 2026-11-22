# Testing

`bash tests/run.sh` builds and runs everything (`g++` or `clang++`, no Qt). It uses `set -e`, so if an early step fails the later ones do not run.

| File | Target |
| --- | --- |
| `dsp_timing_tests.cpp`, `dsp_music_tests.cpp`, `dsp_audioops_tests.cpp`, `dsp_effects_tests.cpp` | Tempo map, stretch, synth, Drummer, effects, audio operations |
| `native_plugin_tests.cpp` | The plug-in API (`plugin-sdk`) and example plug-ins |
| `core_tests.cpp` | The whole `Session`: editing, undo (including coalesced steps), project files (including snapshots, MIDI channels and the file version), MIDI operations and export channels, trimming audio regions, tempo detection, loudness, strip silence, the ten v0.12 functions, `AppPaths` |
| `platform_tests.cpp` | The null audio device with `Session`, recording, decoding, the translation table, the MiniDAW plug-in host |
| `ui_tests.cpp` | The Native screen: drawing, mouse / key input, dialogs, editors, browser, knobs, automation, the parity features |
| `vulkan_tests.cpp` | The Vulkan UI headless: tools, drags, palette, dialogs, piano roll, steps, snapshots, workspaces, the audio editor (fade handles, zoom, trim), IME composition in a text field, the glyph atlas and the glyph time budget, the Preferences device cache, a smoke run of every command, CPU-rendered screenshots |

## How the screen tests work
`NativeUi` draws into a `Canvas` (pixels in memory), so it can run without a window. A test builds a fake project, feeds events such as `mouseDown`, `mouseMove` and `key` directly and checks the state of the `Session`.
The Vulkan UI works the same way: `Studio` produces a `DrawList`, and `SoftRaster` renders it on the CPU with the same formulas as `shaders/ui.frag`. Both save PPM screenshots, so you can **see the screens without Windows or a GPU**.

Tips for writing tests
- Compute coordinates from the layout constants (Native: `kControlH`, `kToolbarH`, `kRulerH`, `kHeaderW`, track height 60). In the Vulkan tests use the same `Theme.h` values.
- If you need the screen state after an edit, call `u.paint(c)` first (`mouseDown` rereads the model but key input uses the snapshot of the last `paint`).
- For areas use accessors such as `pianoGridRect()`, `stepGridRect()`, `autoLaneRect()`, `browserRowRect(i)`.
- Vulkan: a test that opens a tab or panel that changes the layout (the Editor tab raises the bottom dock) should use its own `Rig`, otherwise the coordinates of every later test shift.
- Do not keep `const T& x = session.snapshot()[i]...`: `snapshot()` returns a copy, so the reference dangles. Copy the element.
- To test code that is slow only on Windows (GDI glyphs), wrap `FontSource` in a source that sleeps (see the *slow glyph source* tests) and set `Fonts::setFrameBudgetMs`.
- Vulkan: the double-click detector lives in `Gui` (0.4 s, 5 px). Two quick presses at one spot become a double click, so call `wait()` between them when you want two single clicks.

## What cannot be tested
Windows-only code (`src/native/`, `src/vulkan/VkMain.cpp`, `src/vulkan/VkRenderer.cpp`, `src/platform/AudioWasapi.cpp`, `Midi.cpp`, `Decode.cpp`) is not run on Linux. `VkRenderer.cpp` is only syntax-checked against the mock header; the GLSL shaders are only checked to exist. For these files the CI log (and running on a real GPU) is the only verification. So keep them **small and simple** and put decision logic in `src/ui`, `src/vulkan` (non-Windows files) or `src/core`, where it can be tested.
