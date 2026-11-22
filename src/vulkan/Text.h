// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Vulkan UI, layer 1: glyph cache. A FontSource turns one character into an 8 bit coverage bitmap (GDI on Windows, the built-in
// Liberation Sans bitmaps everywhere else); Fonts packs them into one atlas texture that the GPU samples.
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace vkui {

struct GlyphBitmap {
    int w = 0, h = 0;
    int bearingX = 0, bearingY = 0;   // offset of the bitmap's top-left corner from the pen position at the top of the line box
    float advance = 0;                // device px
    std::vector<std::uint8_t> a8;     // w * h coverage
};

class FontSource {
public:
    virtual ~FontSource() = default;
    virtual bool rasterize(std::uint32_t codepoint, bool bold, int px, GlyphBitmap& out) = 0;
    virtual float lineHeight(bool bold, int px) = 0;   // device px
};

struct GlyphInfo {
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    int w = 0, h = 0, bearingX = 0, bearingY = 0;
    float advance = 0;
};

/// UTF-8 decoding shared by the text code.
std::uint32_t nextCodepoint(const std::string& s, size_t& i);
void appendUtf8(std::string& out, std::uint32_t cp);

class Fonts {
public:
    static constexpr int kAtlas = 1024;
    explicit Fonts(std::unique_ptr<FontSource> source);
    const GlyphInfo& glyph(std::uint32_t cp, bool bold, int px);
    float lineHeight(bool bold, int px);
    /// The atlas (kAtlas x kAtlas, one byte per texel). 'revision' grows whenever it changed and must be uploaded again.
    const std::vector<std::uint8_t>& atlas() const { return m_atlas; }
    std::uint64_t revision() const { return m_revision; }
    /// Set when the atlas was full and had to be cleared: glyphs queued earlier in the same frame are stale, draw the frame again.
    bool consumeOverflow() { const bool o = m_overflow; m_overflow = false; return o; }
    /// Rasterizing a glyph can be slow (Windows GDI font linking for Korean / Japanese ...), and a dialog or the tutorial brings hundreds of new
    /// glyphs at once. With a budget (ms per frame; 0 = unlimited, the default) the glyphs beyond it are skipped for this frame (drawn empty,
    /// not cached) and takePending() tells the caller to draw another frame right away: the window stays responsive and the text fills in.
    void setFrameBudgetMs(double ms) { m_budgetMs = ms; }
    void beginFrame() { m_spentMs = 0; }
    bool takePending() { const bool p = m_pending; m_pending = false; return p; }

private:
    std::unique_ptr<FontSource> m_src;
    std::unordered_map<std::uint64_t, GlyphInfo> m_glyphs;
    std::vector<std::uint8_t> m_atlas;
    int m_penX = 1, m_penY = 1, m_rowH = 0;
    std::uint64_t m_revision = 1;
    bool m_overflow = false;
    double m_budgetMs = 0, m_spentMs = 0;
    bool m_pending = false;
    GlyphInfo m_placeholder;
};

/// Liberation Sans bitmaps from src/ui/FontData (ASCII only, 11 and 13 px): the font of the headless tests and of the fallback.
std::unique_ptr<FontSource> makeBuiltinFontSource();

}  // namespace vkui
