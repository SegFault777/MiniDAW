# Installation

## Installer (recommended, Native version)
1. Download `MiniDAW-<version>-setup.exe` from the release page and run it.
2. Choose how to install: for **your account only** (no administrator rights needed) or for **all users**.
3. Optional items
   - desktop shortcut
   - association of `.mdaw` project files (double-click to open)
   - a folder for your own plug-ins (`%LOCALAPPDATA%\MiniDAW\plugins`)
4. When the installer finishes, start **MiniDAW** from the Start menu.

If an older Qt version is installed, the same installer **upgrades it in place** and removes the Qt files that are no longer needed.

## The Vulkan version has an installer too
`MiniDAW-Vulkan-<version>-setup.exe` installs the Vulkan version (`MiniDAWVulkan.exe`) as **MiniDAW Vulkan**, in its own folder and with its own Start menu entry, so it can sit next to the
Native version. Associating `.mdaw` files with it is off by default because the Native version may already use them. Both versions open the same projects.

## Portable editions (no installation)
| Download | Contents |
| --- | --- |
| `MiniDAW-windows-x64-portable.zip` | Native version: `MiniDAW.exe` |
| `MiniDAW-Vulkan-windows-x64-portable.zip` | Vulkan version: `MiniDAWVulkan.exe` and its `shaders` folder |
| `MiniDAW-Qt-windows-x64-portable.zip` | Qt version: `MiniDAWQt.exe` |

Unzip into any folder (a USB stick works too) and run the program. In the Native and Vulkan editions the file **`MiniDAW.portable`** sits next to the program: it switches on
**portable mode**. Settings, autosaves and your own plug-ins are then kept in the **`data`** folder beside the program, so nothing is written to the computer and you can
carry everything with you. Delete `MiniDAW.portable` to use `%LOCALAPPDATA%\MiniDAW` again. The status bar of the Vulkan version shows "Portable" when the mode is on.

## System requirements
- Windows 10 or 11 (64-bit)
- An audio device for playback (the built-in sound device is fine); a microphone or audio interface for recording
- Vulkan version only: a graphics card or integrated GPU with a Vulkan 1.0 driver (all graphics drivers since about 2017 include one). If Vulkan is not available the program says so and offers to start the Native version.

## Uninstall
Settings > Apps > MiniDAW > Uninstall. Your settings, autosaves and your own plug-ins stay in `%LOCALAPPDATA%\MiniDAW`; delete that folder to remove them completely.
