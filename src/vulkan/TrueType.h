// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Vulkan UI, layer 1: a small TrueType reader and rasteriser for icon fonts (Material Symbols). Reads 'glyf' outlines (simple and
// composite glyphs, the default instance of a variable font), maps characters with a 'cmap' (format 4 and 12) and draws a glyph
// into an 8 bit coverage bitmap with exact area anti-aliasing. No hinting, no kerning, no shaping: one character, one glyph.
#include <cstdint>
#include <string>
#include <vector>

#include "Text.h"

namespace vkui {

class TrueTypeFont {
public:
    /// Takes the file contents. Returns false (and sets 'error') for anything that is not a TrueType-flavoured font.
    bool load(std::vector<std::uint8_t> data, std::string* error = nullptr);
    bool loaded() const { return m_upem > 0; }
    int unitsPerEm() const { return m_upem; }
    int glyphCount() const { return m_numGlyphs; }

    /// Glyph index of a character (0 = none).
    int glyphIndex(std::uint32_t codepoint) const;

    /// Glyph index by the name in the 'post' table (format 2; the icon fonts name their glyphs after the icon, "play_arrow"). 0 = none.
    /// The 258 standard Macintosh names ("space", "A" ...) are not looked up.
    int glyphByName(const std::string& name) const;

    /// Draws a glyph so that the em square (unitsPerEm) is 'px' pixels high: the bitmap's bearing is measured from the top left
    /// corner of that square, as for the text of the UI (advance = px). Returns false for a missing glyph. An empty glyph (space)
    /// returns true with a 0 x 0 bitmap.
    bool rasterize(int glyphIndex, int px, GlyphBitmap& out) const;

private:
    struct Pt { float x, y; bool on; };
    bool glyphPoints(int glyph, std::vector<std::vector<Pt>>& contours, int depth, float a, float b, float c, float d, float dx, float dy) const;
    bool glyphRange(int glyph, std::size_t& begin, std::size_t& end) const;

    std::vector<std::uint8_t> m_data;
    std::size_t m_glyf = 0, m_glyfLen = 0, m_loca = 0, m_cmap = 0;
    int m_upem = 0, m_numGlyphs = 0, m_locaLong = 0;
    // cmap subtable (format 4 or 12) found by load()
    std::size_t m_sub = 0;
    int m_subFormat = 0;
    std::vector<std::pair<std::string, int>> m_names;   // sorted by name; from the post table
    mutable int m_budget = 0;   // glyph records one rasterize() may visit (composites in damaged fonts can nest without end)
};

}  // namespace vkui
