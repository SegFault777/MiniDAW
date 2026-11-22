// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "IconFont.h"
#include "Text.h"
#include <chrono>
#include <algorithm>
#include "../ui/FontData.h"

namespace vkui {

std::uint32_t nextCodepoint(const std::string& s, size_t& i) {
    const unsigned char c = (unsigned char)s[i++];
    if (c < 0x80) return c;
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : -1;
    if (extra < 0) return 0xFFFD;
    std::uint32_t cp = c & (0x3F >> extra);
    for (int k = 0; k < extra; ++k) {
        if (i >= s.size() || ((unsigned char)s[i] & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | ((unsigned char)s[i++] & 0x3F);
    }
    return cp;
}

void appendUtf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) out.push_back(char(cp));
    else if (cp < 0x800) { out.push_back(char(0xC0 | (cp >> 6))); out.push_back(char(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) { out.push_back(char(0xE0 | (cp >> 12))); out.push_back(char(0x80 | ((cp >> 6) & 0x3F))); out.push_back(char(0x80 | (cp & 0x3F))); }
    else { out.push_back(char(0xF0 | (cp >> 18))); out.push_back(char(0x80 | ((cp >> 12) & 0x3F))); out.push_back(char(0x80 | ((cp >> 6) & 0x3F))); out.push_back(char(0x80 | (cp & 0x3F))); }
}

Fonts::Fonts(std::unique_ptr<FontSource> source) : m_src(makeIconAwareSource(std::move(source))), m_atlas(size_t(kAtlas) * kAtlas, 0) {}

float Fonts::lineHeight(bool bold, int px) { return m_src ? m_src->lineHeight(bold, px) : float(px); }

const GlyphInfo& Fonts::glyph(std::uint32_t cp, bool bold, int px) {
    const std::uint64_t key = std::uint64_t(cp) | (std::uint64_t(bold ? 1 : 0) << 21) | (std::uint64_t(px) << 22);
    auto it = m_glyphs.find(key);
    if (it != m_glyphs.end()) return it->second;
    if (m_budgetMs > 0 && m_spentMs >= m_budgetMs) {   // out of time for this frame: empty for now, rasterized in a later frame
        m_pending = true;
        m_placeholder = GlyphInfo{};
        m_placeholder.advance = float(px) * (cp >= 0x2E80 ? 1.0f : 0.55f);
        return m_placeholder;
    }
    GlyphBitmap bm;
    GlyphInfo info;
    const auto t0 = std::chrono::steady_clock::now();
    if (!m_src || !m_src->rasterize(cp, bold, px, bm)) { bm = GlyphBitmap{}; bm.advance = float(px) * 0.5f; }
    if (m_budgetMs > 0) m_spentMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    info.advance = bm.advance;
    if (bm.w > 0 && bm.h > 0 && bm.w < kAtlas - 2 && bm.h < kAtlas - 2) {
        if (m_penX + bm.w + 1 > kAtlas) { m_penX = 1; m_penY += m_rowH + 1; m_rowH = 0; }
        if (m_penY + bm.h + 1 > kAtlas) {   // full: start over, the caller redraws the frame
            m_glyphs.clear();
            std::fill(m_atlas.begin(), m_atlas.end(), std::uint8_t(0));
            m_penX = 1; m_penY = 1; m_rowH = 0;
            m_overflow = true;
        }
        for (int row = 0; row < bm.h; ++row)
            std::copy_n(bm.a8.begin() + std::ptrdiff_t(row) * bm.w, bm.w, m_atlas.begin() + std::ptrdiff_t(m_penY + row) * kAtlas + m_penX);
        info.w = bm.w; info.h = bm.h; info.bearingX = bm.bearingX; info.bearingY = bm.bearingY;
        info.u0 = float(m_penX) / kAtlas; info.v0 = float(m_penY) / kAtlas;
        info.u1 = float(m_penX + bm.w) / kAtlas; info.v1 = float(m_penY + bm.h) / kAtlas;
        m_penX += bm.w + 1;
        m_rowH = std::max(m_rowH, bm.h);
        ++m_revision;
    }
    return m_glyphs.emplace(key, info).first->second;
}

namespace {
class BuiltinSource : public FontSource {
public:
    bool rasterize(std::uint32_t cp, bool bold, int px, GlyphBitmap& out) override {
        const ui::GlyphFace& f = face(bold, px);
        const ui::Glyph& g = f.glyphs[cp >= 32 && cp < 127 ? cp - 32 : '?' - 32];
        out.w = g.w; out.h = g.h; out.bearingX = g.xoff; out.bearingY = f.ascent + g.yoff;
        out.advance = float(g.advance64) / 64.0f;
        out.a8.assign(f.data + g.offset, f.data + g.offset + size_t(g.w) * size_t(g.h));
        return true;
    }
    float lineHeight(bool bold, int px) override { return float(face(bold, px).lineHeight + 1); }

private:
    static const ui::GlyphFace& face(bool bold, int px) { return bold ? (px <= 12 ? ui::gBold11 : ui::gBold13) : (px <= 12 ? ui::gRegular11 : ui::gRegular13); }
};
}  // namespace

std::unique_ptr<FontSource> makeBuiltinFontSource() { return std::make_unique<BuiltinSource>(); }

}  // namespace vkui
