#!/usr/bin/env bash
# Builds and runs the Qt-free DSP tests (tempo map, time stretching, Drummer, MIDI files, drum kit, fades / WAV export).
# Works with g++ or clang++ on any platform:  bash tests/run.sh
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
CXX="${CXX:-g++}"
D=../src/dsp
$CXX -std=c++20 -O2 -Wall -Wextra dsp_timing_tests.cpp $D/TempoMap.cpp $D/Stretch.cpp $D/Synth.cpp $D/Effects.cpp -o /tmp/minidaw_timing_tests
$CXX -std=c++20 -O2 -Wall -Wextra dsp_music_tests.cpp $D/Drummer.cpp $D/MidiFile.cpp $D/DrumKit.cpp -o /tmp/minidaw_music_tests
$CXX -std=c++20 -O2 -Wall -Wextra dsp_audioops_tests.cpp $D/AudioOps.cpp -o /tmp/minidaw_audioops_tests
/tmp/minidaw_timing_tests
/tmp/minidaw_music_tests
/tmp/minidaw_audioops_tests
