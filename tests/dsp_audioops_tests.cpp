#include "../src/dsp/AudioOps.h"
#include <cstdio>
#include <cstring>
int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
static std::uint32_t rd32(const std::vector<std::uint8_t>& v, size_t o) { return v[o] | v[o+1] << 8 | v[o+2] << 16 | std::uint32_t(v[o+3]) << 24; }
int main() {
  using namespace dsp;
  // ---- fades
  CHECK(fadeGain(0, 1000, 0, 0) == 1.f);
  CHECK(fadeGain(0, 1000, 100, 0) < 1e-6f);                  // starts silent
  CHECK(std::fabs(fadeGain(50, 1000, 100, 0) - 0.5f) < 1e-4f);// raised cosine: half-way is 0.5
  CHECK(fadeGain(100, 1000, 100, 0) == 1.f && fadeGain(500, 1000, 100, 100) == 1.f);
  CHECK(fadeGain(999, 1000, 0, 100) < 1e-6f);                // ends silent
  CHECK(std::fabs(fadeGain(949, 1000, 0, 100) - 0.5f) < 2e-2f);
  { float prev = -1; bool mono = true; for (int i = 0; i < 100; ++i) { float g = fadeGain(i, 1000, 100, 0); if (g < prev) mono = false; prev = g; } CHECK(mono); }
  // overlapping fades are shortened, never > 1 and never NaN
  for (int i = 0; i < 100; ++i) { float g = fadeGain(i, 100, 80, 80); CHECK(g >= 0.f && g <= 1.f); }
  // a split crossfade of the same signal sums to 1 (sin^2 + cos^2)
  CHECK(std::fabs(fadeGain(30, 1000, 100, 0) + fadeGain(1000 - 1 - 70, 1000, 0, 100) - 1.f) < 1e-3f);
  // ---- db / normalize
  CHECK(std::fabs(dbToLinear(-6.0206f) - 0.5f) < 1e-3f && std::fabs(linearToDb(2.f) - 6.0206f) < 1e-3f);
  CHECK(std::fabs(normalizeGain(0.25f, 0.f) - 4.f) < 1e-4f);
  CHECK(std::fabs(normalizeGain(0.5f, -6.0206f) - 1.f) < 1e-3f);
  CHECK(normalizeGain(0.f, -1.f) == 1.f);
  // ---- reverse / peak
  float a[] = {0.1f, -0.1f, 0.2f, -0.2f, 0.9f, -0.3f};
  auto r = reverseFrames(a, 3);
  CHECK(r[0] == 0.9f && r[1] == -0.3f && r[4] == 0.1f && r[5] == -0.1f);
  CHECK(peakOf(a, 3) == 0.9f);
  { auto rr = reverseFrames(r.data(), 3); CHECK(std::memcmp(rr.data(), a, sizeof a) == 0); }
  // ---- recording offset
  { std::vector<float> t(20, 1.f); applyRecordingOffset(t, 3); CHECK(t.size() == 14);
    applyRecordingOffset(t, -2); CHECK(t.size() == 18 && t[0] == 0.f && t[3] == 0.f && t[4] == 1.f);
    std::vector<float> s(10, 1.f); applyRecordingOffset(s, 1000); CHECK(s.size() == 2);   // keeps one frame
    std::vector<float> z(10, 1.f); applyRecordingOffset(z, 0); CHECK(z.size() == 10); }
  // ---- WAV
  { auto h = wavHeader(WavFormat::Pcm16, 44100, 100); CHECK(h.size() == 44 && rd32(h, 4) == 36 + 400 && rd32(h, 40) == 400); CHECK(h[20] == 1 && h[34] == 16); }
  { auto h = wavHeader(WavFormat::Pcm24, 48000, 100); CHECK(h.size() == 44 && rd32(h, 40) == 600 && rd32(h, 24) == 48000 && rd32(h, 28) == 48000 * 6 && h[32] == 6 && h[34] == 24); }
  { auto h = wavHeader(WavFormat::Float32, 44100, 100); CHECK(h.size() == 56 && h[20] == 3 && std::memcmp(&h[36], "fact", 4) == 0 && rd32(h, 44) == 100 && std::memcmp(&h[48], "data", 4) == 0 && rd32(h, 52) == 800); CHECK(rd32(h, 4) == h.size() - 8 + 800); }
  { std::uint32_t seed = 1; float in[] = {1.f, -1.f, 0.f, 2.f};
    std::vector<std::uint8_t> o16; encodeSamples(in, 2, WavFormat::Pcm16, 1.f, false, seed, o16);
    CHECK(o16.size() == 8); CHECK(o16[0] == 0xFF && o16[1] == 0x7F);          // +1.0 -> 32767
    CHECK(o16[2] == 0x01 && o16[3] == 0x80);                                  // -1.0 -> -32767
    CHECK(o16[4] == 0 && o16[5] == 0 && o16[6] == 0xFF && o16[7] == 0x7F);    // 2.0 clips
    std::vector<std::uint8_t> o24; encodeSamples(in, 2, WavFormat::Pcm24, 0.5f, false, seed, o24);
    CHECK(o24.size() == 12); int v = o24[0] | o24[1] << 8 | o24[2] << 16; if (v & 0x800000) v -= 0x1000000; CHECK(std::abs(v - 4194304) <= 1);
    std::vector<std::uint8_t> of; encodeSamples(in, 2, WavFormat::Float32, 2.f, false, seed, of);
    float f0; std::memcpy(&f0, &of[0], 4); float f3; std::memcpy(&f3, &of[12], 4); CHECK(f0 == 2.f && f3 == 4.f);   // float is not clipped
    // dither: silence stays within +-1 LSB and is not all zero
    std::vector<float> sil(4000, 0.f); std::vector<std::uint8_t> od; encodeSamples(sil.data(), 2000, WavFormat::Pcm16, 1.f, true, seed, od);
    int nz = 0, big = 0; for (size_t i = 0; i < od.size(); i += 2) { int s = std::int16_t(od[i] | od[i+1] << 8); if (s) ++nz; if (std::abs(s) > 1) ++big; }
    CHECK(nz > 100 && big == 0);
  }
  printf("audioops fails=%d\n", fails); return fails;
}
