// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include "Param.h"

namespace dsp {

constexpr float kSampleRate = 44100.f;

/// Base class of every insert / aux / master effect. Audio is interleaved stereo float.
class Effect : public ParamSet {
public:
    virtual const char* id() const = 0;
    virtual const char* displayName() const = 0;
    virtual void process(float* io, int frames) = 0;  ///< In-place processing.
    virtual void reset() {}                           ///< Clears delay lines and filter state.
    std::atomic<bool> bypass{false};
};

/// Creates an effect by id: "eq", "comp", "echo", "reverb", "drive", "chorus", "filter", "limiter".
std::shared_ptr<Effect> createEffect(const std::string& id);

/// Ids of the effects that can be inserted on a channel strip.
const std::vector<std::string>& insertEffectIds();

}  // namespace dsp
