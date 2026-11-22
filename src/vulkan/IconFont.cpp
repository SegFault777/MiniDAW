// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "IconFont.h"

#include "IconFontData.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace vkui {

namespace fs = std::filesystem;

namespace {
std::string lower(std::string s) { for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c))); return s; }

bool readFileBytes(const fs::path& p, std::vector<std::uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return !out.empty();
}
}  // namespace

IconTheme& IconTheme::instance() { static IconTheme t; return t; }

void IconTheme::setBuiltInEnabled(bool on) {
    m_builtIn = on;
    for (auto& f : m_faces) f.reset();
}

void IconTheme::setFontDirs(std::vector<std::string> dirs) {
    m_dirs = std::move(dirs);
    for (auto& f : m_faces) f.reset();
}

const char* IconTheme::styleKey(IconStyle s) {
    switch (s) { case IconStyle::Classic: return "classic"; case IconStyle::Outlined: return "outlined"; case IconStyle::Rounded: return "rounded"; default: return "sharp"; }
}
const char* IconTheme::styleName(IconStyle s) {
    switch (s) { case IconStyle::Classic: return "Classic"; case IconStyle::Outlined: return "Material Symbols Outlined"; case IconStyle::Rounded: return "Material Symbols Rounded"; default: return "Material Symbols Sharp"; }
}
IconStyle IconTheme::parseStyle(const std::string& key, IconStyle fallback) {
    const std::string k = lower(key);
    if (k == "classic") return IconStyle::Classic;
    if (k == "outlined") return IconStyle::Outlined;
    if (k == "rounded") return IconStyle::Rounded;
    if (k == "sharp") return IconStyle::Sharp;
    return fallback;
}

const std::vector<const char*>& IconTheme::candidates(Icon icon) {
    static const std::vector<std::vector<const char*>> table = {
        /* Play */ {"play_arrow.fill", "play_arrow"}, /* Stop */ {"stop.fill", "stop"}, /* Record */ {"fiber_manual_record.fill", "fiber_manual_record", "radio_button_checked"},
        /* Rewind */ {"skip_previous.fill", "skip_previous", "fast_rewind"},
        /* Loop */ {"repeat", "loop", "sync"}, /* Metronome */ {"metronome", "music_note"}, /* Pointer */ {"arrow_selector_tool", "near_me", "navigation", "arrow_outward"},
        /* Pencil */ {"edit", "draw", "create"}, /* Scissors */ {"content_cut", "cut"}, /* Eraser */ {"ink_eraser", "backspace", "close"},
        /* Mute */ {"volume_off", "no_sound"}, /* Browser */ {"library_music", "left_panel_open", "folder_open", "view_sidebar"},
        /* Inspector */ {"right_panel_open", "info", "tune", "view_sidebar"}, /* Dock */ {"bottom_panel_open", "dock_to_bottom", "view_agenda"},
        /* Search */ {"search"}, /* Follow */ {"my_location", "filter_center_focus", "center_focus_strong", "vertical_align_center"},
        /* Plus */ {"add"}, /* Minus */ {"remove"}, /* Fit */ {"fit_screen", "zoom_out_map", "fullscreen"},
        /* DropDown */ {"arrow_drop_down", "keyboard_arrow_down", "expand_more"}, /* ChevronRight */ {"chevron_right", "keyboard_arrow_right"},
        /* ExpandMore */ {"expand_more", "keyboard_arrow_down"}, /* Check */ {"check", "done"}, /* Close */ {"close", "clear"}};
    return table[size_t(std::clamp(int(icon), 0, int(Icon::Count) - 1))];
}

std::map<std::string, std::uint32_t> IconTheme::parseCodepoints(const std::string& text) {
    std::map<std::string, std::uint32_t> out;
    std::istringstream is(text);
    std::string line;
    while (std::getline(is, line)) {
        std::istringstream ls(line);
        std::string name, hex;
        if (!(ls >> name >> hex)) continue;
        char* end = nullptr;
        const unsigned long v = std::strtoul(hex.c_str(), &end, 16);
        if (end == hex.c_str() || *end != '\0' || v == 0 || v > 0x10FFFF) continue;
        out.emplace(name, std::uint32_t(v));
    }
    return out;
}

