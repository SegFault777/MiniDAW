// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace dsp {

/// A parameter that the UI thread writes and the audio thread reads without locks.
struct Param {
    std::string name;
    float min, max, def;
    bool stepped = false;               ///< Integer valued (for example a waveform choice).
    bool log = false;                   ///< Knobs use a logarithmic response.
    std::string unit;                   ///< Display suffix such as "Hz" or "dB".
    std::vector<std::string> choices;   ///< Labels for stepped parameters.
    std::atomic<float> value;

    Param(std::string n, float lo, float hi, float d, std::string u = {})
        : name(std::move(n)), min(lo), max(hi), def(d), unit(std::move(u)), value(d) {}

    float get() const { return value.load(std::memory_order_relaxed); }
    void set(float v) {
        if (stepped) v = std::round(v);
        value.store(std::clamp(v, min, max), std::memory_order_relaxed);
    }
};

/// Base class for anything that exposes a list of parameters (effects, synth).
class ParamSet {
public:
    virtual ~ParamSet() = default;
    std::vector<std::unique_ptr<Param>> params;

    Param& add(std::string name, float lo, float hi, float def, std::string unit = {}) {
        params.push_back(std::make_unique<Param>(std::move(name), lo, hi, def, std::move(unit)));
        return *params.back();
    }
    Param* find(const std::string& name) const {
        for (const auto& p : params)
            if (p->name == name) return p.get();
        return nullptr;
    }
    void resetToDefaults() {
        for (auto& p : params) p->set(p->def);
    }
};

}  // namespace dsp
