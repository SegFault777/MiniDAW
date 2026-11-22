# Plug-in hosting

## MiniDAW plug-ins (`src/platform/PluginHost.*`, `src/dsp/NativeEffect.*`, `plugin-sdk/`)
- A plug-in is a shared library that implements the C API in `plugin-sdk/mdplugin.h` (`mdplugin_count`, `mdplugin_get`).
- `scanNativePlugins(folders)` opens the `.dll/.so/.dylib` files of the folders, checks the descriptor (`MdEffectDesc`, with `dsp::validDescriptor`) and adds it to the list. A file that is already open is not opened again, and libraries in use stay open until the program ends (the descriptor pointers must stay valid).
- A project stores it as `native:<plug-in id>`; `createNativePlugin(id)` returns a `dsp::Effect` wrapped in `dsp::NativeEffect`.
- The tests (`platform_tests.cpp`) compile the examples to shared libraries, scan the folder and process audio. **All of this is verified on Linux too.**
- At start-up the apps scan `<install folder>\plugins` and `%LOCALAPPDATA%\MiniDAW\plugins` **synchronously** (a project being loaded may need a plug-in at once). In portable mode the user folder is `<exe folder>\data\plugins`.

## VST3 (`src/native/Vst3Native.*`)
- It uses the hosting helpers of the Steinberg VST3 SDK (`Module`, `hostclasses`). The SDK is not in the repository; point `VST3_SDK_DIR` at it.
- The header (`Vst3Native.h`) contains no SDK types. Without `MINIDAW_VST3` everything is an inline empty function, so the rest of the code always compiles.
- API: `scan`, `createEffect`, `createInstrument`, `openEditor`, `saveState`, `loadState`, `isVst3`.
- A project stores it as `vst3:<module path>|<class id>`, and the plug-in state is stored as a base64 string.
- **Editor window:** `openEditor` creates an ordinary Win32 window and attaches the `IPlugView` to its `HWND`. `IPlugFrame::resizeView` fits the window. There is one window per plug-in (an open one is brought to the front). When the window is closed the view is released after `removed()` and `setFrame(nullptr)`.
- This file **needs Windows and the SDK to compile**, so Linux tests cannot verify it. The CI log is the only check, and if the build fails `build.sh` builds again without VST3.
- Plug-in `process` is called on the audio thread, so never mix UI objects into plug-in calls.

## Connection on the app side (`Win32App.cpp`, `VkMain.cpp`)
`Session::Hooks` `makeEffect`, `makeInstrument`, `saveState` and `loadState` lead to the hosts above. The UI reaches plug-ins only through `Host::plugins` (the list), `Host::rescanPlugins` and `Host::openPluginEditor`, so `src/ui` and `src/vulkan` need not know the plug-in SDK.
VST3 scanning can be slow, so it runs on a separate thread and reports with `WM_APP + 2` when done.
