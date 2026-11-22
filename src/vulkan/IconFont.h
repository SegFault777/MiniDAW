// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Vulkan UI: the icon style. "Classic" icons are drawn with draw-list shapes (Icons.h); the Material Symbols styles (Outlined, Rounded,
// Sharp - Google, Apache-2.0) come from the program itself (IconFontData.cpp: the icons that MiniDAW uses, default instance of the variable
// font). A font file in a "fonts" folder - MaterialSymbols<Style>*.ttf, e.g. Google's variable font as it is - replaces the built-in icons
// of its style; its glyphs are found by their names (the 'post' table), or through a *.codepoints file next to it (name -> code point).
// An icon that the font cannot draw falls back to the classic drawing, so the UI never loses a button.
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Text.h"
#include "TrueType.h"

namespace vkui {

enum class IconStyle { Classic = 0, Outlined = 1, Rounded = 2, Sharp = 3 };

/// The icons of the toolbar and panels. The index is also the low byte of the virtual code point.
enum class Icon {
    Play, Stop, Record, Rewind, Loop, Metronome, Pointer, Pencil, Scissors, Eraser, Mute, Browser, Inspector, Dock, Search, Follow, Plus, Minus, Fit,
    DropDown, ChevronRight, ExpandMore, Check, Close,   // small marks of the controls (drop-down arrow, sub-menu, section, check box, close button)
    Count
};

constexpr std::uint32_t kIconPlane = 0x100000;   // virtual code points of icon glyphs: kIconPlane + (style - 1) * 0x100 + icon

class IconTheme {
public:
    static IconTheme& instance();

    /// Folders searched for the font files (in this order). Changing them forgets what was loaded.
    void setFontDirs(std::vector<std::string> dirs);
    const std::vector<std::string>& fontDirs() const { return m_dirs; }

    /// The icons that are part of the program (IconFontData.cpp) are used when no font file is found. Off only in tests.
    void setBuiltInEnabled(bool on);
    bool builtInEnabled() const { return m_builtIn; }

    void setStyle(IconStyle s) { m_style = s; }
    IconStyle style() const { return m_style; }

    /// True when the fonts of 'style' were found and loaded (Classic is always available).
    bool available(IconStyle style);
    /// The font file that was used for 'style', "(built in)" for the icons of the program, or "" when there is none.
    std::string fontPath(IconStyle style);

    /// Virtual code point of an icon in the current style, 0 = draw it the classic way.
    std::uint32_t glyphFor(Icon icon);

    /// Draws the glyph of a virtual code point (used by the glyph cache). em square = px.
    bool rasterize(std::uint32_t virtualCodepoint, int px, GlyphBitmap& out);

    static const char* styleKey(IconStyle s);                 // "classic", "outlined", "rounded", "sharp"
    static IconStyle parseStyle(const std::string& key, IconStyle fallback = IconStyle::Outlined);
    static const char* styleName(IconStyle s);                // "Classic", "Material Symbols Outlined", ...
    /// The Material Symbols names that are tried for an icon, best first.
    static const std::vector<const char*>& candidates(Icon icon);

    /// Reads "name hex" lines (Google's .codepoints files).
    static std::map<std::string, std::uint32_t> parseCodepoints(const std::string& text);

private:
    struct Face {
        bool tried = false, ok = false;
        std::string path;
        TrueTypeFont font;
        int glyph[int(Icon::Count)] = {};   // glyph index per icon, 0 = the font has none
    };
    Face& face(IconStyle style);
    std::vector<std::string> m_dirs;
    IconStyle m_style = IconStyle::Outlined;
    bool m_builtIn = true;
    std::unique_ptr<Face> m_faces[4];
};

/// Wraps a font source: the virtual code points of icons go to the icon fonts, everything else to 'base'.
std::unique_ptr<FontSource> makeIconAwareSource(std::unique_ptr<FontSource> base);

}  // namespace vkui
