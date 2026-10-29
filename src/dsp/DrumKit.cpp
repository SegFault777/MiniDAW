// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "DrumKit.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace dsp {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kSr = 44100.0;

struct Kit {
    std::array<std::vector<float>, kDrumPieceCount> s;
    Kit() {
        std::mt19937 rng(1234);
        std::uniform_real_distribution<float> nd(-1.f, 1.f);
        auto make = [&](int piece, double sec) { s[size_t(piece)].resize(size_t(sec * kSr)); return &s[size_t(piece)]; };
        {   // kick: pitch-swept sine
            auto* v = make(Kick, 0.40);
            double ph = 0;
            for (size_t i = 0; i < v->size(); ++i) {
                const double t = double(i) / kSr;
                ph += 2 * kPi * (42 + 120 * std::exp(-t * 30)) / kSr;
                (*v)[i] = float(std::sin(ph) * std::exp(-t * 7.5) * 0.95);
            }
        }
        {   // snare: noise plus a short tone
            auto* v = make(Snare, 0.25);
            for (size_t i = 0; i < v->size(); ++i) {
                const double t = double(i) / kSr;
                (*v)[i] = float(nd(rng) * std::exp(-t * 20) * 0.6 + std::sin(2 * kPi * 185 * t) * std::exp(-t * 30) * 0.5);
            }
        }
        for (int open = 0; open < 2; ++open) {   // hats: differentiated (high-passed) noise
            auto* v = make(open ? HatOpen : HatClosed, open ? 0.35 : 0.07);
            float prev = 0;
            for (size_t i = 0; i < v->size(); ++i) {
                const double t = double(i) / kSr;
                const float n = nd(rng);
                (*v)[i] = (n - prev) * 0.5f * float(std::exp(-t * (open ? 9 : 70)));
                prev = n;
            }
        }
        {   // clap: three quick bursts and a tail
            auto* v = make(Clap, 0.22);
            for (size_t i = 0; i < v->size(); ++i) {
                const double t = double(i) / kSr;
                double env = std::exp(-t * 22);
                if (t < 0.035) env *= (std::fmod(t, 0.012) < 0.005 ? 1.0 : 0.3);
                (*v)[i] = float(nd(rng) * env * 0.55);
            }
        }
        {   // tom: slower pitch sweep
            auto* v = make(Tom, 0.45);
            double ph = 0;
            for (size_t i = 0; i < v->size(); ++i) {
                const double t = double(i) / kSr;
                ph += 2 * kPi * (95 + 70 * std::exp(-t * 12)) / kSr;
                (*v)[i] = float(std::sin(ph) * std::exp(-t * 6) * 0.9);
            }
        }
        {   // crash: long bright noise
            auto* v = make(Crash, 1.4);
            float prev = 0;
            for (size_t i = 0; i < v->size(); ++i) {
                const double t = double(i) / kSr;
                const float n = nd(rng);
                (*v)[i] = (n - prev) * 0.45f * float(std::exp(-t * 3.2));
                prev = n;
            }
        }
        {   // rim shot: a click with a short resonant body
            auto* v = make(Rim, 0.08);
            for (size_t i = 0; i < v->size(); ++i) {
                const double t = double(i) / kSr;
                (*v)[i] = float((std::sin(2 * kPi * 1700 * t) * 0.6 + nd(rng) * 0.3) * std::exp(-t * 90));
            }
        }
    }
};

const Kit& kit() {
    static const Kit k;   // built once, thread-safe
    return k;
}
}  // namespace

DrumKit::DrumKit() {
    pVol = &add("Volume", 0, 1, 0.8f);
    pKick = &add("Kick", 0, 1, 1.0f);
    pSnare = &add("Snare", 0, 1, 1.0f);
    pHats = &add("Hats", 0, 1, 0.8f);
    pPerc = &add("Percussion", 0, 1, 0.9f);
}

const std::vector<float>& DrumKit::sound(int piece) { return kit().s[size_t(std::clamp(piece, 0, kDrumPieceCount - 1))]; }

int DrumKit::pieceForNote(int n) {
    switch (n) {
    case 35: case 36: return Kick;
    case 38: case 40: return Snare;
    case 42: case 44: return HatClosed;
    case 46: return HatOpen;
    case 39: return Clap;
    case 41: case 43: case 45: case 47: case 48: case 50: return Tom;
    case 49: case 51: case 52: case 55: case 57: case 59: return Crash;
    case 37: case 56: case 75: return Rim;
    default: return Rim;
    }
}

void DrumKit::reset() {
    for (Voice& v : m_voices) v = Voice{};
}

void DrumKit::noteOn(int note, int velocity) {
    const int piece = pieceForNote(note);
    if (piece == HatClosed)   // a closed hat chokes the open hat
        for (Voice& v : m_voices)
            if (v.snd && v.piece == HatOpen) v = Voice{};
    Voice* slot = nullptr;
    for (Voice& v : m_voices)
        if (!v.snd) { slot = &v; break; }
    if (!slot) {   // steal the voice that has played the longest
        slot = &m_voices[0];
        for (Voice& v : m_voices)
            if (v.pos > slot->pos) slot = &v;
    }
    float level = pPerc->get();
    if (piece == Kick) level = pKick->get();
    else if (piece == Snare) level = pSnare->get();
    else if (piece == HatClosed || piece == HatOpen) level = pHats->get();
    slot->snd = &sound(piece);
    slot->pos = 0;
    slot->piece = piece;
    slot->gain = float(std::clamp(velocity, 1, 127)) / 127.f * level;
}

void DrumKit::render(float* io, int frames) {
    const float vol = pVol->get();
    for (Voice& v : m_voices) {
        if (!v.snd) continue;
        const size_t n = std::min<size_t>(size_t(frames), v.snd->size() - v.pos);
        const float g = v.gain * vol;
        for (size_t i = 0; i < n; ++i) {
            const float s = (*v.snd)[v.pos + i] * g;
            io[2 * i] += s;
            io[2 * i + 1] += s;
        }
        v.pos += n;
        if (v.pos >= v.snd->size()) v = Voice{};
    }
}

}  // namespace dsp
