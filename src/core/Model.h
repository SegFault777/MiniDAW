// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The Qt-free project model: tracks, regions, notes, automation and markers.
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include "../dsp/AudioOps.h"
#include "../dsp/DrumKit.h"
#include "../dsp/Drummer.h"
#include "../dsp/Effects.h"
#include "../dsp/Instrument.h"
#include "Base.h"

namespace core {

using DrumPattern = std::array<std::array<bool, kSteps>, kDrumRows>;

enum class TrackType { Audio, Instrument, Drums, Drummer };

struct Color {
    std::uint8_t r = 0, g = 0, b = 0;
};

/// Decoded audio shared between regions (split / duplicated regions reference the same data).
struct AudioData {
    std::string name;
    mutable std::string path;    // source file; empty for fresh recordings until the project is saved
    std::vector<float> pcm;      // interleaved stereo, 44.1 kHz
    std::vector<float> peaks;    // max |sample| per kPeakStride frames
    i64 frames() const { return i64(pcm.size() / 2); }
};

/// One MIDI note. Times are in beats relative to the start of the owning region.
struct Note {
    int pitch = 60;
    double start = 0, length = 0.25;
    int velocity = 100;
};

/// A region on a track. Positions are in quarter-note beats so music follows tempo changes.
struct Region {
    std::string name;
    bool midi = false;
    double start = 0;                        // timeline position in beats
    double lengthBeats = 4;                  // MIDI regions only
    std::vector<Note> notes;
    bool drummer = false;                    // notes are generated from 'drum'
    dsp::DrummerParams drum;

    // Audio regions. 'audio' is what is played: either 'source' itself or a time-stretched copy of it.
    std::shared_ptr<const AudioData> audio;
    std::shared_ptr<const AudioData> source;
    std::string pendingPath;                 // audio still being decoded after a project load
    i64 offset = 0, frames = 0;              // used part of 'audio'
    i64 srcOffset = 0, srcFrames = 0;        // used part of 'source'
    bool follow = false;                     // stretch to the project tempo
    double sourceBpm = 0;                    // tempo of the recording (needed for 'follow')
    double semitones = 0;                    // pitch transposition
    bool mute = false;                       // muted regions stay on the timeline but make no sound (v0.6)
    float gainDb = 0;                        // region gain (v0.5)
    i64 fadeIn = 0, fadeOut = 0;             // fade lengths in frames of the played audio (v0.5)
    double flexBpm = 0;                      // project tempo that 'audio' was stretched for
    bool flexed() const { return follow || std::abs(semitones) > 0.01; }
};

struct AutoPoint {
    double beat = 0;
    float value = 0;   // value of the automated parameter (see autoRange)
};

/// Parameters of a track that can be automated.
enum class AutoParam { Volume, Pan, SendA, SendB };

/// Value range of an automation lane.
inline std::pair<float, float> autoRange(AutoParam p) {
    switch (p) {
    case AutoParam::Volume: return {0.f, 1.5f};
    case AutoParam::Pan: return {-1.f, 1.f};
    default: return {0.f, 1.f};
    }
}

struct Track {
    int id = 0;
    std::string name;
    Color color;
    TrackType type = TrackType::Audio;
    float volume = 0.8f, pan = 0.f, sendA = 0.f, sendB = 0.f;
    bool mute = false, solo = false, armed = false;
    std::vector<Region> regions;
    DrumPattern steps{};
    std::vector<std::shared_ptr<dsp::Effect>> inserts;
    std::shared_ptr<dsp::Instrument> instrument;   // Instrument and Drummer tracks
    std::string instrumentId;                      // "synth", "drumkit" or "vst3:..."
    int preset = 0;
    std::vector<AutoPoint> volAuto, panAuto, sendAAuto, sendBAuto;
    float peakL = 0, peakR = 0;

    std::vector<AutoPoint>& autoOf(AutoParam p) {
        return p == AutoParam::Volume ? volAuto : p == AutoParam::Pan ? panAuto : p == AutoParam::SendA ? sendAAuto : sendBAuto;
    }
    const std::vector<AutoPoint>& autoOf(AutoParam p) const {
        return p == AutoParam::Volume ? volAuto : p == AutoParam::Pan ? panAuto : p == AutoParam::SendA ? sendAAuto : sendBAuto;
    }
    /// Value of the parameter when it has no automation.
    float baseValue(AutoParam p) const { return p == AutoParam::Volume ? volume : p == AutoParam::Pan ? pan : p == AutoParam::SendA ? sendA : sendB; }
};

/// A named position on the timeline (v0.6).
struct Marker {
    double beat = 0;
    std::string name;
};

/// Options of "Bounce to WAV" (v0.5).
struct ExportOptions {
    bool cycleOnly = false;                              ///< Bounce only the cycle range instead of the whole project.
    dsp::WavFormat format = dsp::WavFormat::Pcm16;       ///< 16-bit, 24-bit or 32-bit float.
    bool normalize = false;                              ///< Scale the result so that its peak reaches 'normalizeDb'.
    float normalizeDb = -1.f;
    bool dither = false;                                 ///< TPDF dither (16-bit only).
    bool tail = true;                                    ///< Add 2 s for reverb and echo tails.
};

}  // namespace core
