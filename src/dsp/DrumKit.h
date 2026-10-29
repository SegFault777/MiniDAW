// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <vector>
#include "Instrument.h"

namespace dsp {

enum DrumPiece { Kick, Snare, HatClosed, HatOpen, Clap, Tom, Crash, Rim, kDrumPieceCount };

/// Synthesized drum kit played with General MIDI drum note numbers (36 kick, 38 snare, 42 hat ...).
class DrumKit : public Instrument {
public:
    DrumKit();
    void noteOn(int note, int velocity) override;
    void noteOff(int) override {}   // drum sounds are one-shots
    void allNotesOff() override {}
    void reset() override;
    void render(float* io, int frames) override;
    const char* kind() const override { return "drumkit"; }

    static const std::vector<float>& sound(int piece);   ///< Mono samples of one drum piece (44.1 kHz).
    static int pieceForNote(int note);

private:
    struct Voice { const std::vector<float>* snd = nullptr; size_t pos = 0; float gain = 0; int piece = 0; };
    std::array<Voice, 24> m_voices;
    Param *pVol, *pKick, *pSnare, *pHats, *pPerc;
};

}  // namespace dsp
