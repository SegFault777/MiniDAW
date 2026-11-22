# MiniDAW architecture

The architecture notes live in [docs/dev/architecture.md](docs/dev/architecture.md). In short:

* `src/dsp`, `src/core`, `src/platform`, `src/ui` and `src/vulkan` use only the C++ standard library (no Qt). They are compiled and tested with plain `g++` by `tests/run.sh`.
* `core::Session` is the engine; user interfaces talk to it only through its public API and `Session::Hooks`.
* Three front ends share the engine: **Native** (`MiniDAW.exe`, software-rendered `src/ui` + `src/native/Win32App.cpp`), **Vulkan** (`MiniDAWVulkan.exe`, GPU-drawn `src/vulkan`) and the old **Qt** version (`MiniDAWQt.exe`, `src/*.cpp`, `src/Engine.*`, `src/vst3`, kept as a portable build only).
* Native and Vulkan can run in portable mode: a `MiniDAW.portable` marker next to the exe keeps all data in `<exe folder>\data`.

All documentation lives in [docs/](docs/README.md): `docs/user/` (users), `docs/dev/` (developers), `docs/project/` (project management).
