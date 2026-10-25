// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Synth.h"

#include <algorithm>
#include <cmath>
#include "Effects.h"  // kSampleRate

namespace dsp {
namespace {

constexpr double kTwoPi = 6.283185307179586;

/// PolyBLEP residual used to remove aliasing from saw and square discontinuities.
inline double polyBlep(double t, double dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
    return 0.0;
}

/// Waveforms: 0 sine, 1 saw, 2 square, 3 triangle.
inline double oscillator(int wave, double ph, double dt) {
    switch (wave) {
    case 0: return std::sin(kTwoPi * ph);
    case 1: return 2.0 * ph - 1.0 - polyBlep(ph, dt);
    case 2: {
        double v = ph < 0.5 ? 1.0 : -1.0;
        v += polyBlep(ph, dt);
        v -= polyBlep(std::fmod(ph + 0.5, 1.0), dt);
        return v;
    }
    default: return 2.0 * std::fabs(2.0 * ph - 1.0) - 1.0;
    }
}

// Parameter order: wave1 wave2 mix detune sub cutoff res envAmt fltDecay A D S R volume
constexpr int kParamCount = 14;
struct Preset { const char* name; float v[kParamCount]; };
const Preset kPresets[] = {
    {"Classic Saw", {1, 1, 0.5f, 8, 0.0f, 5000, 0.20f, 0.30f, 0.30f, 0.010f, 0.30f, 0.70f, 0.40f, 0.70f}},
    {"Warm Pad",    {1, 3, 0.5f, 12, 0.2f, 1800, 0.15f, 0.15f, 0.80f, 0.600f, 1.00f, 0.80f, 1.50f, 0.60f}},
    {"Pluck",       {1, 2, 0.3f, 5, 0.0f, 1200, 0.30f, 0.80f, 0.25f, 0.002f, 0.25f, 0.00f, 0.25f, 0.75f}},
    {"Sub Bass",    {0, 1, 0.2f, 0, 0.8f, 600, 0.25f, 0.40f, 0.20f, 0.005f, 0.20f, 0.80f, 0.15f, 0.80f}},
    {"Bright Lead", {2, 1, 0.5f, 10, 0.0f, 9000, 0.35f, 0.50f, 0.40f, 0.010f, 0.20f, 0.80f, 0.30f, 0.60f}},
    {"Soft Keys",   {3, 0, 0.4f, 4, 0.0f, 3500, 0.10f, 0.30f, 0.40f, 0.005f, 0.80f, 0.30f, 0.50f, 0.70f}},
};

}  // namespace

Synth::Synth() {
    pW1 = &add("Wave 1", 0, 3, 1);
    pW2 = &add("Wave 2", 0, 3, 1);
    for (Param* p : {pW1, pW2}) { p->stepped = true; p->choices = {"Sine", "Saw", "Square", "Tri"}; }
    pMix = &add("Osc Mix", 0, 1, 0.5f);
    pDet = &add("Detune", 0, 50, 8, "ct");
    pSub = &add("Sub", 0, 1, 0);
    pCut = &add("Cutoff", 40, 18000, 5000, "Hz");  pCut->log = true;
    pRes = &add("Resonance", 0, 0.95f, 0.2f);
    pEnv = &add("Flt Env", 0, 1, 0.3f);
    pFDec = &add("Flt Decay", 0.02f, 2, 0.3f, "s");  pFDec->log = true;
    pA = &add("Attack", 0.001f, 4, 0.01f, "s");   pA->log = true;
    pD = &add("Decay", 0.01f, 4, 0.3f, "s");      pD->log = true;
    pS = &add("Sustain", 0, 1, 0.7f);
    pR = &add("Release", 0.01f, 6, 0.4f, "s");    pR->log = true;
    pVol = &add("Volume", 0, 1, 0.7f);
}

int Synth::presetCount() { return int(sizeof(kPresets) / sizeof(kPresets[0])); }

const char* Synth::presetName(int i) { return kPresets[std::clamp(i, 0, presetCount() - 1)].name; }

void Synth::setPreset(int index) {
    const Preset& p = kPresets[std::clamp(index, 0, presetCount() - 1)];
    for (int i = 0; i < kParamCount; ++i) params[size_t(i)]->set(p.v[i]);
}

void Synth::reset() {
    for (Voice& v : m_voices) v = Voice{};
}

void Synth::noteOn(int note, int velocity) {
    Voice* slot = nullptr;
    for (Voice& v : m_voices)  // retrigger a voice already playing this note, otherwise use a free one
        if (v.active && v.note == note) { slot = &v; break; }
    if (!slot)
        for (Voice& v : m_voices)
            if (!v.active) { slot = &v; break; }
    if (!slot) {  // steal the oldest voice
        slot = &m_voices[0];
        for (Voice& v : m_voices)
            if (v.age < slot->age) slot = &v;
    }
    *slot = Voice{};
    slot->active = slot->gate = true;
    slot->note = note;
    slot->vel = std::clamp(velocity, 1, 127) / 127.f;
    slot->freq = 440.f * std::pow(2.f, float(note - 69) / 12.f);
    slot->fenv = 1.f;
    slot->age = ++m_age;
}

static void releaseVoice(float& env, int& stage, bool& gate, float& relStep, float seconds) {
    gate = false;
    stage = 3;
    relStep = std::max(env, 1e-4f) / (std::max(seconds, 0.002f) * kSampleRate);
}

void Synth::noteOff(int note) {
    for (Voice& v : m_voices)
        if (v.active && v.gate && v.note == note) releaseVoice(v.env, v.stage, v.gate, v.relStep, pR->get());
}

void Synth::allNotesOff() {
    for (Voice& v : m_voices)
        if (v.active) releaseVoice(v.env, v.stage, v.gate, v.relStep, 0.005f);
}

void Synth::render(float* io, int frames) {
    const float sr = kSampleRate;
    const int w1 = int(pW1->get()), w2 = int(pW2->get());
    const float mix = pMix->get(), sub = pSub->get(), cutoff = pCut->get(), res = pRes->get();
    const float envAmt = pEnv->get(), vol = pVol->get(), sus = pS->get();
    const float atkStep = 1.f / (pA->get() * sr), decStep = (1.f - sus) / (pD->get() * sr);
    const float fCoef = std::exp(-1.f / (pFDec->get() * sr));
    const double detune = std::pow(2.0, double(pDet->get()) / 1200.0);
    const float k = 2.f - 1.95f * res;  // SVF damping

    for (Voice& v : m_voices) {
        if (!v.active) continue;
        const double dt1 = v.freq / sr, dt2 = v.freq * detune / sr, dtSub = v.freq * 0.5 / sr;
        float a1 = 0, a2 = 0, a3 = 0;
        for (int i = 0; i < frames; ++i) {
            switch (v.stage) {
            case 0: v.env += atkStep; if (v.env >= 1.f) { v.env = 1.f; v.stage = 1; } break;
            case 1: v.env -= decStep; if (v.env <= sus) { v.env = sus; v.stage = 2; } break;
            case 2: v.env = sus; break;
            default:
                v.env -= v.relStep;
                if (v.env <= 0.f) { v.active = false; v.env = 0.f; }
                break;
            }
            if (!v.active) break;
            v.fenv *= fCoef;
            if ((i & 15) == 0) {  // refresh filter coefficients every 16 samples
                const float fc = std::clamp(cutoff * std::pow(2.f, envAmt * 5.f * v.fenv), 20.f, 0.45f * sr);
                const float g = std::tan(3.14159265f * fc / sr);
                a1 = 1.f / (1.f + g * (g + k));
                a2 = g * a1;
                a3 = g * a2;
            }
            double o = oscillator(w1, v.ph1, dt1) * (1.0 - mix) + oscillator(w2, v.ph2, dt2) * mix;
            o += sub * std::sin(kTwoPi * v.phSub);
            v.ph1 += dt1; if (v.ph1 >= 1.0) v.ph1 -= 1.0;
            v.ph2 += dt2; if (v.ph2 >= 1.0) v.ph2 -= 1.0;
            v.phSub += dtSub; if (v.phSub >= 1.0) v.phSub -= 1.0;

            const float v3 = float(o) - v.ic2;
            const float v1 = a1 * v.ic1 + a2 * v3;
            const float v2 = v.ic2 + a2 * v.ic1 + a3 * v3;
            v.ic1 = 2.f * v1 - v.ic1;
            v.ic2 = 2.f * v2 - v.ic2;

            const float out = v2 * v.env * v.vel * 0.25f * vol;
            io[2 * i] += out;
            io[2 * i + 1] += out;
        }
    }
}

}  // namespace dsp
