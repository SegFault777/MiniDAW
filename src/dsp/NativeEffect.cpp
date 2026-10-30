// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeEffect.h"

#include <cmath>
#include <limits>

namespace dsp {

bool validDescriptor(const MdEffectDesc* d) {
    if (!d || d->apiVersion != MDPLUGIN_API_VERSION) return false;
    if (!d->id || !d->id[0] || !d->name || !d->name[0]) return false;
    if (d->paramCount < 0 || d->paramCount > MDPLUGIN_MAX_PARAMS || (d->paramCount > 0 && !d->params)) return false;
    if (!d->create || !d->destroy || !d->process) return false;
    for (int i = 0; i < d->paramCount; ++i) {
        const MdParam& p = d->params[i];
        if (!p.name || !(p.max > p.min)) return false;
    }
    return true;
}

std::shared_ptr<NativeEffect> NativeEffect::create(const MdEffectDesc* desc) {
    if (!validDescriptor(desc)) return nullptr;
    void* inst = desc->create(int(kSampleRate));
    if (!inst) return nullptr;
    return std::shared_ptr<NativeEffect>(new NativeEffect(desc, inst));
}

NativeEffect::NativeEffect(const MdEffectDesc* d, void* instance) : m_d(d), m_inst(instance) {
    m_id = std::string("native:") + d->id;
    m_name = d->name;
    for (int i = 0; i < d->paramCount; ++i) {
        const MdParam& p = d->params[i];
        add(p.name, p.min, p.max, std::min(std::max(p.def, p.min), p.max), p.unit ? p.unit : "");
    }
    m_sent.assign(size_t(d->paramCount), std::numeric_limits<float>::quiet_NaN());   // forces the first transfer
}

NativeEffect::~NativeEffect() {
    if (m_inst) m_d->destroy(m_inst);
}

void NativeEffect::reset() {
    if (m_d->reset) m_d->reset(m_inst);
}

void NativeEffect::process(float* io, int frames) {
    if (m_d->setParam) {
        for (size_t i = 0; i < m_sent.size(); ++i) {
            const float v = params[i]->get();
            if (!(v == m_sent[i])) {   // also true for the initial NaN
                m_d->setParam(m_inst, int(i), v);
                m_sent[i] = v;
            }
        }
    }
    m_d->process(m_inst, io, frames);
}

}  // namespace dsp
