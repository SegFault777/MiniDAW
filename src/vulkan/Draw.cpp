// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Draw.h"
#include "Text.h"

namespace vkui {

Color mix(Color a, Color b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    auto ch = [&](int shift) { return Color(std::lround(float((a >> shift) & 255) * (1 - t) + float((b >> shift) & 255) * t)); };
    return (ch(24) << 24) | (ch(16) << 16) | (ch(8) << 8) | ch(0);
}
Color lighten(Color c, float amount) { return mix(c, withAlpha(0xFFFFFFFFu, alphaOf(c)), amount); }
Color darken(Color c, float amount) { return mix(c, withAlpha(0xFF000000u, alphaOf(c)), amount); }

Rect Rect::intersect(const Rect& o) const {
    const float x0 = std::max(x, o.x), y0 = std::max(y, o.y), x1 = std::min(right(), o.right()), y1 = std::min(bottom(), o.bottom());
    return {x0, y0, std::max(0.f, x1 - x0), std::max(0.f, y1 - y0)};
}

namespace {
std::uint32_t pack(Color c) {   // 0xAARRGGBB -> bytes R, G, B, A in memory order
    return ((c >> 16) & 255u) | (((c >> 8) & 255u) << 8) | ((c & 255u) << 16) | (((c >> 24) & 255u) << 24);
}
Vertex vert(float x, float y, float u, float v, float hx, float hy, float r, float s, Color c) { return {x, y, u, v, hx, hy, r, s, pack(c)}; }
}  // namespace

void DrawList::begin(float widthDp, float heightDp, float scale, Fonts* fonts) {
    m_w = widthDp;
    m_h = heightDp;
    m_scale = std::max(0.5f, scale);
    m_fonts = fonts;
    m_v.clear();
    m_i.clear();
    m_b.clear();
    m_clips.clear();
    startBatch();
}

void DrawList::startBatch() {
    const Rect c = clip();
    Batch b;
    const int x0 = std::max(0, int(std::lround(c.x * m_scale))), y0 = std::max(0, int(std::lround(c.y * m_scale)));
    const int x1 = std::min(deviceWidth(), int(std::lround(c.right() * m_scale))), y1 = std::min(deviceHeight(), int(std::lround(c.bottom() * m_scale)));
    b.x = x0; b.y = y0; b.w = std::max(0, x1 - x0); b.h = std::max(0, y1 - y0);
    b.first = std::uint32_t(m_i.size());
    if (!m_b.empty() && m_b.back().count == 0) { m_b.back() = b; return; }
    m_b.push_back(b);
}

void DrawList::pushClip(const Rect& r) {
    m_clips.push_back(m_clips.empty() ? r.intersect({0, 0, m_w, m_h}) : r.intersect(m_clips.back()));
    startBatch();
}
void DrawList::popClip() {
    if (!m_clips.empty()) m_clips.pop_back();
    startBatch();
}

void DrawList::quad(const Vertex& a, const Vertex& b, const Vertex& c, const Vertex& d) {
    const std::uint32_t base = std::uint32_t(m_v.size());
    m_v.push_back(a); m_v.push_back(b); m_v.push_back(c); m_v.push_back(d);
    for (std::uint32_t k : {0u, 1u, 2u, 2u, 1u, 3u}) m_i.push_back(base + k);
    m_b.back().count += 6;
}

void DrawList::rect(const Rect& r, Color c) { rectGradient(r, c, c); }

void DrawList::rectGradient(const Rect& r, Color top, Color bottom) {
    if (r.w <= 0 || r.h <= 0 || (alphaOf(top) == 0 && alphaOf(bottom) == 0)) return;
    const float x0 = snapPx(r.x), y0 = snapPx(r.y);
    const float x1 = std::max(snapPx(r.right()), x0 + 1), y1 = std::max(snapPx(r.bottom()), y0 + 1);
    quad(vert(x0, y0, 0, 0, 0, 0, 0, -3, top), vert(x1, y0, 0, 0, 0, 0, 0, -3, top), vert(x0, y1, 0, 0, 0, 0, 0, -3, bottom), vert(x1, y1, 0, 0, 0, 0, 0, -3, bottom));
}

void DrawList::sdfQuad(const Rect& r, float radius, float stroke, Color top, Color bottom) {
    if (r.w <= 0 || r.h <= 0 || (alphaOf(top) == 0 && alphaOf(bottom) == 0)) return;
    const float x0 = snapPx(r.x), y0 = snapPx(r.y), x1 = std::max(snapPx(r.right()), x0 + 1), y1 = std::max(snapPx(r.bottom()), y0 + 1);
    const float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f, hx = (x1 - x0) * 0.5f, hy = (y1 - y0) * 0.5f;
    const float rad = std::min(px(radius), std::min(hx, hy)), st = stroke > 0 ? std::max(1.f, px(stroke)) : 0.f;
    const float m = 1.0f;   // room for the anti-aliased edge
    quad(vert(x0 - m, y0 - m, x0 - m - cx, y0 - m - cy, hx, hy, rad, st, top), vert(x1 + m, y0 - m, x1 + m - cx, y0 - m - cy, hx, hy, rad, st, top),
         vert(x0 - m, y1 + m, x0 - m - cx, y1 + m - cy, hx, hy, rad, st, bottom), vert(x1 + m, y1 + m, x1 + m - cx, y1 + m - cy, hx, hy, rad, st, bottom));
}

void DrawList::rrect(const Rect& r, float radius, Color c) { sdfQuad(r, radius, 0, c, c); }
void DrawList::rrectGradient(const Rect& r, float radius, Color top, Color bottom) { sdfQuad(r, radius, 0, top, bottom); }
void DrawList::rrectStroke(const Rect& r, float radius, float width, Color c) { sdfQuad(r, radius, std::max(width, 0.01f), c, c); }

void DrawList::line(float ax, float ay, float bx, float by, float width, Color c) {
    if (alphaOf(c) == 0) return;
    float x0 = px(ax), y0 = px(ay), x1 = px(bx), y1 = px(by);
    float dx = x1 - x0, dy = y1 - y0;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4f) return;
    dx /= len; dy /= len;
    const float w = std::max(1.f, px(width)), half = w * 0.5f + 0.5f;
    x0 -= dx * 0.5f; y0 -= dy * 0.5f; x1 += dx * 0.5f; y1 += dy * 0.5f;
    const float nx = -dy * half, ny = dx * half;
    quad(vert(x0 + nx, y0 + ny, 0, -1, half, 0, 0, -2, c), vert(x0 - nx, y0 - ny, 0, 1, half, 0, 0, -2, c),
         vert(x1 + nx, y1 + ny, 0, -1, half, 0, 0, -2, c), vert(x1 - nx, y1 - ny, 0, 1, half, 0, 0, -2, c));
}

