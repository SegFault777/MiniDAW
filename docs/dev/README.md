# Developer documentation

| Document | Contents |
| --- | --- |
| [architecture.md](architecture.md) | Overall structure, layer rules, threads, directory map |
| [building.md](building.md) | Building on Windows, the targets of `build.sh`, portable packages, CI |
| [testing.md](testing.md) | Test suites, how to run them, screenshot tests |
| [ui-toolkit.md](ui-toolkit.md) | The two Qt-free UI toolkits: software `src/ui` (Native) and GPU `src/vulkan` (Vulkan) |
| [plugin-hosting.md](plugin-hosting.md) | MiniDAW plug-ins (SDK) and VST3 hosting |
| [adding-features.md](adding-features.md) | The order of work for a new edit function, dialog or translation |
| [coding-rules.md](coding-rules.md) | Coding rules, thread rules, frequent MSVC errors |

To start: `git clone`, then "Tests only on Linux" in [building.md](building.md), then `bash tests/run.sh`.
