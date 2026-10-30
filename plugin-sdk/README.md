# MiniDAW plug-in SDK (API version 1)

MiniDAW can load its own simple audio-effect plug-ins. A plug-in is one shared library (`.dll` on Windows,
`.so` on Linux, `.dylib` on macOS) that exports two C functions and describes its effects with a small
table. There is no framework to link: you only need the single header [`mdplugin.h`](mdplugin.h).

```c
int                 mdplugin_count(void);
const MdEffectDesc* mdplugin_get(int index);
```

## Writing a plug-in

1. Copy `mdplugin.h` into your project and describe each effect with an `MdEffectDesc`: a unique `id`
   (saved in project files, so never change it), a display `name`, up to 32 parameters (`MdParam`: name,
   unit, min, max, default) and five callbacks: `create`, `destroy`, `reset`, `setParam`, `process`.
2. `process()` receives interleaved stereo `float` frames (L R L R …) and changes them in place. It runs on
   the audio thread: no allocation, no locks, no file or network access.
3. MiniDAW calls `setParam()` only when a value changed; the host already clamps values to `min` / `max`.
4. Export `mdplugin_count` and `mdplugin_get` with `MDPLUGIN_EXPORT` (see the examples) and keep the
   descriptors in static storage.

`examples/tremolo.cpp` and `examples/bitcrusher.cpp` are complete plug-ins in about 60 lines each.

## Building

| Platform | Command |
| --- | --- |
| Windows (MSVC) | `cl /nologo /O2 /EHsc /LD tremolo.cpp /Fe:tremolo.dll` |
| Linux | `g++ -std=c++17 -O2 -fPIC -shared tremolo.cpp -o tremolo.so` |
| macOS | `clang++ -std=c++17 -O2 -shared tremolo.cpp -o tremolo.dylib` |

Build the plug-in for the same architecture as MiniDAW (64-bit). Use the dynamic runtime (`/MD`) on Windows.

## Installing

Copy the library into the `plugins` folder next to `MiniDAW.exe` (the installer already ships the two
examples there) or into `%LOCALAPPDATA%/MiniDAW/plugins`, restart MiniDAW and pick the effect in the
**MiniDAW Plug-ins** submenu of any insert slot. Projects remember the plug-in by its `id`; if
a plug-in is missing when a project is opened, MiniDAW shows a message and skips that effect.

MiniDAW loads every library in the plugin folders with the privileges of the program, like any other
plug-in format, so only install plug-ins you trust.

## License

`mdplugin.h` is part of MiniDAW (GPL-3.0-or-later). A plug-in that only uses this interface is a separate
work; you may license it as you like.
