# MiniDAW architecture

The architecture notes moved to [docs/dev/architecture.md](docs/dev/architecture.md) (Korean). In short:

* `src/dsp`, `src/core`, `src/platform`, `src/ui` use only the C++ standard library (no Qt); they are compiled
  and tested with plain `g++` by `tests/run.sh`.
* `core::Session` is the engine; the user interface talks to it only through its public API and `Session::Hooks`.
* `src/ui` is a software-rendered, Qt-free UI toolkit; `src/native/Win32App.cpp` is the only Windows shell.
* The old Qt version (`src/*.cpp`, `src/Engine.*`, `src/vst3`) is kept as a portable build only.

All documentation lives in [docs/](docs/README.md): `docs/user/` (users), `docs/dev/` (developers),
`docs/project/` (project management).
