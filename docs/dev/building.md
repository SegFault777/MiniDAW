# Building

## Tests only, on Linux (no Qt, no Windows needed)
```
sudo apt-get install g++
bash tests/run.sh
```
`dsp`, `core`, `platform`, `ui` and the Vulkan UI are all compiled with `g++` and every test runs. `VkRenderer.cpp` is only syntax-checked against `tests/mock/vulkan/vulkan.h`. Screenshots of the screen tests are left in `/tmp/minidaw_ui*.ppm` and `/tmp/vk_*.ppm`
(convert to PNG with `python3 -c "from PIL import Image; Image.open('x.ppm').save('x.png')"`).

## Windows: all three executables
Required: MSVC (x64) from Visual Studio, Git Bash, Inno Setup 6 (only for the installer). Qt is needed only for the old Qt version (6.5+). The Vulkan build needs the Vulkan headers and a GLSL compiler.
Open Git Bash from the x64 Native Tools prompt and run:

```
bash build.sh all              # Qt + Native + Vulkan   (SKIP_VULKAN=1 leaves out Vulkan)
```

| Executable | Built by | Result |
| --- | --- | --- |
| Native | `bash build.sh native` | `build/MiniDAW.exe` |
| Vulkan | `bash build.sh vulkan` | `build/MiniDAWVulkan.exe` and `shaders/*.spv` next to it |
| Qt | `bash build.sh build` | `build/MiniDAWQt.exe` |

| Target | What it does | Result |
| --- | --- | --- |
| `native` | Compile and link the Native version | `build/MiniDAW.exe` |
| `native-deploy` | Collect translations, loops, SDK, example plug-ins and documents; make the installer payload, the portable folder and the zip | `dist-native/MiniDAW/` (installer payload), `dist-portable/MiniDAW/`, `MiniDAW-windows-x64-portable.zip` |
| `installer` | Inno Setup | `installer-out/MiniDAW-<version>-setup.exe` |
| `native-release` | The three above in order | |
| `shaders` | Compile `src/vulkan/shaders/ui.vert\|frag` to SPIR-V | `build/shaders/*.spv` |
| `vulkan`, `vulkan-deploy`, `vulkan-installer`, `vulkan-release` | Build; package (install folder, portable folder, zip); Inno Setup; all three | `build/MiniDAWVulkan.exe`, `dist-vulkan/MiniDAW/` (installer payload), `dist-vulkan-portable/MiniDAW/`, `MiniDAW-Vulkan-windows-x64-portable.zip`, `installer-out/MiniDAW-Vulkan-<version>-setup.exe` |
| `qt`, `build`, `deploy`, `qt-installer` | The old Qt version | `dist/MiniDAWQt/`, `MiniDAW-Qt-windows-x64-portable.zip` |
| `all` | `qt` + `native-release` + `vulkan-release` | |
| `clean` | Delete the products | |

Environment variables: `MINIDAW_VERSION` (default at the top of `build.sh`), `VST3_SDK_DIR` (VST3 support), `ISCC` (location of Inno Setup), `JOBS` (parallel compiles), `SKIP_VULKAN=1`,
`VULKAN_SDK` (LunarG SDK) or `VULKAN_HEADERS_DIR` (a Vulkan-Headers checkout), `GLSLC` / `GLSLANG_VALIDATOR` (shader compiler).

## Portable mode
If a file named `MiniDAW.portable` lies next to the executable, all data (settings, autosave, plug-in folder) is kept in `<exe folder>\data` instead of `%APPDATA%` / `%LOCALAPPDATA%`.
`native-deploy` and `vulkan-deploy` create that marker in the portable folders. Native and Vulkan share the marker but use different settings files (`native-settings.ini` / `vulkan-settings.ini`).

## VST3 support
Download the Steinberg VST3 SDK and point `VST3_SDK_DIR` at it. `native` then compiles the SDK sources with the static CRT (`-MT`) and `src/native/Vst3Native.cpp` with `-DMINIDAW_VST3`. Some SDK files change place between SDK versions (`threadchecker_win32.cpp`, `commonstringconvert.cpp` and the file that defines `IPlugFrame::iid`), so `build.sh` finds them by name or by content (`vst3_named_sources`, `vst3_plugframe_source`); when no SDK file defines `IPlugFrame::iid`, `Vst3Native.cpp` defines it (`-DMINIDAW_DEFINE_PLUGFRAME_IID`). **If that fails, a warning is printed and the build is repeated without VST3**, so an SDK problem never blocks the whole build. The last line of the build log says `VST3 hosting: enabled / disabled`.
```
git clone --depth 1 --recurse-submodules https://github.com/steinbergmedia/vst3sdk.git
export VST3_SDK_DIR=$PWD/vst3sdk
```

## Compiler options
Native is built with `-std:c++20 -MT -utf-8 -permissive- -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS`.
A file that includes `windows.h` must have `NOMINMAX` defined **before** it (members of `dsp/Param.h` are called `min` / `max`).

## CI (`.github/workflows/build.yml`)
| Job | Runner | What it does |
| --- | --- | --- |
| `tests` | ubuntu | `bash tests/run.sh` |
| `native` | windows | Download the VST3 SDK (continue on failure), install Inno Setup, `build.sh native-release`, upload the installer and the portable zip, release on a tag |
| `vulkan` | windows | Clone Vulkan-Headers, install a GLSL compiler (LunarG Vulkan SDK from Chocolatey, else glslang built from source), install Inno Setup, `build.sh vulkan-release`, upload the Vulkan installer and portable zip |
| `qt` | windows | Install Qt, `build.sh qt`, upload the Qt portable zip |

A release is made by pushing a `v<version>` tag (for example `v0.12`, `v0.10_1`).
