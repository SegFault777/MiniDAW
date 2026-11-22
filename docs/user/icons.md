# Icon style (Vulkan version)

**File > Preferences > Icon style** chooses how the toolbar and panel icons look. The change applies at once and is remembered.

| Style | Look |
| --- | --- |
| Material Symbols Outlined | Google's Material Symbols, outlined strokes (the default) |
| Material Symbols Rounded | the same icons with rounded corners |
| Material Symbols Sharp | the same icons with sharp corners |
| Classic | the icons MiniDAW has always drawn itself |

## The icons are part of the program
The icons that MiniDAW uses are built into `MiniDAWVulkan.exe` for all three Material Symbols styles (about 25 KB in total), so nothing has to be
installed or downloaded. They are the default design of the variable fonts of Google (weight 400, optical size 24); the transport buttons (play, stop,
record, back to start) use the filled variant of their icon.

## Using the complete fonts instead (optional)
Put Google's font files into a `fonts` folder next to `MiniDAWVulkan.exe`, or into the `fonts` folder of the data folder
(`%LOCALAPPDATA%\MiniDAW\fonts`, or `data\fonts` in a portable copy). A file whose name starts with `MaterialSymbolsOutlined`, `MaterialSymbolsRounded` or
`MaterialSymbolsSharp` and ends with `.ttf` (for example `MaterialSymbolsOutlined-VariableFont_FILL_GRAD_opsz_wght.ttf`, as downloaded) replaces the
built-in icons of that style. Icons are found by their names in the font; a `.codepoints` file next to the font is used when there is one.
The variable font is drawn in its default design (not filled, weight 400).

The icon style also covers the small marks of the controls: the arrow of drop-down lists, the check mark of check boxes, the arrow of sub-menus,
the fold marks of the side panel sections and the close button of effects and device cards.

If an icon is missing, that one button is drawn the classic way, so no button ever disappears.

The Native version (`MiniDAW.exe`) keeps its own icons.

## For developers
`tools/subset-material-symbols.py <folder with the three fonts>` regenerates `src/vulkan/IconFontData.cpp` (the built-in icons) from the full fonts; run it
after adding an icon name to `IconTheme::candidates()` in `src/vulkan/IconFont.cpp`. It needs `fontTools` and is not used by the build. The TrueType reader
(`TrueType.*`) draws the outlines of the font as they are, without hinting. Licence: Apache 2.0, see `THIRD-PARTY.md` and `licenses/`.
