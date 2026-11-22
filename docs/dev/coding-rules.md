# Coding rules

## General
- C++20. Exceptions only at boundaries (reading files and the like); errors are returned as `bool` / `std::string* error`.
- Follow the layer rules in [architecture.md](architecture.md). No UI or window code under `src/core`.
- A `Session` method that changes the model calls **`checkpoint()` before the change** and `changed(...)` after it. If one gesture changes values many times (a drag), call `checkpoint()` **once at the start** and only change values afterwards. If the caller cannot tell where a gesture starts (a slider that sets a value every frame), use `checkpointCoalesced(tag)`: a change with the same tag within 0.6 s joins the previous step. `setRegionAudio` has an `undoStep` argument for callers that did the checkpoint themselves. Note edits through `setRegionNotes` do not checkpoint, so call `checkpoint()` first.
- Never call a UI object (`NativeUi`, `Studio`) from a thread other than the UI thread; pass results from other threads with `PostMessage`.
- Inside the audio callback (`fillInt16`, `pushRecorded`) avoid memory allocation, file I/O and long locks.

## Strings and translations
- Write user-visible English text as it is (the canvas / `Studio::tr` translates it); if numbers are mixed in, use the `%1` pattern.
- Vulkan UI: build messages that contain numbers or names with `Studio::fmt("Saved %1", value)` (pattern first, then fill in); never concatenate English pieces, because the translation table looks up the whole pattern.
- Core messages go through `Session::tr`.
- Paths and strings are **UTF-8** `std::string` inside. Convert with `widen` / `narrow` only where the Windows API is called.

## Vulkan UI habits
- Coordinates are dp (logical units); `DrawList` applies the scale and pixel-snaps edges.
- Draw and handle input in one place (`g.interact`). Several edits in one frame make `m_tracks` indices stale: after a deletion check `m_modelDirty` and do one deletion per frame.
- Add a command with `addCmd` in `StudioCommands.cpp`; do not bind keys elsewhere. Unconditionally active commands must not share a shortcut (a test checks it).
- Whenever the mouse is over the piano roll, arrows / Delete / Ctrl+C V X D A / Q / B belong to the roll, not to the global commands.

## Frequent MSVC errors
| Symptom | Cause and fix |
| --- | --- |
| `C4003 min/max`, "`ParamSet` is not defined" and similar chains | `NOMINMAX` is missing before `windows.h`. Define it at the top of the file and also use `-DNOMINMAX` |
| `std::clamp` / `std::min` not found | `#include <algorithm>` is missing (GCC gets it through other headers, so it is invisible on Linux) |
| A `static` function used before its definition | Put a declaration at the top of the file |
| A member used before its declaration inside a class | Reorder the members; MSVC and GCC differ in what they accept |
| `fopen` / `sscanf` warnings | `-D_CRT_SECURE_NO_WARNINGS` (already on) |
| Link error `LNK2005` (CRT conflict) | Native and the VST3 SDK are both `-MT`. Do not mix |
| `error C2628: 'TextStyle' followed by 'char'` | `<windows.h>` (rpcndr.h) defines `small` as a macro for `char`. `src/vulkan/Theme.h` does `#undef small`; do the same if you name something `small`, `near` or `far` |
| `std::min` / `std::max` with `LONG` and `int` | The types must match: write `std::min<int>(a, b)` when mixing Windows types (`LONG`, `DWORD`) with `int` |
| `warning C4005: 'NOMINMAX': macro redefinition` | The build already passes `-DNOMINMAX`; use `#ifndef NOMINMAX` before defining it in a file |
| The window is black while its edge is dragged | Windows runs a modal loop while sizing, so the main loop does not render. `VkMain.cpp` draws from `WM_SIZE` / `WM_PAINT` (`renderNow`); keep that when you change the window code |
| `LNK2019` for `ThreadChecker::create`, `StringConvert::convert` or `IPlugFrame::iid` | A VST3 SDK source file is missing from the build. The place of these files changes between SDK versions, so `build.sh` looks for them by name / content; add a new file name to `vst3_named_sources` if another symbol is missing |
| Vulkan: a struct field that does not exist | `tests/mock/vulkan/vulkan.h` is only a stand-in; check the field against the real Khronos header |

## Commit messages and versions
- The version string appears in `src/main.cpp`, `src/MainWindow.cpp`, `src/native/Win32App.cpp`, `src/vulkan/VkMain.cpp`, `build.sh`, `.github/workflows/build.yml`, `installer/*.iss` and `README.md`. Change them all together and add an entry to `CHANGELOG.md`.
- A hotfix gets an underscore number like `0.10_1`.
