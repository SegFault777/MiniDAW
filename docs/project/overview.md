# Project overview

## In one line
**MiniDAW** is a light and fast open-source (GPL-3.0-or-later) digital audio workstation for Windows, inspired by the way Logic Pro works and borrowing the best ideas of four other DAWs.

## Who it is for
- People making their first song: those who want sound quickly with the Drummer, loops and the browser
- People who want a light tool: a small installer, a fast start, and it runs on old PCs
- People who want to modify it: the source is small and the layers are clear (see the developer documents)

## Goals
1. **Light:** no external UI framework, only Win32 / Vulkan and our own UI toolkits.
2. **Sound at once:** instruments, drums, effects and loops are included from the start.
3. **Trustworthy:** autosave and recovery, undo, and the core and the screens are tested automatically on Linux.
4. **Open:** a plug-in SDK and VST3 are supported, and the project file is human-readable JSON.
5. **For everyone:** Korean, English, Japanese, Chinese (Simplified) and Spanish.
6. **Portable:** the Native and Vulkan versions run from a folder or a USB stick (`MiniDAW.portable`).

## What it does not do (for now)
- Score editing
- Video sync
- Screens for macOS / Linux (the engine is portable, but the window and audio device code is Windows-only)
- Hosting 32-bit plug-ins

## Name and trademarks
Logic Pro and macOS are trademarks of Apple Inc.; Ableton Live of Ableton AG; FL Studio of Image-Line; Cubase and VST of Steinberg Media Technologies GmbH; Pro Tools of Avid Technology; Windows of Microsoft Corporation.
MiniDAW has no relation to these companies, does not copy their products (see [ui-design.md](ui-design.md) for which ideas were taken and how they were solved differently) and uses none of their names, icons or code.

## Parts
| Part | Location | Status |
| --- | --- | --- |
| Engine (core, DSP, platform) | `src/core`, `src/dsp`, `src/platform` | No Qt, tested on Linux |
| **Native app** `MiniDAW.exe` | `src/ui`, `src/native` | Installer and portable zip |
| **Vulkan app** `MiniDAWVulkan.exe` | `src/vulkan` | Portable zip; GPU parts not yet verified on Windows |
| Old Qt app `MiniDAWQt.exe` | `src/*.cpp`, `src/Engine.*`, `src/vst3` | Portable zip only, no longer extended |
