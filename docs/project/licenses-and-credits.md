# Licences and credits

## MiniDAW
**GPL-3.0-or-later.** The full text is in `LICENSE` in the repository root.

## What is bundled
| Component | Licence | Where it is used |
| --- | --- | --- |
| **Liberation Sans** (font) | SIL Open Font License 1.1 | Included as a glyph table in `src/ui/FontData.cpp` (the built-in font for screenshots and tests, also used by the Vulkan UI tests). Windows apps use the system font (Segoe UI) |
| **Steinberg VST3 SDK** | GPL-3.0 / commercial dual licence | Linked statically only in builds with VST3 support (not in the repository) |
| **Vulkan headers** (Khronos) | Apache-2.0 / MIT | Needed only to build the Vulkan interface; `vulkan-1.dll` is loaded at run time and nothing of Khronos is shipped |
| **glslang / glslc** | BSD-3-Clause / Apache-2.0 | Build tools that compile the shaders to SPIR-V; not shipped (only the compiled `.spv` files are) |
| Plug-in SDK header (`plugin-sdk/mdplugin.h`) | GPL-3.0-or-later, same as the project (examples included) | For plug-in development |
| Example loops (`loops/`) | Example sounds included in the project (see `loops/` in the repository) | The default loops of the browser |
| Windows API, Media Foundation, WASAPI | Operating system | Audio, MIDI, decoding |

## Trademarks
- Logic Pro and macOS are trademarks of Apple Inc.
- Ableton Live is a trademark of Ableton AG.
- FL Studio is a trademark of Image-Line.
- Cubase and VST are trademarks of Steinberg Media Technologies GmbH.
- Pro Tools is a trademark of Avid Technology, Inc.
- Windows is a trademark of Microsoft Corporation.
- Vulkan is a registered trademark of the Khronos Group Inc.

MiniDAW is not affiliated with or endorsed by these companies. The design comparison ([ui-design.md](ui-design.md)) is a general assessment based on published usage experience; no code or artwork of those products was used.

## Contributing
Contributors agree that their contribution is distributed under GPL-3.0-or-later. Please read `docs/dev/coding-rules.md` and `docs/dev/adding-features.md` before contributing.
