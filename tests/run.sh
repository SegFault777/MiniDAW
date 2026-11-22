#!/usr/bin/env bash
# Builds and runs the Qt-free tests: DSP (tempo map, time stretching, Drummer, MIDI files, drum kit, fades / WAV export,
# effects, native plug-in API), the whole Qt-free core (project model, save / load, autosave, export, bounce in place, MIDI
# operations, v0.12 features, portable paths), the platform layer, the Native user interface and the Vulkan user interface
# (rendered on the CPU by the twin of its shader, so no GPU is needed).
# Works with g++ or clang++ on Linux / macOS:  bash tests/run.sh
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
CXX="${CXX:-g++}"
D=../src/dsp
ENGINE="../src/core/*.cpp $D/TempoMap.cpp $D/Stretch.cpp $D/Synth.cpp $D/Effects.cpp $D/DrumKit.cpp $D/Drummer.cpp $D/MidiFile.cpp $D/AudioOps.cpp $D/TempoDetect.cpp $D/AudioAnalysis.cpp"
$CXX -std=c++20 -O2 -Wall -Wextra dsp_timing_tests.cpp $D/TempoMap.cpp $D/Stretch.cpp $D/Synth.cpp $D/Effects.cpp -o /tmp/minidaw_timing_tests
$CXX -std=c++20 -O2 -Wall -Wextra dsp_music_tests.cpp $D/Drummer.cpp $D/MidiFile.cpp $D/DrumKit.cpp -o /tmp/minidaw_music_tests
$CXX -std=c++20 -O2 -Wall -Wextra dsp_audioops_tests.cpp $D/AudioOps.cpp -o /tmp/minidaw_audioops_tests
$CXX -std=c++20 -O2 -Wall -Wextra dsp_effects_tests.cpp $D/Effects.cpp -o /tmp/minidaw_effects_tests
$CXX -std=c++17 -O2 -fPIC -shared ../plugin-sdk/examples/tremolo.cpp -o /tmp/md_tremolo.so
$CXX -std=c++17 -O2 -fPIC -shared ../plugin-sdk/examples/bitcrusher.cpp -o /tmp/md_bitcrusher.so
mkdir -p /tmp/mdplugins && cp /tmp/md_tremolo.so /tmp/md_bitcrusher.so /tmp/mdplugins/
$CXX -std=c++20 -O2 -Wall -Wextra native_plugin_tests.cpp $D/NativeEffect.cpp -ldl -o /tmp/minidaw_native_tests
$CXX -std=c++20 -O1 -Wall -Wextra core_tests.cpp $ENGINE -o /tmp/minidaw_core_tests
$CXX -std=c++20 -O1 -pthread -Wall -Wextra platform_tests.cpp ../src/platform/*.cpp $D/NativeEffect.cpp $ENGINE -ldl -o /tmp/minidaw_platform_tests
$CXX -std=c++20 -O1 -Wall -Wextra ui_tests.cpp ../src/ui/*.cpp $ENGINE -o /tmp/minidaw_ui_tests
$CXX -std=c++20 -O1 -g -fsanitize=address,undefined -Wall -Wextra vulkan_ttf_tests.cpp ../src/vulkan/TrueType.cpp -o /tmp/minidaw_ttf_tests
# the Vulkan user interface: everything except the two files that need Windows / the Vulkan loader
VK=()
for f in ../src/vulkan/*.cpp; do case "$f" in */VkMain.cpp|*/VkRenderer.cpp) ;; *) VK+=("$f") ;; esac; done
$CXX -std=c++20 -O1 -Wall -Wextra vulkan_tests.cpp "${VK[@]}" ../src/ui/FontData.cpp $ENGINE -o /tmp/minidaw_vulkan_tests
# the GPU backend only has to compile here: tests/mock/vulkan/vulkan.h stands in for the Khronos header (CI uses the real one)
$CXX -std=c++20 -Wall -Wextra -Wno-missing-field-initializers -Imock -fsyntax-only ../src/vulkan/VkRenderer.cpp
test -s ../src/vulkan/shaders/ui.vert && test -s ../src/vulkan/shaders/ui.frag

/tmp/minidaw_timing_tests
/tmp/minidaw_music_tests
/tmp/minidaw_audioops_tests
/tmp/minidaw_effects_tests
/tmp/minidaw_native_tests
/tmp/minidaw_core_tests
/tmp/minidaw_platform_tests
/tmp/minidaw_ui_tests
/tmp/minidaw_ttf_tests
/tmp/minidaw_vulkan_tests
