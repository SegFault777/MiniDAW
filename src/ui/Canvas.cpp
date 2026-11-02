// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Canvas.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include "../core/I18n.h"
#include "FontData.h"

namespace ui {

Rgba withAlpha(Rgba c, int a) { return (c & 0x00ffffffu) | (Rgba(std::clamp(a, 0, 255)) << 24); }

Rgba mix(Rgba a, Rgba b, double t) {
    t = std::clamp(t, 0.0, 1.0);
    auto ch = [&](int s) { return int(std::lround(double((a >> s) & 255) * (1 - t) + double((b >> s) & 255) * t)); };
    return rgba(ch(16), ch(8), ch(0), ch(24));
}
Rgba lighten(Rgba c, double amount) { return mix(c, rgba(255, 255, 255, alphaOf(c)), amount); }
Rgba darken(Rgba c, double amount) { return mix(c, rgba(0, 0, 0, alphaOf(c)), amount); }

Rect Rect::intersect(const Rect& o) const {
    const int x0 = std::max(x, o.x), y0 = std::max(y, o.y), x1 = std::min(right(), o.right()), y1 = std::min(bottom(), o.bottom());
    return {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

// ---- built-in text ----
namespace {
const GlyphFace& faceOf(Font f) {
    switch (f) {
    case Font::Regular13: return gRegular13;
    case Font::Bold11: return gBold11;
    case Font::Bold13: return gBold13;
    default: return gRegular11;
    }
}

/// Decodes the next code point of a UTF-8 string; unknown bytes become U+FFFD.
unsigned nextCodePoint(const std::string& s, size_t& i) {
    const unsigned char c = (unsigned char)s[i++];
    if (c < 0x80) return c;
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    unsigned cp = extra == 3 ? c & 7 : extra == 2 ? c & 15 : extra == 1 ? c & 31 : 0xFFFD;
    while (extra-- > 0 && i < s.size() && ((unsigned char)s[i] & 0xC0) == 0x80) cp = (cp << 6) | ((unsigned char)s[i++] & 63);
    return cp;
}

class BuiltinText : public TextRenderer {
public:
    int width(const std::string& s, Font f) override {
        const GlyphFace& face = faceOf(f);
        long total = 0;
        for (size_t i = 0; i < s.size();) total += glyph(face, nextCodePoint(s, i)).advance64;
        return int((total + 32) / 64);
    }
    int lineHeight(Font f) override { return faceOf(f).lineHeight + 1; }
    void draw(Canvas& c, int x, int y, const std::string& s, Rgba color, Font f) override {
        const GlyphFace& face = faceOf(f);
        long pen = long(x) * 64;
        for (size_t i = 0; i < s.size();) {
            const Glyph& g = glyph(face, nextCodePoint(s, i));
            const int gx = int((pen + 32) / 64) + g.xoff, gy = y + face.ascent + g.yoff;
            for (int row = 0; row < g.h; ++row)
                for (int col = 0; col < g.w; ++col) {
                    const int cov = face.data[g.offset + row * g.w + col];
                    if (cov) c.blend(gx + col, gy + row, color, cov);
                }
            pen += g.advance64;
        }
    }

private:
    static const Glyph& glyph(const GlyphFace& face, unsigned cp) { return face.glyphs[cp >= 32 && cp < 127 ? cp - 32 : '?' - 32]; }
};

BuiltinText gBuiltin;
TextRenderer* gRenderer = nullptr;
}  // namespace

namespace { std::shared_ptr<const core::TranslationTable> gTable; }
void Canvas::setTranslations(std::shared_ptr<const core::TranslationTable> table) { gTable = std::move(table); }
const std::string& Canvas::translate(const std::string& s) { return gTable ? gTable->translate(s) : s; }

void Canvas::setTextRenderer(TextRenderer* r) { gRenderer = r; }
TextRenderer* Canvas::textRenderer() { return gRenderer ? gRenderer : &gBuiltin; }

// ---- canvas ----
void Canvas::resize(int w, int h) {
    m_w = std::max(0, w);
    m_h = std::max(0, h);
    m_px.assign(size_t(m_w) * size_t(m_h), 0xff000000u);
    m_clips.clear();
    m_clip = bounds();
}

void Canvas::pushClip(const Rect& r) {
    m_clips.push_back(m_clip);
    m_clip = m_clip.intersect(r);
}
void Canvas::popClip() {
    if (m_clips.empty()) return;
    m_clip = m_clips.back();
    m_clips.pop_back();
}

void Canvas::blend(int x, int y, Rgba c, int coverage) {
    if (!m_clip.contains(x, y)) return;
    const int a = alphaOf(c) * std::clamp(coverage, 0, 255) / 255;
    if (a <= 0) return;
    Rgba& d = m_px[size_t(y) * size_t(m_w) + size_t(x)];
    if (a >= 255) { d = c | 0xff000000u; return; }
    auto ch = [&](int s) { return Rgba((((c >> s) & 255) * Rgba(a) + ((d >> s) & 255) * Rgba(255 - a) + 127) / 255); };
    d = 0xff000000u | (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

void Canvas::clear(Rgba c) { std::fill(m_px.begin(), m_px.end(), c | 0xff000000u); }

void Canvas::fillRect(const Rect& r, Rgba c) {
    const Rect v = r.intersect(m_clip);
    for (int y = v.y; y < v.bottom(); ++y)
        for (int x = v.x; x < v.right(); ++x) blend(x, y, c);
}

void Canvas::fillGradient(const Rect& r, Rgba top, Rgba bottom) {
    for (int y = 0; y < r.h; ++y) fillRect({r.x, r.y + y, r.w, 1}, mix(top, bottom, r.h > 1 ? double(y) / double(r.h - 1) : 0.0));
}

void Canvas::strokeRect(const Rect& r, Rgba c) {
    if (r.empty()) return;
    hline(r.x, r.right() - 1, r.y, c);
    hline(r.x, r.right() - 1, r.bottom() - 1, c);
    vline(r.x, r.y + 1, r.bottom() - 2, c);
    vline(r.right() - 1, r.y + 1, r.bottom() - 2, c);
}

void Canvas::hline(int x0, int x1, int y, Rgba c) { fillRect({x0, y, x1 - x0 + 1, 1}, c); }
void Canvas::vline(int x, int y0, int y1, Rgba c) { fillRect({x, y0, 1, y1 - y0 + 1}, c); }

namespace {
/// Coverage (0..255) of pixel (px, py) inside a rounded rectangle; 'inner' shrinks the shape for outlines.
int roundCoverage(const Rect& r, double radius, int px, int py) {
    const double cx = px + 0.5, cy = py + 0.5;
    const double left = r.x, top = r.y, right = r.right(), bottom = r.bottom();
    if (cx < left || cx > right || cy < top || cy > bottom) return 0;
    const double qx = std::max({left + radius - cx, 0.0, cx - (right - radius)});
    const double qy = std::max({top + radius - cy, 0.0, cy - (bottom - radius)});
    if (qx <= 0 || qy <= 0) return 255;
    const double d = std::sqrt(qx * qx + qy * qy);
    return int(std::lround(255.0 * std::clamp(radius - d + 0.5, 0.0, 1.0)));
}
}  // namespace

void Canvas::fillRoundRect(const Rect& r, int radius, Rgba c) {
    radius = std::clamp(radius, 0, std::min(r.w, r.h) / 2);
    if (radius == 0) { fillRect(r, c); return; }
    const Rect v = r.intersect(m_clip);
    for (int y = v.y; y < v.bottom(); ++y)
        for (int x = v.x; x < v.right(); ++x) blend(x, y, c, roundCoverage(r, radius, x, y));
}

void Canvas::strokeRoundRect(const Rect& r, int radius, Rgba c) {
    radius = std::clamp(radius, 0, std::min(r.w, r.h) / 2);
    const Rect in = r.inset(1);
    const Rect v = r.intersect(m_clip);
    for (int y = v.y; y < v.bottom(); ++y)
        for (int x = v.x; x < v.right(); ++x) {
            const int outer = roundCoverage(r, radius, x, y), inner = in.empty() ? 0 : roundCoverage(in, std::max(0, radius - 1), x, y);
            blend(x, y, c, std::max(0, outer - inner));
        }
}

void Canvas::line(double x0, double y0, double x1, double y1, Rgba c) {
    // Xiaolin Wu's anti-aliased line.
    const bool steep = std::fabs(y1 - y0) > std::fabs(x1 - x0);
    if (steep) { std::swap(x0, y0); std::swap(x1, y1); }
    if (x0 > x1) { std::swap(x0, x1); std::swap(y0, y1); }
    const double dx = x1 - x0, dy = y1 - y0, grad = dx == 0 ? 1.0 : dy / dx;
    auto plot = [&](int x, int y, double k) {
        if (steep) blend(y, x, c, int(255 * std::clamp(k, 0.0, 1.0)));
        else blend(x, y, c, int(255 * std::clamp(k, 0.0, 1.0)));
    };
    double y = y0;
    for (int x = int(std::floor(x0)); x <= int(std::floor(x1)); ++x) {
        const double f = y - std::floor(y);
        plot(x, int(std::floor(y)), 1 - f);
        plot(x, int(std::floor(y)) + 1, f);
        y += grad;
    }
}

void Canvas::fillCircle(double cx, double cy, double r, Rgba c) {
    const int x0 = int(std::floor(cx - r - 1)), x1 = int(std::ceil(cx + r + 1)), y0 = int(std::floor(cy - r - 1)), y1 = int(std::ceil(cy + r + 1));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const double d = std::hypot(x + 0.5 - cx, y + 0.5 - cy);
            blend(x, y, c, int(std::lround(255.0 * std::clamp(r - d + 0.5, 0.0, 1.0))));
        }
}

void Canvas::strokeCircle(double cx, double cy, double r, double width, Rgba c) {
    const double h = width / 2;
    const int x0 = int(std::floor(cx - r - width)), x1 = int(std::ceil(cx + r + width)), y0 = int(std::floor(cy - r - width)), y1 = int(std::ceil(cy + r + width));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const double d = std::fabs(std::hypot(x + 0.5 - cx, y + 0.5 - cy) - r);
            blend(x, y, c, int(std::lround(255.0 * std::clamp(h - d + 0.5, 0.0, 1.0))));
        }
}

void Canvas::arc(double cx, double cy, double r, double a0, double a1, double width, Rgba c) {
    const double h = width / 2;
    const int x0 = int(std::floor(cx - r - width)), x1 = int(std::ceil(cx + r + width)), y0 = int(std::floor(cy - r - width)), y1 = int(std::ceil(cy + r + width));
    if (a1 < a0) std::swap(a0, a1);
    const double twoPi = 6.283185307179586;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const double dx = x + 0.5 - cx, dy = y + 0.5 - cy;
            double a = std::atan2(dy, dx);
            while (a < a0) a += twoPi;
            while (a > a0 + twoPi) a -= twoPi;
            if (a > a1) continue;
            const double d = std::fabs(std::hypot(dx, dy) - r);
            blend(x, y, c, int(std::lround(255.0 * std::clamp(h - d + 0.5, 0.0, 1.0))));
        }
}

void Canvas::fillTriangle(double x0, double y0, double x1, double y1, double x2, double y2, Rgba c) {
    const int minX = int(std::floor(std::min({x0, x1, x2}))), maxX = int(std::ceil(std::max({x0, x1, x2})));
    const int minY = int(std::floor(std::min({y0, y1, y2}))), maxY = int(std::ceil(std::max({y0, y1, y2})));
    auto edge = [](double ax, double ay, double bx, double by, double px, double py) { return (px - ax) * (by - ay) - (py - ay) * (bx - ax); };
    const double area = edge(x0, y0, x1, y1, x2, y2);
    if (area == 0) return;
    const double len01 = std::hypot(x1 - x0, y1 - y0), len12 = std::hypot(x2 - x1, y2 - y1), len20 = std::hypot(x0 - x2, y0 - y2);
    for (int y = minY; y <= maxY; ++y)
        for (int x = minX; x <= maxX; ++x) {
            const double px = x + 0.5, py = y + 0.5, s = area > 0 ? 1.0 : -1.0;
            // signed distance to every edge in pixels, inside is positive
            const double d = std::min({s * edge(x0, y0, x1, y1, px, py) / std::max(len01, 1e-9), s * edge(x1, y1, x2, y2, px, py) / std::max(len12, 1e-9),
                                       s * edge(x2, y2, x0, y0, px, py) / std::max(len20, 1e-9)});
            blend(x, y, c, int(std::lround(255.0 * std::clamp(d + 0.5, 0.0, 1.0))));
        }
}

int Canvas::textWidth(const std::string& s, Font f) { return textRenderer()->width(translate(s), f); }
int Canvas::lineHeight(Font f) { return textRenderer()->lineHeight(f); }

void Canvas::text(int x, int y, const std::string& src, Rgba c, Font f, Align a) {
    if (src.empty()) return;
    const std::string& s = translate(src);
    const int w = textRenderer()->width(s, f);
    if (a == Align::Center) x -= w / 2;
    else if (a == Align::Right) x -= w;
    textRenderer()->draw(*this, x, y, s, c, f);
}

std::string Canvas::fit(const std::string& src, int maxWidth, Font f) {
    const std::string s = translate(src);
    if (textWidth(s, f) <= maxWidth) return s;
    std::string t = s;
    while (!t.empty()) {
        do t.pop_back(); while (!t.empty() && ((unsigned char)t.back() & 0xC0) == 0x80);   // whole UTF-8 characters
        if (textWidth(t + "..", f) <= maxWidth) return t + "..";
    }
    return {};
}

void Canvas::textIn(const Rect& r, const std::string& s, Rgba c, Font f, Align a) {
    const std::string t = fit(s, r.w, f);
    const int y = r.y + (r.h - lineHeight(f)) / 2;
    const int x = a == Align::Left ? r.x : a == Align::Center ? r.x + r.w / 2 : r.right();
    pushClip(r);
    text(x, y, t, c, f, a);
    popClip();
}

bool Canvas::savePpm(const std::string& path) const {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", m_w, m_h);
    for (Rgba p : m_px) { const unsigned char rgb3[3] = {(unsigned char)(p >> 16), (unsigned char)(p >> 8), (unsigned char)p}; std::fwrite(rgb3, 1, 3, f); }
    std::fclose(f);
    return true;
}

}  // namespace ui
