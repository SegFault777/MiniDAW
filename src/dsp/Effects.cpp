// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Effects.h"

#include <algorithm>
#include <cmath>

namespace dsp {
namespace {

constexpr float kPi = 3.14159265358979f;

inline float dbToLin(float db) { return std::exp(db * 0.11512925f); }  // ln(10) / 20

/// Transposed direct form II biquad with RBJ cookbook coefficient helpers.
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1[2] = {0, 0}, z2[2] = {0, 0};

    void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }

    float process(int ch, float x) {
        const float y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return y;
    }

    void set(float nb0, float nb1, float nb2, float a0, float na1, float na2) {
        b0 = nb0 / a0; b1 = nb1 / a0; b2 = nb2 / a0; a1 = na1 / a0; a2 = na2 / a0;
    }

    void peaking(float f, float gainDb, float q) {
        f = std::min(f, 0.49f * kSampleRate);
        const float A = std::sqrt(dbToLin(gainDb)), w = 2 * kPi * f / kSampleRate;
        const float c = std::cos(w), alpha = std::sin(w) / (2 * q);
        set(1 + alpha * A, -2 * c, 1 - alpha * A, 1 + alpha / A, -2 * c, 1 - alpha / A);
    }

    void lowpass(float f, float q) {
        f = std::min(f, 0.49f * kSampleRate);
        const float w = 2 * kPi * f / kSampleRate, c = std::cos(w), alpha = std::sin(w) / (2 * q);
        set((1 - c) * 0.5f, 1 - c, (1 - c) * 0.5f, 1 + alpha, -2 * c, 1 - alpha);
    }

    void highpass(float f, float q) {
        f = std::min(f, 0.49f * kSampleRate);
        const float w = 2 * kPi * f / kSampleRate, c = std::cos(w), alpha = std::sin(w) / (2 * q);
        set((1 + c) * 0.5f, -(1 + c), (1 + c) * 0.5f, 1 + alpha, -2 * c, 1 - alpha);
    }

    void lowShelf(float f, float gainDb) {
        f = std::min(f, 0.49f * kSampleRate);
        const float A = std::sqrt(dbToLin(gainDb)), w = 2 * kPi * f / kSampleRate;
        const float c = std::cos(w), alpha = std::sin(w) * 0.5f * 1.41421356f;
        const float sa = 2 * std::sqrt(A) * alpha;
        set(A * ((A + 1) - (A - 1) * c + sa), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sa),
            (A + 1) + (A - 1) * c + sa, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sa);
    }

    void highShelf(float f, float gainDb) {
        f = std::min(f, 0.49f * kSampleRate);
        const float A = std::sqrt(dbToLin(gainDb)), w = 2 * kPi * f / kSampleRate;
        const float c = std::cos(w), alpha = std::sin(w) * 0.5f * 1.41421356f;
        const float sa = 2 * std::sqrt(A) * alpha;
        set(A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sa),
            (A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa);
    }
};

// ------------------------------------------------------------------ Channel EQ
class ChannelEq : public Effect {
public:
    ChannelEq() {
        pLowF = &add("Low Freq", 20, 500, 100, "Hz");   pLowF->log = true;
        pLowG = &add("Low Gain", -18, 18, 0, "dB");
        pMidF = &add("Mid Freq", 200, 8000, 1000, "Hz"); pMidF->log = true;
        pMidG = &add("Mid Gain", -18, 18, 0, "dB");
        pMidQ = &add("Mid Q", 0.2f, 10, 1);              pMidQ->log = true;
        pHighF = &add("High Freq", 2000, 16000, 8000, "Hz"); pHighF->log = true;
        pHighG = &add("High Gain", -18, 18, 0, "dB");
    }
    const char* id() const override { return "eq"; }
    const char* displayName() const override { return "Channel EQ"; }
    void reset() override { for (auto& b : m_bq) b.reset(); }

    void process(float* io, int n) override {
        const float lg = pLowG->get(), mg = pMidG->get(), hg = pHighG->get();
        if (std::fabs(lg) < 0.01f && std::fabs(mg) < 0.01f && std::fabs(hg) < 0.01f) return;
        m_bq[0].lowShelf(pLowF->get(), lg);
        m_bq[1].peaking(pMidF->get(), mg, pMidQ->get());
        m_bq[2].highShelf(pHighF->get(), hg);
        for (int i = 0; i < n; ++i)
            for (int ch = 0; ch < 2; ++ch) {
                float x = io[2 * i + ch];
                for (auto& b : m_bq) x = b.process(ch, x);
                io[2 * i + ch] = x;
            }
    }
private:
    Param *pLowF, *pLowG, *pMidF, *pMidG, *pMidQ, *pHighF, *pHighG;
    Biquad m_bq[3];
};