void DrawList::polyline(const std::vector<Vec2>& pts, float width, Color c) {
    for (size_t i = 0; i + 1 < pts.size(); ++i) line(pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, width, c);
}

void DrawList::circle(float cx, float cy, float radius, Color c) {
    if (radius <= 0 || alphaOf(c) == 0) return;
    const float X = px(cx), Y = px(cy), R = px(radius), m = 1.0f;
    quad(vert(X - R - m, Y - R - m, -R - m, -R - m, R, R, R, 0, c), vert(X + R + m, Y - R - m, R + m, -R - m, R, R, R, 0, c),
         vert(X - R - m, Y + R + m, -R - m, R + m, R, R, R, 0, c), vert(X + R + m, Y + R + m, R + m, R + m, R, R, R, 0, c));
}

void DrawList::ring(float cx, float cy, float radius, float width, Color c) {
    if (radius <= 0 || alphaOf(c) == 0) return;
    const float X = px(cx), Y = px(cy), R = px(radius), m = 1.0f, st = std::max(1.f, px(width));
    quad(vert(X - R - m, Y - R - m, -R - m, -R - m, R, R, R, st, c), vert(X + R + m, Y - R - m, R + m, -R - m, R, R, R, st, c),
         vert(X - R - m, Y + R + m, -R - m, R + m, R, R, R, st, c), vert(X + R + m, Y + R + m, R + m, R + m, R, R, R, st, c));
}

void DrawList::arc(float cx, float cy, float radius, float a0, float a1, float width, Color c) {
    const int n = std::max(4, int(std::ceil(std::fabs(a1 - a0) * radius * m_scale / 3.0f)));
    std::vector<Vec2> pts;
    for (int i = 0; i <= n; ++i) {
        const float a = a0 + (a1 - a0) * float(i) / float(n);
        pts.push_back({cx + std::cos(a) * radius, cy + std::sin(a) * radius});
    }
    polyline(pts, width, c);
}

void DrawList::triangle(Vec2 a, Vec2 b, Vec2 c, Color col) {
    if (alphaOf(col) == 0) return;
    const std::uint32_t base = std::uint32_t(m_v.size());
    m_v.push_back(vert(px(a.x), px(a.y), 0, 0, 0, 0, 0, -3, col));
    m_v.push_back(vert(px(b.x), px(b.y), 0, 0, 0, 0, 0, -3, col));
    m_v.push_back(vert(px(c.x), px(c.y), 0, 0, 0, 0, 0, -3, col));
    for (std::uint32_t k : {0u, 1u, 2u}) m_i.push_back(base + k);
    m_b.back().count += 3;
    line(a.x, a.y, b.x, b.y, 1, col);   // the soft edge: triangles have no coverage of their own
    line(b.x, b.y, c.x, c.y, 1, col);
    line(c.x, c.y, a.x, a.y, 1, col);
}

