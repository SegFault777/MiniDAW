// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Param.h"

namespace dsp {

/// Anything that turns note events into audio: the built-in synth, the drum kit and VST3 instruments.
class Instrument : public ParamSet {
public:
    virtual void noteOn(int note, int velocity) = 0;
    virtual void noteOff(int note) = 0;
    virtual void allNotesOff() = 0;                    ///< Quick release of every voice (seek / loop).
    virtual void reset() = 0;                          ///< Silence immediately.
    virtual void render(float* io, int frames) = 0;    ///< Adds interleaved stereo audio to io.
    virtual const char* kind() const = 0;              ///< "synth", "drumkit" or "vst3:<path>|<class id>".
    virtual int presetCount() const { return 0; }
    virtual const char* presetName(int) const { return ""; }
    virtual void setPreset(int) {}
};

}  // namespace dsp
