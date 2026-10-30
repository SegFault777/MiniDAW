#include "../src/dsp/Effects.h"
#include <cmath>
#include <cstdio>
int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
using namespace dsp;
// RMS of the last half of a processed stereo sine
static float rmsAfter(const char* id, float hz, void (*setup)(Effect&)) {
  auto fx = createEffect(id); CHECK(fx != nullptr); if (!fx) return 0;
  if (setup) setup(*fx);
  const int N = 44100;
  std::vector<float> b(size_t(N) * 2);
  for (int i = 0; i < N; ++i) b[2*i] = b[2*i+1] = 0.5f * std::sin(2 * 3.14159265f * hz * float(i) / kSampleRate);
  for (int off = 0; off < N; off += 512) fx->process(b.data() + 2 * off, std::min(512, N - off));
  double s = 0; for (int i = N / 2; i < N; ++i) { CHECK(std::isfinite(b[2*i]) && std::isfinite(b[2*i+1])); s += double(b[2*i]) * b[2*i]; }
  return float(std::sqrt(s / (N / 2)));
}
int main() {
  const float ref = 0.5f / std::sqrt(2.f);
  // filter defaults are transparent
  CHECK(std::fabs(rmsAfter("filter", 1000, nullptr) - ref) < 0.01f);
  // high cut at 500 Hz removes a 8 kHz tone, keeps a 100 Hz tone
  auto hc = [](Effect& e) { e.find("High Cut")->set(500); };
  CHECK(rmsAfter("filter", 8000, hc) < ref * 0.05f);
  CHECK(rmsAfter("filter", 100, hc) > ref * 0.85f);
  // low cut at 1 kHz removes a 60 Hz tone, keeps a 8 kHz tone
  auto lc = [](Effect& e) { e.find("Low Cut")->set(1000); };
  CHECK(rmsAfter("filter", 60, lc) < ref * 0.05f);
  CHECK(rmsAfter("filter", 8000, lc) > ref * 0.85f);
  // mix = 0 is a bypass
  auto dry = [](Effect& e) { e.find("High Cut")->set(200); e.find("Mix")->set(0); };
  CHECK(std::fabs(rmsAfter("filter", 8000, dry) - ref) < 0.01f);
  // chorus: finite, keeps the level roughly, dry at mix 0
  float c = rmsAfter("chorus", 440, nullptr); CHECK(c > ref * 0.5f && c < ref * 1.5f);
  auto cd = [](Effect& e) { e.find("Mix")->set(0); };
  CHECK(std::fabs(rmsAfter("chorus", 440, cd) - ref) < 0.005f);
  // the chorus really modulates: its output differs from the input
  { auto fx = createEffect("chorus"); std::vector<float> in(8192 * 2), out; for (size_t i = 0; i < in.size() / 2; ++i) in[2*i] = in[2*i+1] = std::sin(float(i) * 0.1f);
    out = in; fx->process(out.data(), 8192); double d = 0; for (size_t i = 4096 * 2; i < out.size(); ++i) d += std::fabs(out[i] - in[i]); CHECK(d > 10.0); }
  { auto ids = insertEffectIds(); bool a = false, b = false; for (auto& s : ids) { a |= s == "chorus"; b |= s == "filter"; } CHECK(a && b); }
  printf("effects fails=%d\n", fails); return fails;
}