void DrawList::shadow(const Rect& r, float radius, float spread, Color c) {
    constexpr int kLayers = 6;
    for (int i = 0; i < kLayers; ++i) {
        const float e = spread * float(kLayers - i) / float(kLayers);
        rrect(r.inset(-e), radius + e, scaleAlpha(c, 1.0f / float(kLayers) * 0.9f));
    }
}

// ----------------------------------------------------------------------------------------------------------------- text

float DrawList::text(float x, float y, const std::string& s, Color c, TextStyle st, Align a) {
    if (!m_fonts || s.empty() || alphaOf(c) == 0) return 0;
    const int pxSize = std::max(6, int(std::lround(st.size * m_scale)));
    struct Q { GlyphInfo g; };
    std::vector<GlyphInfo> glyphs;
    float total = 0;
    for (size_t i = 0; i < s.size();) {
        const GlyphInfo g = m_fonts->glyph(nextCodepoint(s, i), st.bold, pxSize);   // copied: the cache may be reset by the next lookup
        glyphs.push_back(g);
        total += g.advance;
    }
    float pen = px(x);
    if (a == Align::Center) pen -= total * 0.5f;
    else if (a == Align::Right) pen -= total;
    pen = std::round(pen);
    const float top = std::round(px(y));
    for (const GlyphInfo& g : glyphs) {
        if (g.w > 0 && g.h > 0) {
            const float gx = std::round(pen) + float(g.bearingX), gy = top + float(g.bearingY);
            quad(vert(gx, gy, g.u0, g.v0, 0, 0, 0, -1, c), vert(gx + float(g.w), gy, g.u1, g.v0, 0, 0, 0, -1, c),
                 vert(gx, gy + float(g.h), g.u0, g.v1, 0, 0, 0, -1, c), vert(gx + float(g.w), gy + float(g.h), g.u1, g.v1, 0, 0, 0, -1, c));
        }
        pen += g.advance;
    }
    return total / m_scale;
}

void DrawList::glyph(float cx, float cy, float emDp, std::uint32_t codepoint, Color c) {
    if (!m_fonts || alphaOf(c) == 0) return;
    const int pxSize = std::max(6, int(std::lround(emDp * m_scale)));
    const GlyphInfo g = m_fonts->glyph(codepoint, false, pxSize);   // copied: the cache may be reset by the next lookup
    if (g.w <= 0 || g.h <= 0) return;
    const float left = std::round(px(cx) - float(pxSize) * 0.5f), top = std::round(px(cy) - float(pxSize) * 0.5f);
    const float gx = left + float(g.bearingX), gy = top + float(g.bearingY);
    quad(vert(gx, gy, g.u0, g.v0, 0, 0, 0, -1, c), vert(gx + float(g.w), gy, g.u1, g.v0, 0, 0, 0, -1, c),
         vert(gx, gy + float(g.h), g.u0, g.v1, 0, 0, 0, -1, c), vert(gx + float(g.w), gy + float(g.h), g.u1, g.v1, 0, 0, 0, -1, c));
}

float DrawList::textWidth(const std::string& s, TextStyle st) const {
    if (!m_fonts || s.empty()) return 0;
    const int pxSize = std::max(6, int(std::lround(st.size * m_scale)));
    float total = 0;
    for (size_t i = 0; i < s.size();) total += m_fonts->glyph(nextCodepoint(s, i), st.bold, pxSize).advance;
    return total / m_scale;
}

float DrawList::lineHeight(TextStyle st) const {
    if (!m_fonts) return st.size;
    return m_fonts->lineHeight(st.bold, std::max(6, int(std::lround(st.size * m_scale)))) / m_scale;
}

std::string DrawList::fit(const std::string& s, float maxWidth, TextStyle st) const {
    if (textWidth(s, st) <= maxWidth) return s;
    std::vector<size_t> cuts;   // byte offsets of character boundaries
    for (size_t i = 0; i < s.size();) { cuts.push_back(i); nextCodepoint(s, i); }
    const float dots = textWidth("..", st);
    for (size_t n = cuts.size(); n-- > 0;) {
        const std::string head = s.substr(0, cuts[n]);
        if (textWidth(head, st) + dots <= maxWidth) return head + "..";
    }
    return "";
}

void DrawList::textIn(const Rect& r, const std::string& s, Color c, TextStyle st, Align a) {
    if (r.w <= 0 || s.empty()) return;
    const std::string t = fit(s, r.w, st);
    const float y = r.y + (r.h - lineHeight(st)) * 0.5f;
    text(a == Align::Left ? r.x : a == Align::Center ? r.cx() : r.right(), y, t, c, st, a);
}

}  // namespace vkui
