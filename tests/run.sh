#!/usr/bin/env bash
# Builds and runs the Qt-free DSP tests (tempo map, time stretching, Drummer, MIDI files, drum kit,
# fades / WAV export, effects, native plug-in API).
# Works with g++ or clang++ on Linux / macOS:  bash tests/run.sh
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
CXX="${CXX:-g++}"
D=../src/dsp
$CXX -std=c++20 -O2 -Wall -Wextra dsp_timing_tests.cpp $D/TempoMap.cpp $D/Stretch.cpp $D/Synth.cpp $D/Effects.cpp -o /tmp/minidaw_timing_tests
$CXX -std=c++20 -O2 -Wall -Wextra dsp_music_tests.cpp $D/Drummer.cpp $D/MidiFile.cpp $D/DrumKit.cpp -o /tmp/minidaw_music_tests
$CXX -std=c++20 -O2 -Wall -Wextra dsp_audioops_tests.cpp $D/AudioOps.cpp -o /tmp/minidaw_audioops_tests
$CXX -std=c++20 -O2 -Wall -Wextra dsp_effects_tests.cpp $D/Effects.cpp -o /tmp/minidaw_effects_tests
$CXX -std=c++17 -O2 -fPIC -shared ../plugin-sdk/examples/tremolo.cpp -o /tmp/md_tremolo.so
$CXX -std=c++17 -O2 -fPIC -shared ../plugin-sdk/examples/bitcrusher.cpp -o /tmp/md_bitcrusher.so
$CXX -std=c++20 -O2 -Wall -Wextra native_plugin_tests.cpp $D/NativeEffect.cpp -ldl -o /tmp/minidaw_native_tests
/tmp/minidaw_timing_tests
/tmp/minidaw_music_tests
/tmp/minidaw_audioops_tests
/tmp/minidaw_effects_tests
/tmp/minidaw_native_tests
