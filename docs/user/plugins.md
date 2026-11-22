# Plug-ins

MiniDAW uses two kinds of plug-ins.

## MiniDAW plug-ins (`.dll`)
Small effects made with the plug-in SDK (`plugin-sdk/`). The installation folder contains two examples (Tremolo, Bitcrusher).
- Where they are searched: `<installation folder>\plugins` and `%LOCALAPPDATA%\MiniDAW\plugins` (in a portable edition: `data\plugins`)
- Put the file there and press **Preferences > Rescan plug-ins** (or restart); it then appears under *Effects* in the browser.
- To write your own, see `plugin-sdk\README.md`. One C header (`mdplugin.h`) is all you need.

## VST3 plug-ins
- Default search folders: `C:\Program Files\Common Files\VST3`, `%LOCALAPPDATA%\Programs\Common\VST3`
- To use other folders, write them in **Preferences > VST3 folders** separated by `;`, or use **Add VST3 Folder...**
- The scan runs in the background. When it is done the status bar says "Plug-in scan finished."
- Drop **effects** from the browser's Effects onto a track, **instruments** from Instruments onto a Software Instrument track.
- Open the plug-in's own window with the **E** button of a Devices card (or the *Editor* button in the inspector).
- The state (preset) of a plug-in is saved in the project file.

> If this installation was built without VST3 support, VST3 items do not appear and trying to load one shows a message.

*VST is a trademark of Steinberg Media Technologies GmbH.*

## If something goes wrong
- The plug-in is not in the list -> check the folder path and that it is a 64-bit (x64) plug-in, then press *Rescan plug-ins*. 32-bit plug-ins cannot be used.
- A plug-in makes the program hang -> take that plug-in out of the folder and start again.
