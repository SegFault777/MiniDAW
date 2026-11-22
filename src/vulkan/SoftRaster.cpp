// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "SoftRaster.h"
#include <cstdio>

namespace vkui {

bool SoftImage::savePpm(const std::string& path) const {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (std::uint32_t c : px) { const unsigned char rgb3[3] = {(unsigned char)(c >> 16), (unsigned char)(c >> 8), (unsigned char)c}; std::fwrite(rgb3, 1, 3, f); }
    std::fclose(f);
    return true;
}

namespace {
struct V { float x, y, u, v, hx, hy, r, s, cr, cg, cb, ca; };

V load(const Vertex& v) {
    return {v.x, v.y, v.u, v.v, v.hx, v.hy, v.r, v.s, float(v.rgba & 255u) / 255.f, float((v.rgba >> 8) & 255u) / 255.f, float((v.rgba >> 16) & 255u) / 255.f, float(v.rgba >> 24) / 255.f};
}
float edge(float ax, float ay, float bx, float by, float px, float py) { return (bx - ax) * (py - ay) - (by - ay) * (px - ax); }
// does an edge a->b own the pixels that lie exactly on it? (top-left rule, so shared diagonals are drawn once)
bool owns(float ax, float ay, float bx, float by) { return by > ay || (by == ay && bx < ax); }
float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
}  // namespace

SoftImage softRender(const DrawList& list, const Fonts& fonts, Color clear) {
    SoftImage img;
    img.w = list.deviceWidth();
    img.h = list.deviceHeight();
    img.px.assign(size_t(img.w) * size_t(img.h), clear | 0xFF000000u);
    const auto& verts = list.vertices();
    const auto& idx = list.indices();
    const auto& atlas = fonts.atlas();
    for (const Batch& b : list.batches()) {
        if (b.count == 0 || b.w <= 0 || b.h <= 0) continue;
        for (std::uint32_t t = b.first; t + 2 < b.first + b.count + 0u; t += 3) {
            V a = load(verts[idx[t]]), c1 = load(verts[idx[t + 1]]), c2 = load(verts[idx[t + 2]]);
            float area = edge(a.x, a.y, c1.x, c1.y, c2.x, c2.y);
            if (std::fabs(area) < 1e-6f) continue;
            if (area < 0) { std::swap(c1, c2); area = -area; }
            const int x0 = std::max(b.x, int(std::floor(std::min({a.x, c1.x, c2.x})))), x1 = std::min(b.x + b.w, int(std::ceil(std::max({a.x, c1.x, c2.x}))));
            const int y0 = std::max(b.y, int(std::floor(std::min({a.y, c1.y, c2.y})))), y1 = std::min(b.y + b.h, int(std::ceil(std::max({a.y, c1.y, c2.y}))));
            const bool o0 = owns(c1.x, c1.y, c2.x, c2.y), o1 = owns(c2.x, c2.y, a.x, a.y), o2 = owns(a.x, a.y, c1.x, c1.y);
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x) {
                    const float fx = float(x) + 0.5f, fy = float(y) + 0.5f;
                    const float w0 = edge(c1.x, c1.y, c2.x, c2.y, fx, fy), w1 = edge(c2.x, c2.y, a.x, a.y, fx, fy), w2 = edge(a.x, a.y, c1.x, c1.y, fx, fy);
                    if (w0 < 0 || w1 < 0 || w2 < 0 || (w0 == 0 && !o0) || (w1 == 0 && !o1) || (w2 == 0 && !o2)) continue;
                    const float l0 = w0 / area, l1 = w1 / area, l2 = w2 / area;
                    auto lerp = [&](float V::*m) { return a.*m * l0 + c1.*m * l1 + c2.*m * l2; };
                    const float s = a.s;   // constant across a primitive
                    float cov = 1.f;
                    if (s >= 0) {
                        const float u = lerp(&V::u), v = lerp(&V::v);
                        const float qx = std::fabs(u) - (a.hx - a.r), qy = std::fabs(v) - (a.hy - a.r);
                        const float d = std::sqrt(std::max(qx, 0.f) * std::max(qx, 0.f) + std::max(qy, 0.f) * std::max(qy, 0.f)) + std::min(std::max(qx, qy), 0.f) - a.r;
                        cov = clamp01(0.5f - d);
                        if (s > 0) cov *= clamp01(0.5f + d + s);
                    } else if (s > -1.5f) {
                        const int tx = std::clamp(int(std::floor(lerp(&V::u) * float(Fonts::kAtlas))), 0, Fonts::kAtlas - 1);
                        const int ty = std::clamp(int(std::floor(lerp(&V::v) * float(Fonts::kAtlas))), 0, Fonts::kAtlas - 1);
                        cov = float(atlas[size_t(ty) * Fonts::kAtlas + size_t(tx)]) / 255.f;
                    } else if (s > -2.5f) {
                        cov = clamp01((1.f - std::fabs(lerp(&V::v))) * a.hx);
                    }
                    const float al = lerp(&V::ca) * cov;
                    if (al <= 0) continue;
                    std::uint32_t& dst = img.px[size_t(y) * size_t(img.w) + size_t(x)];
                    const float sr = lerp(&V::cr), sg = lerp(&V::cg), sb = lerp(&V::cb);
                    const float dr = float((dst >> 16) & 255u), dg = float((dst >> 8) & 255u), db = float(dst & 255u);
                    const auto out = [&](float sc, float dc) { return std::uint32_t(std::clamp(int(std::lround(sc * 255.f * al + dc * (1.f - al))), 0, 255)); };
                    dst = 0xFF000000u | (out(sr, dr) << 16) | (out(sg, dg) << 8) | out(sb, db);
                }
        }
    }
    return img;
}

}  // namespace vkui
