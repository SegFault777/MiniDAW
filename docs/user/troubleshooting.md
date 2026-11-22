# Troubleshooting

## There is no sound
1. Is the name of the audio device on the right of the status bar? If it says "No audio output", pick a device in **Preferences > Output device**. *Play Test Tone* helps to check it.
2. Does the master level meter move? If so, the program is making sound: check the Windows volume and your speakers.
3. Is **M** (mute) on for the track, or **S** (solo) on for another track?
4. Does the Software Instrument track have an **instrument**? Check in the inspector.

## Sound crackles or drops out
Raise **Preferences > Buffer size** by one step (for example 512 -> 1024). Closing other heavy programs or using fewer plug-ins helps too.

## "Audio device lost"
You unplugged the audio device or Windows switched devices. Restart MiniDAW.

## Recording does not work / comes in too late
- **R** must be on for the track ("Arm a track with the R button first.").
- Check that the microphone is not blocked in Windows Settings > Privacy > Microphone.
- If it comes in late, adjust **Recording latency compensation**.

## A file cannot be opened or imported
- WAV, MP3, AAC/M4A, FLAC and WMA open. OGG may not open with the default Windows codecs.
- Project files are `.mdaw`.

## Preferences says "Looking for audio devices..."
The program reads the audio and MIDI device lists in the background, because some drivers (Bluetooth, virtual audio cables) take seconds to answer. Open Preferences again after a moment. If it keeps saying so, unplug or disable the device that hangs (a Bluetooth headset that is switched off is a common one) and try again.

## Preferences or the tutorial open slowly, and Korean or Japanese text appears a little at a time
The first time such a window shows many characters that were never drawn before, the program prepares only a few of them per frame so that the window keeps answering. The text fills in within a moment and stays prepared until you close the program. If a window freezes for seconds instead, please report it with the Windows version and the language you use.

## Shortcuts do not work while the Korean (Hangul) keyboard is on (Vulkan)
Shortcuts work in Hangul mode whenever no text field has the focus. Inside a text field the IME is on on purpose, so that you can type Korean: finish with `Enter` (keeps what you typed) or `Esc` (cancels the edit), or click outside, and the shortcuts are back. If they do not work even then, report it with the name of your IME.

## "Opened a project saved by a newer MiniDAW"
The project was saved by a newer version than the one you run. It opens with everything this version knows, and the settings it does not know (for example features added later) are left out. Do not save over the original: use **Save As...** and keep the file of the newer version.

## The program closed strangely
Start it again: it offers to restore the autosaved project. Save it after restoring.
If the problem repeats, delete `native-settings.ini` / `vulkan-settings.ini` in `%LOCALAPPDATA%\MiniDAW` (portable: in `data`) - this resets the settings - and try again.

## The Vulkan version does not start
- "vulkan-1.dll was not found": update the graphics driver. You can use `MiniDAW.exe` meanwhile.
- "The shader files ... are missing": the `shaders` folder must sit next to `MiniDAWVulkan.exe`. Use the whole zip, not the exe alone.
- A black or frozen window: update the driver; if it persists, report it with the name of your graphics card (shown by the error message or Task Manager).

## Text looks blurry or too small (Native)
The Native version is stretched by Windows on high-resolution screens and can look a little soft. The **Vulkan version** is drawn at the display scale and stays sharp; its *Interface scale* preference makes everything larger or smaller.

## Reporting
Send bugs and suggestions as GitHub issues, with the version (`About MiniDAW`), what you tried to do and the message that appeared.
