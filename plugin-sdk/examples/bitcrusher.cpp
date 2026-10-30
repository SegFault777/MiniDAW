// Example MiniDAW plug-in: Bitcrusher (bit depth and sample rate reduction).
// Build (Linux/macOS):  g++ -std=c++17 -O2 -fPIC -shared bitcrusher.cpp -o bitcrusher.so
// Build (Windows):      cl /nologo /O2 /EHsc /LD bitcrusher.cpp /Fe:bitcrusher.dll
#include <cmath>
#include "../mdplugin.h"

namespace {

struct Crusher {
    float bits = 8.f, hold = 4.f, mix = 1.f;
    float held[2] = {0.f, 0.f};
    float counter = 0.f;
};

const MdParam kParams[] = {
    {"Bits", "", 1.f, 16.f, 8.f},
    {"Downsample", "x", 1.f, 40.f, 4.f},
    {"Mix", "", 0.f, 1.f, 1.f},
};

void* create(int) { return new Crusher; }
void destroy(void* p) { delete static_cast<Crusher*>(p); }
void reset(void* p) {
    auto* c = static_cast<Crusher*>(p);
    c->held[0] = c->held[1] = 0.f;
    c->counter = 0.f;
}
void setParam(void* p, int index, float v) {
    auto* c = static_cast<Crusher*>(p);
    if (index == 0) c->bits = v;
    else if (index == 1) c->hold = v;
    else if (index == 2) c->mix = v;
}
void process(void* p, float* io, int frames) {
    auto* c = static_cast<Crusher*>(p);
    const float levels = std::pow(2.f, c->bits - 1.f);
    for (int i = 0; i < frames; ++i) {
        c->counter += 1.f;
        if (c->counter >= c->hold) {   // sample and hold
            c->counter -= c->hold;
            for (int ch = 0; ch < 2; ++ch) c->held[ch] = std::round(io[2 * i + ch] * levels) / levels;
        }
        for (int ch = 0; ch < 2; ++ch) io[2 * i + ch] = io[2 * i + ch] * (1.f - c->mix) + c->held[ch] * c->mix;
    }
}

const MdEffectDesc kDesc = {MDPLUGIN_API_VERSION, "minidaw.example.bitcrusher", "Bitcrusher", "MiniDAW examples", 3, kParams,
                            create, destroy, reset, setParam, process};

}  // namespace

extern "C" {
MDPLUGIN_EXPORT int mdplugin_count(void) { return 1; }
MDPLUGIN_EXPORT const MdEffectDesc* mdplugin_get(int index) { return index == 0 ? &kDesc : nullptr; }
}