IconTheme::Face& IconTheme::face(IconStyle style) {
    const int i = std::clamp(int(style), 0, 3);
    if (!m_faces[i]) m_faces[i] = std::make_unique<Face>();
    Face& f = *m_faces[i];
    if (f.tried || style == IconStyle::Classic) return f;
    f.tried = true;

    // glyph of every icon: through the names of a .codepoints file when there is one, else through the names of the font itself
    auto resolve = [&](TrueTypeFont& tt, const std::map<std::string, std::uint32_t>* codepoints) {
        int found = 0;
        for (int ic = 0; ic < int(Icon::Count); ++ic) {
            f.glyph[ic] = 0;
            for (const char* cand : candidates(Icon(ic))) {
                int g = 0;
                if (codepoints) { const auto it = codepoints->find(cand); if (it != codepoints->end()) g = tt.glyphIndex(it->second); }
                else g = tt.glyphByName(cand);
                if (g > 0) { f.glyph[ic] = g; ++found; break; }
            }
        }
        return found;
    };

    const std::string prefix = "materialsymbols" + lower(styleKey(style));
    for (const std::string& dir : m_dirs) {
        std::error_code ec;
        if (!fs::is_directory(fs::path(dir), ec)) continue;
        std::vector<fs::path> fonts, cps;
        for (fs::directory_iterator it(fs::path(dir), ec), end; !ec && it != end; it.increment(ec)) {
            const std::string n = lower(it->path().filename().string());
            if (n.rfind(prefix, 0) != 0) continue;
            if (n.size() > 4 && n.compare(n.size() - 4, 4, ".ttf") == 0) fonts.push_back(it->path());
            else if (n.size() > 11 && n.compare(n.size() - 11, 11, ".codepoints") == 0) cps.push_back(it->path());
        }
        std::sort(fonts.begin(), fonts.end());
        std::sort(cps.begin(), cps.end());
        if (fonts.empty()) continue;
        std::vector<std::uint8_t> bytes;
        if (!readFileBytes(fonts.front(), bytes)) continue;
        TrueTypeFont tt;
        if (!tt.load(std::move(bytes), nullptr)) continue;
        std::map<std::string, std::uint32_t> names;
        if (!cps.empty()) {   // the codepoints file with the same stem as the font, else the first one
            fs::path cpFile = cps.front();
            for (const auto& c : cps) if (c.stem().string() == fonts.front().stem().string()) { cpFile = c; break; }
            std::ifstream cf(cpFile);
            if (cf) names = parseCodepoints(std::string((std::istreambuf_iterator<char>(cf)), std::istreambuf_iterator<char>()));
        }
        if (resolve(tt, names.empty() ? nullptr : &names) == 0) continue;
        f.font = std::move(tt);
        f.path = fonts.front().string();
        f.ok = true;
        return f;
    }
    if (m_builtIn) {
        const EmbeddedIconFont& e = embeddedIconFont(int(style));
        if (e.data && e.size) {
            TrueTypeFont tt;
            if (tt.load(std::vector<std::uint8_t>(e.data, e.data + e.size), nullptr) && resolve(tt, nullptr) > 0) {
                f.font = std::move(tt);
                f.path = "(built in)";
                f.ok = true;
            }
        }
    }
    return f;
}

bool IconTheme::available(IconStyle style) { return style == IconStyle::Classic || face(style).ok; }

std::string IconTheme::fontPath(IconStyle style) { return style == IconStyle::Classic ? std::string() : face(style).path; }

std::uint32_t IconTheme::glyphFor(Icon icon) {
    if (m_style == IconStyle::Classic || int(icon) < 0 || int(icon) >= int(Icon::Count)) return 0;
    Face& f = face(m_style);
    if (!f.ok || f.glyph[int(icon)] <= 0) return 0;
    return kIconPlane + std::uint32_t(int(m_style) - 1) * 0x100u + std::uint32_t(icon);
}

bool IconTheme::rasterize(std::uint32_t cp, int px, GlyphBitmap& out) {
    if (cp < kIconPlane) return false;
    const std::uint32_t v = cp - kIconPlane;
    const int style = int(v >> 8) + 1, icon = int(v & 0xFF);
    if (style < 1 || style > 3 || icon >= int(Icon::Count)) return false;
    Face& f = face(IconStyle(style));
    if (!f.ok || f.glyph[icon] <= 0) return false;
    return f.font.rasterize(f.glyph[icon], px, out);
}

namespace {
class IconAwareSource : public FontSource {
public:
    explicit IconAwareSource(std::unique_ptr<FontSource> base) : m_base(std::move(base)) {}
    bool rasterize(std::uint32_t cp, bool bold, int px, GlyphBitmap& out) override {
        if (cp >= kIconPlane) return IconTheme::instance().rasterize(cp, px, out);
        return m_base && m_base->rasterize(cp, bold, px, out);
    }
    float lineHeight(bool bold, int px) override { return m_base ? m_base->lineHeight(bold, px) : float(px); }
private:
    std::unique_ptr<FontSource> m_base;
};
}  // namespace

std::unique_ptr<FontSource> makeIconAwareSource(std::unique_ptr<FontSource> base) { return std::make_unique<IconAwareSource>(std::move(base)); }

}  // namespace vkui
