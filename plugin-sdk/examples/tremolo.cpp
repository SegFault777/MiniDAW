// Example MiniDAW plug-in: Tremolo (amplitude modulation).
// Build (Linux/macOS):  g++ -std=c++17 -O2 -fPIC -shared tremolo.cpp -o tremolo.so
// Build (Windows):      cl /nologo /O2 /EHsc /LD tremolo.cpp /Fe:tremolo.dll
// Copy the result into the "plugins" folder next to MiniDAW.exe and choose it from an insert slot's menu.
#include <cmath>
#include "../mdplugin.h"

namespace {

struct Tremolo {
    float rate = 5.f, depth = 0.5f, phase = 0.f;
    float sampleRate = 44100.f;
};

const MdParam kParams[] = {
    {"Rate", "Hz", 0.1f, 20.f, 5.f},
    {"Depth", "", 0.f, 1.f, 0.5f},
};

void* create(int sampleRate) {
    auto* t = new Tremolo;
    t->sampleRate = float(sampleRate);
    return t;
}
void destroy(void* p) { delete static_cast<Tremolo*>(p); }
void reset(void* p) { static_cast<Tremolo*>(p)->phase = 0.f; }
void setParam(void* p, int index, float v) {
    auto* t = static_cast<Tremolo*>(p);
    if (index == 0) t->rate = v;
    else if (index == 1) t->depth = v;
}
void process(void* p, float* io, int frames) {
    auto* t = static_cast<Tremolo*>(p);
    const float inc = t->rate / t->sampleRate;
    for (int i = 0; i < frames; ++i) {
        const float lfo = 0.5f + 0.5f * std::sin(6.2831853f * t->phase);   // 0..1
        const float g = 1.f - t->depth * (1.f - lfo);
        io[2 * i] *= g;
        io[2 * i + 1] *= g;
        t->phase += inc;
        if (t->phase >= 1.f) t->phase -= 1.f;
    }
}

const MdEffectDesc kDesc = {MDPLUGIN_API_VERSION, "minidaw.example.tremolo", "Tremolo", "MiniDAW examples", 2, kParams,
                            create, destroy, reset, setParam, process};

}  // namespace

extern "C" {
MDPLUGIN_EXPORT int mdplugin_count(void) { return 1; }
MDPLUGIN_EXPORT const MdEffectDesc* mdplugin_get(int index) { return index == 0 ? &kDesc : nullptr; }
}
