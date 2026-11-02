// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Qt-free software 2D canvas: an ARGB pixel buffer with clipping, anti-aliased shapes and text. The window layer
// only has to copy the buffer to the screen, so everything above it can be tested and screenshotted on any machine.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace core { class TranslationTable; }

namespace ui {

using Rgba = std::uint32_t;   // 0xAARRGGBB
constexpr Rgba rgba(int r, int g, int b, int a = 255) { return (Rgba(a) << 24) | (Rgba(r) << 16) | (Rgba(g) << 8) | Rgba(b); }
inline int alphaOf(Rgba c) { return int(c >> 24); }
Rgba withAlpha(Rgba c, int a);
/// Linear mix of two colors, t = 0 gives a, t = 1 gives b.
Rgba mix(Rgba a, Rgba b, double t);
Rgba lighten(Rgba c, double amount);
Rgba darken(Rgba c, double amount);

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    int right() const { return x + w; }
    int bottom() const { return y + h; }
    bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    bool empty() const { return w <= 0 || h <= 0; }
    Rect inset(int d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
    Rect inset(int dx, int dy) const { return {x + dx, y + dy, w - 2 * dx, h - 2 * dy}; }
    Rect intersect(const Rect& o) const;
};

enum class Font { Regular11, Regular13, Bold11, Bold13 };
enum class Align { Left, Center, Right };

class Canvas;

/// Draws text. The built-in renderer covers ASCII; the Windows window layer installs a GDI renderer for all of Unicode.
class TextRenderer {
public:
    virtual ~TextRenderer() = default;
    virtual int width(const std::string& utf8, Font f) = 0;
    virtual int lineHeight(Font f) = 0;
    /// Draws with the top-left of the line box at (x, y).
    virtual void draw(Canvas& c, int x, int y, const std::string& utf8, Rgba color, Font f) = 0;
};

class Canvas {
public:
    Canvas(int w = 0, int h = 0) { resize(w, h); }
    void resize(int w, int h);
    int width() const { return m_w; }
    int height() const { return m_h; }
    Rect bounds() const { return {0, 0, m_w, m_h}; }
    Rgba* pixels() { return m_px.data(); }
    const Rgba* pixels() const { return m_px.data(); }
    Rgba pixel(int x, int y) const { return m_px[size_t(y) * size_t(m_w) + size_t(x)]; }

    void pushClip(const Rect& r);
    void popClip();

    /// Source-over blend of one pixel; 'coverage' 0..255 scales the color's alpha. Honors the clip.
    void blend(int x, int y, Rgba color, int coverage = 255);
    void clear(Rgba c);
    void fillRect(const Rect& r, Rgba c);
    void fillGradient(const Rect& r, Rgba top, Rgba bottom);
    void strokeRect(const Rect& r, Rgba c);
    void fillRoundRect(const Rect& r, int radius, Rgba c);
    void strokeRoundRect(const Rect& r, int radius, Rgba c);
    void hline(int x0, int x1, int y, Rgba c);   // inclusive
    void vline(int x, int y0, int y1, Rgba c);   // inclusive
    void line(double x0, double y0, double x1, double y1, Rgba c);   // anti-aliased, 1 px wide
    void fillCircle(double cx, double cy, double r, Rgba c);
    void strokeCircle(double cx, double cy, double r, double width, Rgba c);
    /// Arc around (cx, cy) from angle a0 to a1 (radians, 0 = right, clockwise on screen).
    void arc(double cx, double cy, double r, double a0, double a1, double width, Rgba c);
    void fillTriangle(double x0, double y0, double x1, double y1, double x2, double y2, Rgba c);

    void text(int x, int y, const std::string& s, Rgba c, Font f = Font::Regular11, Align a = Align::Left);
    /// Text inside a rectangle, vertically centered, shortened with ".." when it does not fit.
    void textIn(const Rect& r, const std::string& s, Rgba c, Font f = Font::Regular11, Align a = Align::Left);
    int textWidth(const std::string& s, Font f = Font::Regular11);
    int lineHeight(Font f = Font::Regular11);
    std::string fit(const std::string& s, int maxWidth, Font f = Font::Regular11);

    /// Installs the text renderer used by every canvas (nullptr restores the built-in ASCII renderer).
    static void setTextRenderer(TextRenderer* r);
    static TextRenderer* textRenderer();
    /// Every string that is drawn is looked up here first (the English text is the key), so the whole UI can be
    /// translated without touching the code that draws it. nullptr turns translation off.
    static void setTranslations(std::shared_ptr<const core::TranslationTable> table);
    static const std::string& translate(const std::string& s);

    bool savePpm(const std::string& path) const;

private:
    int m_w = 0, m_h = 0;
    std::vector<Rgba> m_px;
    std::vector<Rect> m_clips;
    Rect m_clip;
};

}  // namespace ui
