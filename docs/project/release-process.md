# Release process

## 1. Preparation
- [ ] Everything planned for this version is in (the "unreleased" entries of `CHANGELOG.md` are tidy)
- [ ] `bash tests/run.sh` passes
- [ ] The latest CI run on `main` is green for all four jobs (`tests`, `native`, `vulkan`, `qt`)
- [ ] New English text is in the four `translations/*.tsv`
- [ ] The documents (`docs/user`, `docs/dev`) match the changed behaviour
- [ ] If the project file format changed in a way that older programs would misread, `kProjectVersion` (`src/core/ProjectIO.cpp`) is raised (see [architecture](../dev/architecture.md))
- [ ] The Vulkan interface has been started on a real GPU at least once for this release, and its screens were compared with the screenshots of `tests/vulkan_tests`

## 2. Raise the version
Change it everywhere at once (the list is in `docs/dev/coding-rules.md`):
`src/main.cpp`, `src/MainWindow.cpp`, `src/native/Win32App.cpp`, `src/vulkan/VkMain.cpp`, `build.sh`, `.github/workflows/build.yml`, `installer/MiniDAW.iss`, `installer/MiniDAW-Qt.iss`, `README.md`.
Then add a `## [version] - date` entry to `CHANGELOG.md`.

## 3. Tag
```
git commit -am "Release 0.12"
git tag v0.12
git push && git push origin v0.12
```
A hotfix gets an underscore number like `v0.12_1`. When the tag is pushed, CI builds the installer and the portable zips (Native, Vulkan, Qt) and attaches them to the release.

## 4. Checks after the release
- [ ] Install the installer on a **clean PC (or VM)** and run it
- [ ] Install over the previous version (upgrade) and check that settings and projects survive
- [ ] Unzip the portable zips (Native and Vulkan) to a new folder: they must start, and must write only inside `data\` next to the exe
- [ ] Save a project and open it again, record, export to WAV
- [ ] Load a plug-in (the `VST3 hosting:` line of the build log tells whether VST3 was enabled)
- [ ] After uninstalling, check that user data (`%LOCALAPPDATA%\MiniDAW`) remains

## 5. Record known problems
Write the known problems of this version in the release notes (for example "OGG files may not open with the default Windows codec").