// ------------------------------------------------------------------ Compressor
class Compressor : public Effect {
public:
    Compressor() {
        pThr = &add("Threshold", -60, 0, -18, "dB");
        pRatio = &add("Ratio", 1, 20, 3);
        pAtk = &add("Attack", 0.1f, 100, 10, "ms");   pAtk->log = true;
        pRel = &add("Release", 10, 1000, 120, "ms");  pRel->log = true;
        pMake = &add("Makeup", 0, 24, 0, "dB");
    }
    const char* id() const override { return "comp"; }
    const char* displayName() const override { return "Compressor"; }
    void reset() override { m_env = 0; }

    void process(float* io, int n) override {
        const float thr = pThr->get(), slope = 1.f - 1.f / pRatio->get(), makeup = dbToLin(pMake->get());
        const float atk = std::exp(-1.f / (0.001f * pAtk->get() * kSampleRate));
        const float rel = std::exp(-1.f / (0.001f * pRel->get() * kSampleRate));
        for (int i = 0; i < n; ++i) {
            const float l = io[2 * i], r = io[2 * i + 1];
            const float lvl = std::max(std::fabs(l), std::fabs(r));
            m_env = lvl > m_env ? atk * m_env + (1 - atk) * lvl : rel * m_env + (1 - rel) * lvl;
            const float db = 20.f * std::log10(m_env + 1e-9f);
            const float over = db - thr;
            const float grDb = over > 0 ? over * slope : 0.f;
            const float g = dbToLin(-grDb) * makeup;
            io[2 * i] = l * g;
            io[2 * i + 1] = r * g;
        }
    }
private:
    Param *pThr, *pRatio, *pAtk, *pRel, *pMake;
    float m_env = 0;
};

// ------------------------------------------------------------------ Echo
class Echo : public Effect {
public:
    Echo() {
        pTime = &add("Time", 10, 1500, 375, "ms");
        pFb = &add("Feedback", 0, 0.95f, 0.4f);
        pDamp = &add("Damping", 0, 0.95f, 0.3f);
        pMix = &add("Mix", 0, 1, 0.3f);
        for (auto& b : m_buf) b.assign(kMax, 0.f);
    }
    const char* id() const override { return "echo"; }
    const char* displayName() const override { return "Echo"; }
    void reset() override {
        for (auto& b : m_buf) std::fill(b.begin(), b.end(), 0.f);
        m_lp[0] = m_lp[1] = 0;
    }

    void process(float* io, int n) override {
        const int d = std::clamp(int(pTime->get() * 0.001f * kSampleRate), 1, kMax - 1);
        const float fb = pFb->get(), damp = pDamp->get(), mix = pMix->get();
        for (int i = 0; i < n; ++i) {
            const int r = (m_idx - d + kMax) % kMax;
            for (int ch = 0; ch < 2; ++ch) {
                const float x = io[2 * i + ch], y = m_buf[ch][size_t(r)];
                m_lp[ch] = y * (1 - damp) + m_lp[ch] * damp;
                m_buf[ch][size_t(m_idx)] = x + m_lp[ch] * fb;
                io[2 * i + ch] = x * (1 - mix) + y * mix;
            }
            if (++m_idx >= kMax) m_idx = 0;
        }
    }
private:
    static constexpr int kMax = 70000;  // a little over 1.5 s at 44.1 kHz
    Param *pTime, *pFb, *pDamp, *pMix;
    std::vector<float> m_buf[2];
    float m_lp[2] = {0, 0};
    int m_idx = 0;
};

// ------------------------------------------------------------------ Reverb (Freeverb topology)
struct Comb {
    std::vector<float> buf; int idx = 0; float store = 0;
    void init(int n) { buf.assign(size_t(n), 0.f); idx = 0; store = 0; }
    float process(float in, float fb, float damp) {
        const float out = buf[size_t(idx)];
        store = out * (1 - damp) + store * damp;
        buf[size_t(idx)] = in + store * fb;
        if (++idx >= int(buf.size())) idx = 0;
        return out;
    }
};

struct Allpass {
    std::vector<float> buf; int idx = 0;
    void init(int n) { buf.assign(size_t(n), 0.f); idx = 0; }
    float process(float in) {
        const float b = buf[size_t(idx)], out = -in + b;
        buf[size_t(idx)] = in + b * 0.5f;
        if (++idx >= int(buf.size())) idx = 0;
        return out;
    }
};

