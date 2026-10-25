// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include "Param.h"

namespace dsp {

/// 16-voice subtractive synthesizer: two band-limited oscillators, a sub oscillator,
/// a resonant state-variable low-pass filter with its own decay envelope and an ADSR amp envelope.
class Synth : public ParamSet {
public:
    Synth();

    void noteOn(int note, int velocity);
    void noteOff(int note);
    void allNotesOff();                    ///< Releases every voice quickly (used on seek / loop).
    void reset();                          ///< Silences every voice immediately.
    void render(float* io, int frames);    ///< Adds the output into an interleaved stereo buffer.

    void setPreset(int index);
    static int presetCount();
    static const char* presetName(int index);

private:
    struct Voice {
        bool active = false, gate = false;
        int note = 0, stage = 0;           // stage: 0 attack, 1 decay, 2 sustain, 3 release
        float vel = 0, freq = 0, env = 0, relStep = 0, fenv = 0, ic1 = 0, ic2 = 0;
        double ph1 = 0, ph2 = 0, phSub = 0;
        std::uint64_t age = 0;
    };
    std::array<Voice, 16> m_voices;
    std::uint64_t m_age = 0;
    Param *pW1, *pW2, *pMix, *pDet, *pSub, *pCut, *pRes, *pEnv, *pFDec, *pA, *pD, *pS, *pR, *pVol;
};

}  // namespace dsp
