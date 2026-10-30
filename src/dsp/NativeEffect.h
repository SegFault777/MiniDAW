// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "Effects.h"
#include "../../plugin-sdk/mdplugin.h"

namespace dsp {

/// True when a plug-in descriptor follows the rules of mdplugin.h.
bool validDescriptor(const MdEffectDesc* d);

/// An effect backed by a MiniDAW plug-in (see mdplugin.h). Its id is "native:<plug-in id>".
class NativeEffect : public Effect {
public:
    /// Creates an instance, or returns nullptr when the descriptor is invalid or create() fails.
    static std::shared_ptr<NativeEffect> create(const MdEffectDesc* desc);
    ~NativeEffect() override;

    const char* id() const override { return m_id.c_str(); }
    const char* displayName() const override { return m_name.c_str(); }
    void process(float* io, int frames) override;
    void reset() override;

private:
    NativeEffect(const MdEffectDesc* d, void* instance);
    const MdEffectDesc* m_d;
    void* m_inst;
    std::string m_id, m_name;
    std::vector<float> m_sent;   // last value handed to the plug-in for every parameter
};

}  // namespace dsp