class Reverb : public Effect {
public:
    Reverb() {
        pSize = &add("Size", 0, 1, 0.7f);
        pDamp = &add("Damping", 0, 1, 0.5f);
        pWidth = &add("Width", 0, 1, 1);
        pMix = &add("Mix", 0, 1, 0.25f);
        static const int comb[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
        static const int allp[4] = {556, 441, 341, 225};
        for (int c = 0; c < 2; ++c) {
            const int spread = c * 23;  // decorrelates the right channel
            for (int i = 0; i < 8; ++i) m_comb[c][i].init(comb[i] + spread);
            for (int i = 0; i < 4; ++i) m_ap[c][i].init(allp[i] + spread);
        }
    }
    const char* id() const override { return "reverb"; }
    const char* displayName() const override { return "Reverb"; }
    void reset() override {
        for (int c = 0; c < 2; ++c) {
            for (auto& x : m_comb[c]) x.init(int(x.buf.size()));
            for (auto& x : m_ap[c]) x.init(int(x.buf.size()));
        }
    }

    void process(float* io, int n) override {
        const float room = pSize->get() * 0.28f + 0.7f, damp = pDamp->get() * 0.4f;
        const float width = pWidth->get(), mix = pMix->get();
        const float w1 = width * 0.5f + 0.5f, w2 = (1 - width) * 0.5f;
        for (int i = 0; i < n; ++i) {
            const float l = io[2 * i], r = io[2 * i + 1], in = (l + r) * 0.015f;
            float oL = 0, oR = 0;
            for (int c = 0; c < 8; ++c) {
                oL += m_comb[0][c].process(in, room, damp);
                oR += m_comb[1][c].process(in, room, damp);
            }
            for (int a = 0; a < 4; ++a) {
                oL = m_ap[0][a].process(oL);
                oR = m_ap[1][a].process(oR);
            }
            io[2 * i] = l * (1 - mix) + (oL * w1 + oR * w2) * mix;
            io[2 * i + 1] = r * (1 - mix) + (oR * w1 + oL * w2) * mix;
        }
    }
private:
    Param *pSize, *pDamp, *pWidth, *pMix;
    Comb m_comb[2][8];
    Allpass m_ap[2][4];
};

// ------------------------------------------------------------------ Overdrive
class Overdrive : public Effect {
public:
    Overdrive() {
        pDrive = &add("Drive", 0, 40, 12, "dB");
        pTone = &add("Tone", 500, 12000, 5000, "Hz");  pTone->log = true;
        pLevel = &add("Level", -24, 6, -6, "dB");
        pMix = &add("Mix", 0, 1, 1);
    }
    const char* id() const override { return "drive"; }
    const char* displayName() const override { return "Overdrive"; }
    void reset() override { m_lp[0] = m_lp[1] = 0; }

    void process(float* io, int n) override {
        const float drive = dbToLin(pDrive->get()), level = dbToLin(pLevel->get()), mix = pMix->get();
        const float a = std::exp(-2 * kPi * pTone->get() / kSampleRate);
        for (int i = 0; i < n; ++i)
            for (int ch = 0; ch < 2; ++ch) {
                const float x = io[2 * i + ch];
                const float y = std::tanh(x * drive);
                m_lp[ch] = y * (1 - a) + m_lp[ch] * a;
                io[2 * i + ch] = x * (1 - mix) + m_lp[ch] * level * mix;
            }
    }
private:
    Param *pDrive, *pTone, *pLevel, *pMix;
    float m_lp[2] = {0, 0};
};

// ------------------------------------------------------------------ Chorus (v0.6)
class Chorus : public Effect {
public:
    Chorus() {
        pRate = &add("Rate", 0.05f, 5, 0.8f, "Hz");  pRate->log = true;
        pDepth = &add("Depth", 0, 1, 0.5f);
        pMix = &add("Mix", 0, 1, 0.5f);
        for (auto& b : m_buf) b.assign(kMax, 0.f);
    }
    const char* id() const override { return "chorus"; }
    const char* displayName() const override { return "Chorus"; }
    void reset() override {
        for (auto& b : m_buf) std::fill(b.begin(), b.end(), 0.f);
        m_idx = 0;
        m_phase = 0;
    }

