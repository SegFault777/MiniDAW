// Loads the example plug-ins with dlopen and runs them through dsp::NativeEffect (Linux / macOS).
#include <dlfcn.h>
#include <cmath>
#include <cstdio>
#include <set>
#include "../src/dsp/NativeEffect.h"
int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
using namespace dsp;

static const MdEffectDesc* load(const char* path) {
  void* h = dlopen(path, RTLD_NOW);
  if (!h) { printf("dlopen failed: %s\n", dlerror()); ++fails; return nullptr; }
  auto count = reinterpret_cast<int (*)()>(dlsym(h, "mdplugin_count"));
  auto get = reinterpret_cast<const MdEffectDesc* (*)(int)>(dlsym(h, "mdplugin_get"));
  CHECK(count && get); if (!count || !get) return nullptr;
  CHECK(count() == 1 && get(1) == nullptr);
  return get(0);
}

int main() {
  const MdEffectDesc* trem = load("/tmp/md_tremolo.so");
  const MdEffectDesc* crush = load("/tmp/md_bitcrusher.so");
  CHECK(validDescriptor(trem) && validDescriptor(crush));
  if (!trem || !crush) return 1;
  {
    auto fx = NativeEffect::create(trem);
    CHECK(fx != nullptr);
    CHECK(std::string(fx->id()) == "native:minidaw.example.tremolo" && std::string(fx->displayName()) == "Tremolo");
    CHECK(fx->params.size() == 2 && fx->find("Rate") && fx->find("Depth"));
    CHECK(fx->find("Depth")->def == 0.5f && fx->find("Rate")->unit == "Hz");
    // depth 1: the gain swings from ~0 to 1
    fx->find("Depth")->set(1.f); fx->find("Rate")->set(10.f);
    std::vector<float> b(44100 * 2, 1.f);
    for (int off = 0; off < 44100; off += 256) fx->process(b.data() + 2 * off, std::min(256, 44100 - off));
    float lo = 9, hi = -9; for (size_t i = 0; i < b.size(); i += 2) { lo = std::min(lo, b[i]); hi = std::max(hi, b[i]); }
    CHECK(lo < 0.01f && hi > 0.99f);
    // depth 0 passes the signal through
    fx->find("Depth")->set(0.f);
    std::vector<float> c(2048, 0.7f); fx->process(c.data(), 1024);
    for (float v : c) CHECK(std::fabs(v - 0.7f) < 1e-5f);
    // parameters are clamped by the host
    fx->find("Rate")->set(1000.f); CHECK(fx->find("Rate")->get() == 20.f);
    fx->reset();
  }
  {
    auto fx = NativeEffect::create(crush);
    CHECK(fx && fx->params.size() == 3);
    fx->find("Bits")->set(2.f); fx->find("Downsample")->set(1.f); fx->find("Mix")->set(1.f);
    std::vector<float> b(8192); for (size_t i = 0; i < b.size() / 2; ++i) b[2*i] = b[2*i+1] = std::sin(float(i) * 0.05f);
    fx->process(b.data(), 4096);
    std::set<float> distinct(b.begin(), b.end()); CHECK(distinct.size() <= 5);   // 2 bits: levels -1, -0.5, 0, 0.5, 1
  }
  // invalid descriptors are rejected
  { MdEffectDesc d = *trem; d.apiVersion = 99; CHECK(!validDescriptor(&d)); CHECK(NativeEffect::create(&d) == nullptr); }
  { MdEffectDesc d = *trem; d.process = nullptr; CHECK(!validDescriptor(&d)); }
  { MdEffectDesc d = *trem; d.paramCount = MDPLUGIN_MAX_PARAMS + 1; CHECK(!validDescriptor(&d)); }
  { MdEffectDesc d = *trem; d.id = ""; CHECK(!validDescriptor(&d)); }
  CHECK(!validDescriptor(nullptr));
  printf("native plugin fails=%d\n", fails); return fails;
}
