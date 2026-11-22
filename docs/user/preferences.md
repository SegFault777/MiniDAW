# Preferences

**File > Preferences...** (`Ctrl+,`)

| Item | Description |
| --- | --- |
| Output device | The device that plays the sound. *System Default* follows the Windows default device. |
| Buffer size | 128 to 4096 samples (Vulkan) / 256 to 4096 (Native). Smaller reacts faster (shorter latency) but is more likely to crackle. Default 512-2048. |
| Input device | The microphone / audio interface for recording |
| Recording latency compensation | Moves recorded audio earlier by this many milliseconds so that it sits on the beat. (See [Recording](recording.md).) |
| MIDI input | A MIDI keyboard. The connected keyboard plays the selected Software Instrument track. |
| Language | Automatic (Windows language), English, Korean, Japanese, Chinese (Simplified), Spanish. Needs a **restart**. |
| Autosave every | Autosave interval in minutes. 0 switches it off. Default 2. |
| Loops folder | The loops folder the browser looks in |
| VST3 folders | VST3 search folders (separated by `;`). Empty uses the default folders. |
| Interface scale (Vulkan) | 0.75 to 2.0 times on top of the Windows display scale. Needs a restart. |
| Icon style (Vulkan) | Material Symbols Outlined, Rounded, Sharp or the Classic icons. See [Icons](icons.md). |
| Rescan plug-ins | Scan for plug-ins again |
| Play Test Tone | A short tone to check the output device |

The lists of audio and MIDI devices are read in the background (at the start and again each time Preferences opens). If Preferences answers *Looking for audio devices...* you opened it before the first reading was done: open it again after a moment. A device you plugged in just now shows up the next time you open Preferences.

Settings are stored in `native-settings.ini` (Native) or `vulkan-settings.ini` (Vulkan) in `%LOCALAPPDATA%\MiniDAW` - or in the `data` folder of a **portable edition** - together with the five most recent projects.

## Autosave and recovery
While you work the program makes autosave files in `%LOCALAPPDATA%\MiniDAW\autosave` (portable: `data\autosave`).
If the program did not close properly (power loss, crash), it asks **Restore?** at the next start. **File > Recover Autosaved Project** does the same on demand.
Please **save** a recovered project.
