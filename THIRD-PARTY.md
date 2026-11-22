# Third-party notices

## LogicProFormatWriter (Logic Pro `ProjectData` format notes)
The Logic Pro project reader (`src/core/LogicArrangement.*`, `LogicProjectData.*`) is a C++ re-implementation based on the format
documentation and reference decoder of LogicProFormatWriter, https://github.com/jonkubis/LogicProFormatWriter
(`PROJECTDATA_FORMAT.md`, `logicx/session.py`). `tests/data/logicx_reference.tsv` holds numbers produced by that decoder on its test fixtures.

MIT License

Copyright (c) 2026 Jon Kubis

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH
THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Logic Pro is a trademark of Apple Inc.; this project is not affiliated with or endorsed by Apple.

## logicxkit (track names)
The reading of track names in `src/core/LogicArrangement.cpp` (`readLogicTrackNames`) follows the file-format findings documented in the
source of logicxkit, https://github.com/phierceweb/logicxkit (Apache-2.0, Copyright Mike Farr): the `karT` arrange list and the `ivnE`
Environment objects. Only the byte offsets and the rules for choosing the list were taken, re-implemented in C++. No logicxkit code is
included. `tests/data/logicx_tracknames.tsv` holds names read by logicxkit from the LogicProFormatWriter test fixtures.

## Material Symbols (built-in icons of the Vulkan version)
`src/vulkan/IconFontData.cpp` contains icons of the Material Symbols fonts (Outlined, Rounded, Sharp), Copyright Google LLC, licensed under the
Apache License, Version 2.0 (https://www.apache.org/licenses/LICENSE-2.0; the licence text is `licenses/MaterialSymbols-LICENSE.txt` and is installed with
the program), https://github.com/google/material-design-icons. **Modified**: the data is a subset of the original variable fonts (only the glyphs MiniDAW
draws), fixed to their default instance (FILL 0, GRAD 0, opsz 24, wght 400), made by `tools/subset-material-symbols.py`. The program reads font files of the
same family from a `fonts` folder when they are there. The TrueType reader in `src/vulkan/TrueType.cpp` is MiniDAW's own code.
