# Adding a feature

Example: *Half Lengths*, which halves the note lengths of the selected MIDI region.

## 1. A pure function (`src/core/MidiOps.*`)
Make it a function whose input and output are both `std::vector<Note>`. It knows neither the screen nor `Session`, so it is the easiest thing to test.
```cpp
std::vector<Note> halveLengths(const std::vector<Note>& notes);
```

## 2. Connect it to the session (`src/core/Features.cpp` or `Studio.cpp`, `Session.h`)
`editNotes` takes care of locking, the undo point and the change notification, so this is one line:
```cpp
bool Session::halveLengthsRegion(int t, int r) { return editNotes(t, r, [&](const std::vector<Note>& n, double) { return halveLengths(n); }); }
```
Audio region functions change the `Region` directly (see `stripSilenceRegion`): call `checkpoint()` before changing the model and `changed(false)` at the end.

## 3. Tests (`tests/core_tests.cpp`)
One or two for the pure function and one through `Session` (including undo).

## 4. The screens
- **Vulkan interface:** add one `addCmd(...)` to `src/vulkan/StudioCommands.cpp`. The menu, the `Ctrl+K` palette, the `F1` sheet and the shortcut all come from that one table. A label that starts with `~` is a hidden alias. If the function needs parameters, add a `dlgXxx()` in `StudioDialogs.cpp` (the parameter-dialog engine). Test it in `tests/vulkan_tests.cpp` (the smoke test runs every command).
- **Native interface:** add `item("Half Lengths", "", [...], midi)` to `functionsMenu()` in `UiInput.cpp`; for parameters make a `dlgXxx()` in `UiDialogs.cpp` and declare it in `Ui.h` (the dialog pattern is in [ui-toolkit.md](ui-toolkit.md)). Test it in `ui_tests.cpp` through the menu or dialog.

## 5. Translations
Add the English text (`"Half Lengths"`, result messages) to `translations/ko.tsv ja.tsv zh_CN.tsv es.tsv`. Never add the same key twice.

## 6. Documentation
A row in the table of `docs/user/editing.md`, and one line in `CHANGELOG.md`.

## Checklist
- [ ] `bash tests/run.sh` passes
- [ ] No `Q...` types in `src/core` and the other Qt-free directories
- [ ] One undo returns everything to the original state
- [ ] No crash on empty input, out-of-range indices or invalid values
- [ ] Translations in all four languages