    void process(float* io, int n) override {
        const float inc = pRate->get() / kSampleRate, depth = pDepth->get(), mix = pMix->get();
        const float base = 0.012f * kSampleRate, swing = 0.006f * kSampleRate * depth;   // 12 ms +- 6 ms
        for (int i = 0; i < n; ++i) {
            for (int ch = 0; ch < 2; ++ch) m_buf[ch][size_t(m_idx)] = io[2 * i + ch];
            for (int ch = 0; ch < 2; ++ch) {
                const float lfo = std::sin(2 * kPi * (m_phase + 0.25f * float(ch)));   // right channel runs a quarter cycle later
                float rpos = float(m_idx) - (base + swing * lfo);
                if (rpos < 0) rpos += float(kMax);
                const int i0 = int(rpos), i1 = (i0 + 1) % kMax;
                const float frac = rpos - float(i0);
                const float wet = m_buf[ch][size_t(i0)] * (1 - frac) + m_buf[ch][size_t(i1)] * frac;
                io[2 * i + ch] = io[2 * i + ch] * (1 - mix * 0.5f) + wet * mix * 0.5f * 1.4f;
            }
            m_phase += inc;
            if (m_phase >= 1.f) m_phase -= 1.f;
            if (++m_idx >= kMax) m_idx = 0;
        }
    }
private:
    static constexpr int kMax = 2048;
    Param *pRate, *pDepth, *pMix;
    std::vector<float> m_buf[2];
    int m_idx = 0;
    float m_phase = 0;
};

// ------------------------------------------------------------------ Filter: low cut + high cut with resonance (v0.6)
class Filter : public Effect {
public:
    Filter() {
        pLow = &add("Low Cut", 20, 2000, 20, "Hz");     pLow->log = true;
        pHigh = &add("High Cut", 200, 20000, 20000, "Hz");  pHigh->log = true;
        pRes = &add("Resonance", 0.5f, 8, 0.707f);      pRes->log = true;
        pMix = &add("Mix", 0, 1, 1);
    }
    const char* id() const override { return "filter"; }
    const char* displayName() const override { return "Filter"; }
    void reset() override { m_hp.reset(); m_lp.reset(); }

    void process(float* io, int n) override {
        const float low = pLow->get(), high = pHigh->get(), q = pRes->get(), mix = pMix->get();
        const bool useHp = low > 21.f, useLp = high < 19500.f;
        if (useHp) m_hp.highpass(low, q);
        if (useLp) m_lp.lowpass(high, q);
        for (int i = 0; i < n; ++i)
            for (int ch = 0; ch < 2; ++ch) {
                const float x = io[2 * i + ch];
                float y = x;
                if (useHp) y = m_hp.process(ch, y);
                if (useLp) y = m_lp.process(ch, y);
                io[2 * i + ch] = x * (1 - mix) + y * mix;
            }
    }
private:
    Param *pLow, *pHigh, *pRes, *pMix;
    Biquad m_hp, m_lp;
};

// ------------------------------------------------------------------ Limiter
class Limiter : public Effect {
public:
    Limiter() {
        pCeil = &add("Ceiling", -12, 0, -0.3f, "dB");
        pRel = &add("Release", 10, 1000, 120, "ms");  pRel->log = true;
    }
    const char* id() const override { return "limiter"; }
    const char* displayName() const override { return "Limiter"; }
    void reset() override { m_gain = 1; }

    void process(float* io, int n) override {
        const float ceil = dbToLin(pCeil->get());
        const float rel = std::exp(-1.f / (0.001f * pRel->get() * kSampleRate));
        for (int i = 0; i < n; ++i) {
            const float peak = std::max(std::fabs(io[2 * i]), std::fabs(io[2 * i + 1]));
            const float target = peak > ceil ? ceil / peak : 1.f;
            // Instant attack guarantees the ceiling; the release is smoothed.
            m_gain = target < m_gain ? target : target + (m_gain - target) * rel;
            io[2 * i] *= m_gain;
            io[2 * i + 1] *= m_gain;
        }
    }
private:
    Param *pCeil, *pRel;
    float m_gain = 1;
};

}  // namespace

std::shared_ptr<Effect> createEffect(const std::string& id) {
    if (id == "eq") return std::make_shared<ChannelEq>();
    if (id == "comp") return std::make_shared<Compressor>();
    if (id == "echo") return std::make_shared<Echo>();
    if (id == "reverb") return std::make_shared<Reverb>();
    if (id == "drive") return std::make_shared<Overdrive>();
    if (id == "chorus") return std::make_shared<Chorus>();
    if (id == "filter") return std::make_shared<Filter>();
    if (id == "limiter") return std::make_shared<Limiter>();
    return nullptr;
}

const std::vector<std::string>& insertEffectIds() {
    static const std::vector<std::string> ids = {"eq", "comp", "echo", "reverb", "drive", "chorus", "filter"};
    return ids;
}

}  // namespace dsp
