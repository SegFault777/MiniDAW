# Roadmap

In order of priority. This list is a direction, not a promise.

## Near term (around v0.12 / v0.13)
1. **Build and run the Vulkan interface on Windows.** `VkMain.cpp`, `VkRenderer.cpp`, the shaders and the new `build.sh` / CI parts were only written and syntax-checked on Linux. Fix the errors, then compare a real GPU's output with the CPU screenshots.
2. **Check CI and stabilise.** Compile the Native build (especially VST3) and the `win32` changes (portable paths, folder dialog) on Windows for the first time.
3. **Native review of the translations.** All new texts have `ko`, `ja`, `zh_CN` and `es` entries; the `ja`, `zh_CN` and `es` ones were written without a native speaker and need a review.
4. **Check on Windows what was written on Linux** (nothing below could be tried on a real machine, only tested headless): the Korean / Japanese IME in text fields and shortcuts in Hangul mode (`VkMain.cpp`, `imm32`), that Preferences and the tutorial open without a freeze (glyph budget and the device-list cache), the Material Symbols icon styles, and the gapless MP3 start delay of the Windows encoder.
5. ~~Performance~~ done: playback refreshes only the meters; the whole project is copied when it changes.
6. ~~Project file compatibility~~ done: the file version is 4 and a project from a newer version opens with a warning.
7. **Decide the future of the Qt version** once nothing exists only in Qt (the Native and Vulkan versions already have its features).

## Next
- **Session view (clip launcher):** an engine function that starts and stops clips on the beat, and a grid screen.
- **Audio editor, next steps:** a gain handle, trimming by dragging the region edges (and bringing a cut part back without undo), selection-based editing (cut, silence, fade a range), zoom with a scroll bar.
- **Flex editing** of audio regions (beat markers), pitch correction.
- A text-input IME that also shows candidate lists inside the program (today the system's windows are placed at the caret).
- More automation targets (plug-in parameters), curve shapes.
- Choice of metronome sound, pre-roll.
- A "package save" that includes the samples in the project.
- Touch and pen input, accessibility.

## Long term
- ASIO or WASAPI exclusive mode for lower latency
- Porting to macOS / Linux (only the `platform` and shell layers must be rewritten; Vulkan helps here)
- Score view
- Instruments and MIDI effects in the plug-in SDK

## Decided against
- Introducing an external UI framework (Qt and the like) again
- Cloud accounts or online features
