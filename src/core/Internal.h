// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Helpers shared by the Session sources.
#include <algorithm>
#include <cmath>
#include "../dsp/Stretch.h"
#include "AudioIO.h"
#include "Model.h"

namespace core {
namespace detail {

/// Automation value (linear interpolation between breakpoints); 'fallback' when the lane is empty.
inline float autoValue(const std::vector<AutoPoint>& p, double beat, float fallback) {
    if (p.empty()) return fallback;
    if (beat <= p.front().beat) return p.front().value;
    if (beat >= p.back().beat) return p.back().value;
    for (size_t i = 1; i < p.size(); ++i) {
        if (beat <= p[i].beat) {
            const float f = float((beat - p[i - 1].beat) / std::max(p[i].beat - p[i - 1].beat, 1e-9));
            return p[i - 1].value + (p[i].value - p[i - 1].value) * f;
        }
    }
    return p.back().value;
}

/// Copies every parameter value that both sets share (by name).
inline void copyParams(const dsp::ParamSet& from, dsp::ParamSet& to) {
    for (const auto& p : from.params)
        if (dsp::Param* q = to.find(p->name)) q->set(p->get());
}

/// Rebuilds the playback buffer of an audio region from its source (time-stretch and transpose).
inline void applyFlex(Region& r, double projectBpm) {
    if (!r.source) return;
    if (!r.flexed()) {
        r.audio = r.source;
        r.offset = r.srcOffset;
        r.frames = r.srcFrames;
        r.flexBpm = 0;
        return;
    }
    const double ratio = (r.follow && r.sourceBpm > 0 && projectBpm > 0) ? r.sourceBpm / projectBpm : 1.0;
    auto d = std::make_shared<AudioData>();
    d->name = r.source->name;
    d->path = r.source->path;
    d->pcm = dsp::flex(r.source->pcm.data() + r.srcOffset * 2, size_t(r.srcFrames), ratio, r.semitones);
    computePeaks(*d);
    r.audio = d;
    r.offset = 0;
    r.frames = d->frames();
    r.flexBpm = projectBpm;
}



}  // namespace detail
using namespace detail;
}  // namespace core
