// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Vulkan UI, layer 1: a GPU-agnostic draw list. Everything the user interface draws becomes triangles in one vertex format
// (see shaders/ui.frag for how the GPU - and SoftRaster.cpp for how the test renderer - turns them into pixels).
// Coordinates are in logical units (dp); the list multiplies them by the display scale and snaps edges to device pixels.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace vkui {

using Color = std::uint32_t;   // 0xAARRGGBB
constexpr Color rgb(int r, int g, int b, int a = 255) { return (Color(a) << 24) | (Color(r) << 16) | (Color(g) << 8) | Color(b); }
inline int alphaOf(Color c) { return int(c >> 24); }
inline Color withAlpha(Color c, int a) { return (c & 0x00FFFFFFu) | (Color(std::clamp(a, 0, 255)) << 24); }
inline Color scaleAlpha(Color c, float k) { return withAlpha(c, int(std::lround(float(alphaOf(c)) * k))); }
Color mix(Color a, Color b, float t);
Color lighten(Color c, float amount);
Color darken(Color c, float amount);

struct Vec2 { float x = 0, y = 0; };

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    float right() const { return x + w; }
    float bottom() const { return y + h; }
    float cx() const { return x + w * 0.5f; }
    float cy() const { return y + h * 0.5f; }
    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    bool empty() const { return w <= 0 || h <= 0; }
    Rect inset(float d) const { return {x + d, y + d, std::max(0.f, w - 2 * d), std::max(0.f, h - 2 * d)}; }
    Rect inset(float dx, float dy) const { return {x + dx, y + dy, std::max(0.f, w - 2 * dx), std::max(0.f, h - 2 * dy)}; }
    Rect intersect(const Rect& o) const;
    // layout helpers: cut a strip off one side and return it (this rect shrinks)
    Rect takeLeft(float n) { Rect r{x, y, std::min(n, w), h}; x += r.w; w -= r.w; return r; }
    Rect takeRight(float n) { Rect r{right() - std::min(n, w), y, std::min(n, w), h}; w -= r.w; return r; }
    Rect takeTop(float n) { Rect r{x, y, w, std::min(n, h)}; y += r.h; h -= r.h; return r; }
    Rect takeBottom(float n) { Rect r{x, bottom() - std::min(n, h), w, std::min(n, h)}; h -= r.h; return r; }
};

/// The one vertex format. 'shape' selects what the fragment shader does:
///   s >= 0  rounded rectangle, signed distance field: uv = position from the centre in px, (hx, hy) half size, r corner radius,
///           s = 0 filled / s > 0 outline of that width
///   s == -1 glyph: uv = atlas coordinates, coverage = atlas red channel
///   s == -2 anti-aliased line: uv.y runs -1 .. 1 across the line, hx = half width incl. feather
///   s == -3 plain triangle / gradient
struct Vertex {
    float x, y;
    float u, v;
    float hx, hy, r, s;
    std::uint32_t rgba;   // R in the lowest byte (VK_FORMAT_R8G8B8A8_UNORM), straight alpha
};
static_assert(sizeof(Vertex) == 36, "Vertex layout is shared with the shaders");

struct Batch {
    std::uint32_t first = 0, count = 0;   // index range
    int x = 0, y = 0, w = 0, h = 0;       // scissor in device pixels
};

enum class Align { Left, Center, Right };
struct TextStyle {
    float size = 12;   // dp
    bool bold = false;
};

class Fonts;

class DrawList {
public:
    void begin(float widthDp, float heightDp, float scale, Fonts* fonts);
    float width() const { return m_w; }
    float height() const { return m_h; }
    float scale() const { return m_scale; }
    Fonts* fonts() const { return m_fonts; }

    void pushClip(const Rect& r);
    void popClip();
    Rect clip() const { return m_clips.empty() ? Rect{0, 0, m_w, m_h} : m_clips.back(); }

    void rect(const Rect& r, Color c);
    void rectGradient(const Rect& r, Color top, Color bottom);
    void rrect(const Rect& r, float radius, Color c);
    void rrectGradient(const Rect& r, float radius, Color top, Color bottom);
    void rrectStroke(const Rect& r, float radius, float width, Color c);
    void line(float x0, float y0, float x1, float y1, float width, Color c);
    void hline(float x0, float x1, float y, Color c) { rect({x0, y, x1 - x0, 1}, c); }
    void vline(float x, float y0, float y1, Color c) { rect({x, y0, 1, y1 - y0}, c); }
    void polyline(const std::vector<Vec2>& pts, float width, Color c);
    void circle(float cx, float cy, float radius, Color c);
    void ring(float cx, float cy, float radius, float width, Color c);
    void arc(float cx, float cy, float radius, float a0, float a1, float width, Color c);   // radians, 0 = right, clockwise on screen
    void triangle(Vec2 a, Vec2 b, Vec2 c, Color col);
    void shadow(const Rect& r, float radius, float spread, Color c);

    /// Text with the top of the line box at y. Returns the width drawn (dp).
    float text(float x, float y, const std::string& s, Color c, TextStyle st = {}, Align a = Align::Left);
    /// Text vertically centred in r and shortened with ".." when it does not fit.
    void textIn(const Rect& r, const std::string& s, Color c, TextStyle st = {}, Align a = Align::Left);
    float textWidth(const std::string& s, TextStyle st = {}) const;
    /// One glyph of an icon font (a virtual code point, see IconFont.h) with its em square of 'emDp' centred on (cx, cy).
    void glyph(float cx, float cy, float emDp, std::uint32_t codepoint, Color c);
    float lineHeight(TextStyle st = {}) const;
    std::string fit(const std::string& s, float maxWidth, TextStyle st = {}) const;

    const std::vector<Vertex>& vertices() const { return m_v; }
    const std::vector<std::uint32_t>& indices() const { return m_i; }
    const std::vector<Batch>& batches() const { return m_b; }
    int deviceWidth() const { return int(std::lround(m_w * m_scale)); }
    int deviceHeight() const { return int(std::lround(m_h * m_scale)); }

private:
    float px(float v) const { return v * m_scale; }
    float snapPx(float v) const { return std::round(v * m_scale); }
    void quad(const Vertex& a, const Vertex& b, const Vertex& c, const Vertex& d);
    void startBatch();
    void sdfQuad(const Rect& r, float radius, float stroke, Color top, Color bottom);

    float m_w = 0, m_h = 0, m_scale = 1;
    Fonts* m_fonts = nullptr;
    std::vector<Vertex> m_v;
    std::vector<std::uint32_t> m_i;
    std::vector<Batch> m_b;
    std::vector<Rect> m_clips;
};

}  // namespace vkui
