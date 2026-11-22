// TrueType reader / rasteriser for icon fonts: compared with FreeType (reference numbers made with Pillow on the same font),
// plus the cases that matter for Material Symbols: holes, curves made only of off-curve points, composite glyphs, empty glyphs,
// code points outside the BMP (cmap format 12) and damaged files.
#include <cmath>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "../src/vulkan/TrueType.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { ++fails; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static std::vector<std::uint8_t> readAll(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static double coverage(const vkui::GlyphBitmap& g) { double s = 0; for (auto v : g.a8) s += v; return s / 255.0; }

int main() {
    using namespace vkui;
    const std::string dir = "data/";
    TrueTypeFont f;
    std::string err;
    CHECK(f.load(readAll(dir + "icontest.ttf"), &err));
    CHECK(f.unitsPerEm() == 1000 && f.glyphCount() == 8);

    // character map: BMP private use (format 4) and plane 15 (format 12)
    CHECK(f.glyphIndex(0xE000) == 1 && f.glyphIndex(0xE001) == 2 && f.glyphIndex(0xE005) == 6);
    CHECK(f.glyphIndex(0xF0001) == 7);
    // glyph names (post table, format 2): the icon fonts are searched by name
    CHECK(f.glyphByName("sq") == 1 && f.glyphByName("tri") == 2 && f.glyphByName("blank") == 6 && f.glyphByName("far") == 7);
    CHECK(f.glyphByName("nope") == 0 && f.glyphByName("") == 0 && f.glyphByName("SQ") == 0);
    CHECK(f.glyphIndex('A') == 0 && f.glyphIndex(0xE100) == 0 && f.glyphIndex(0x10FFFF) == 0);

    // coverage sums against FreeType
    std::ifstream ref(dir + "icontest_reference.tsv");
    std::string line;
    int rows = 0, bad = 0;
    double worst = 0;
    while (std::getline(ref, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream is(line);
        unsigned cp; int px, w, h; double sum;
        is >> cp >> px >> sum >> w >> h;
        GlyphBitmap g;
        const bool ok = f.rasterize(f.glyphIndex(cp), px, g);
        ++rows;
        if (cp == 0xE005) { CHECK(ok && g.w == 0 && g.h == 0); continue; }   // empty glyph
        if (!ok) { ++bad; std::printf("no glyph for U+%X\n", cp); continue; }
        const double mine = coverage(g);
        const double tol = std::max(1.2, sum * 0.03);
        worst = std::max(worst, std::fabs(mine - sum) / std::max(sum, 1.0));
        if (std::fabs(mine - sum) > tol) { ++bad; std::printf("U+%X at %d px: coverage %.2f, FreeType %.2f\n", cp, px, mine, sum); }
    }
    std::printf("ttf: %d glyphs compared with FreeType, %d differ, worst relative error %.3f\n", rows, bad, worst);
    CHECK(rows == 21 && bad == 0);

    // exact geometry: a 600 x 600 unit square at 24 px = 14.4 px, full inside, bearings from the top left of the em square
    {
        GlyphBitmap g;
        CHECK(f.rasterize(f.glyphIndex(0xE000), 24, g));
        CHECK(std::fabs(coverage(g) - 14.4 * 14.4) < 1.0);
        CHECK(g.bearingX == 4 && g.bearingY == 4);                       // 200 units = 4.8 px -> floor 4; y: (1000-800)*0.024 = 4.8
        CHECK(g.a8[std::size_t(g.h / 2) * std::size_t(g.w) + std::size_t(g.w / 2)] == 255);
        CHECK(g.advance == 24.f);
    }
    // a ring has a hole
    {
        GlyphBitmap g;
        CHECK(f.rasterize(f.glyphIndex(0xE002), 48, g));
        CHECK(g.a8[std::size_t(g.h / 2) * std::size_t(g.w) + std::size_t(g.w / 2)] == 0);
        CHECK(g.a8[3 * std::size_t(g.w) + std::size_t(g.w / 2)] > 200 || g.a8[5 * std::size_t(g.w) + std::size_t(g.w / 2)] > 200);
        const double outer = 38.4 * 38.4, inner = 19.2 * 19.2;
        CHECK(std::fabs(coverage(g) - (outer - inner)) < 3.0);
    }
    // an all-off-curve circle: area close to pi r^2 (r = 400 units, 19.2 px at 48 px; the spline sits slightly outside the circle)
    {
        GlyphBitmap g;
        CHECK(f.rasterize(f.glyphIndex(0xE003), 48, g));
        const double area = coverage(g), circle = 3.14159265 * 19.2 * 19.2;
        CHECK(area > circle * 0.97 && area < circle * 1.06);
    }
    // a composite glyph (the square at half size) is a quarter of the square's area
    {
        GlyphBitmap a, b;
        CHECK(f.rasterize(f.glyphIndex(0xE000), 48, a) && f.rasterize(f.glyphIndex(0xE004), 48, b));
        CHECK(std::fabs(coverage(b) - coverage(a) * 0.25) < 2.0);
    }
    // missing glyph and absurd sizes
    {
        GlyphBitmap g;
        CHECK(!f.rasterize(0, 24, g) && !f.rasterize(99, 24, g) && !f.rasterize(1, 0, g));
        CHECK(f.rasterize(1, 400, g) && g.w > 0);
    }
    // damaged input never crashes: truncated files and random byte flips
    {
        const auto good = readAll(dir + "icontest.ttf");
        CHECK(!TrueTypeFont().load({}, &err) && !TrueTypeFont().load({1, 2, 3}, &err));
        for (std::size_t cut : {size_t(11), size_t(40), good.size() / 2, good.size() - 1}) {
            TrueTypeFont t;
            t.load(std::vector<std::uint8_t>(good.begin(), good.begin() + std::ptrdiff_t(cut)), &err);
            GlyphBitmap g;
            for (std::uint32_t cp : {0xE000u, 0xE003u, 0xF0001u}) t.rasterize(t.glyphIndex(cp), 24, g);
        }
        std::mt19937 rng(7);
        int loaded = 0;
        for (int n = 0; n < 3000; ++n) {
            auto bad2 = good;
            for (int k = 0; k < 1 + int(rng() % 4); ++k) bad2[rng() % bad2.size()] = std::uint8_t(rng());
            TrueTypeFont t;
            if (!t.load(std::move(bad2), &err)) continue;
            ++loaded;
            GlyphBitmap g;
            for (std::uint32_t cp : {0xE000u, 0xE001u, 0xE002u, 0xE003u, 0xE004u, 0xE005u, 0xF0001u}) t.rasterize(t.glyphIndex(cp), 17, g);
        }
        std::printf("ttf: %d damaged fonts still loaded and were rendered without a crash\n", loaded);
        CHECK(loaded > 100);
    }
    std::printf("ttf fails=%d\n", fails);
    return fails ? 1 : 0;
}
