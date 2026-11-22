// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Vulkan UI: toolbar icons. icons::play etc. draw the icon of the chosen style (IconFont.h): a Material Symbols glyph, or the classic
// drawing made of draw-list shapes (icons::classic, no image files, scales with the display) when the style is Classic or the font cannot.
#include "Draw.h"
#include "IconFont.h"

namespace vkui {
namespace icons {
namespace classic {

using R = const Rect&;

inline void play(DrawList& d, R r, Color c) { const float s = std::min(r.w, r.h) * 0.36f; d.triangle({r.cx() - s * 0.6f, r.cy() - s}, {r.cx() - s * 0.6f, r.cy() + s}, {r.cx() + s * 0.9f, r.cy()}, c); }
inline void stop(DrawList& d, R r, Color c) { const float s = std::min(r.w, r.h) * 0.26f; d.rrect({r.cx() - s, r.cy() - s, 2 * s, 2 * s}, 2, c); }
inline void record(DrawList& d, R r, Color c) { d.circle(r.cx(), r.cy(), std::min(r.w, r.h) * 0.27f, c); }
inline void rewind(DrawList& d, R r, Color c) {
    const float s = std::min(r.w, r.h) * 0.24f;
    d.rect({r.cx() - s - 2, r.cy() - s, 2, 2 * s}, c);
    d.triangle({r.cx() + s, r.cy() - s}, {r.cx() + s, r.cy() + s}, {r.cx() - s + 1, r.cy()}, c);
}
inline void loop(DrawList& d, R r, Color c) {
    const float rad = std::min(r.w, r.h) * 0.24f;
    d.arc(r.cx(), r.cy(), rad, 0.4f, 5.9f, 1.8f, c);
    d.triangle({r.cx() + rad + 3, r.cy() - 1}, {r.cx() + rad - 3, r.cy() - 1}, {r.cx() + rad, r.cy() + 4}, c);
}
inline void metronome(DrawList& d, R r, Color c) {
    const float h = std::min(r.w, r.h) * 0.3f;
    d.line(r.cx() - h * 0.8f, r.cy() + h, r.cx() - h * 0.25f, r.cy() - h, 1.6f, c);
    d.line(r.cx() + h * 0.8f, r.cy() + h, r.cx() + h * 0.25f, r.cy() - h, 1.6f, c);
    d.line(r.cx() - h * 0.8f, r.cy() + h, r.cx() + h * 0.8f, r.cy() + h, 1.6f, c);
    d.line(r.cx(), r.cy() + h * 0.6f, r.cx() + h * 0.5f, r.cy() - h * 0.7f, 1.6f, c);
}
inline void pointer(DrawList& d, R r, Color c) {
    const float s = std::min(r.w, r.h) * 0.32f, x = r.cx() - s * 0.6f, y = r.cy() - s;
    d.triangle({x, y}, {x, y + 2 * s}, {x + s * 1.25f, y + s * 1.45f}, c);
}
inline void pencil(DrawList& d, R r, Color c) {
    const float s = std::min(r.w, r.h) * 0.3f;
    d.line(r.cx() - s, r.cy() + s, r.cx() + s, r.cy() - s, 3.f, c);
    d.triangle({r.cx() - s - 1, r.cy() + s + 1}, {r.cx() - s - 1, r.cy() + s * 0.4f}, {r.cx() - s * 0.4f, r.cy() + s + 1}, c);
}
inline void scissors(DrawList& d, R r, Color c) {
    const float s = std::min(r.w, r.h) * 0.3f;
    d.line(r.cx() - s, r.cy() - s, r.cx() + s, r.cy() + s * 0.5f, 1.5f, c);
    d.line(r.cx() + s, r.cy() - s, r.cx() - s, r.cy() + s * 0.5f, 1.5f, c);
    d.ring(r.cx() - s * 0.6f, r.cy() + s * 0.8f, s * 0.4f, 1.3f, c);
    d.ring(r.cx() + s * 0.6f, r.cy() + s * 0.8f, s * 0.4f, 1.3f, c);
}
inline void eraser(DrawList& d, R r, Color c) {
    const float s = std::min(r.w, r.h) * 0.3f;
    d.rrect({r.cx() - s, r.cy() - s * 0.55f, 2 * s, s * 1.1f}, 2, c);
    d.line(r.cx() - s * 0.2f, r.cy() - s * 0.55f, r.cx() - s * 0.2f, r.cy() + s * 0.55f, 1.2f, withAlpha(0xFF000000u, 120));
}
inline void mute(DrawList& d, R r, Color c) {
    const float s = std::min(r.w, r.h) * 0.24f;
    d.line(r.cx() - s, r.cy() - s, r.cx() + s, r.cy() + s, 2.f, c);
    d.line(r.cx() + s, r.cy() - s, r.cx() - s, r.cy() + s, 2.f, c);
}
inline void browser(DrawList& d, R r, Color c) {
    const float s = std::min(r.w, r.h) * 0.3f;
    d.rrectStroke({r.cx() - s, r.cy() - s, 2 * s, 2 * s}, 2, 1.4f, c);
    d.rect({r.cx() - s, r.cy() - s, s * 0.7f, 2 * s}, withAlpha(c, 160));
}
inline void inspector(DrawList& d, R r, Color c) {
    const float s = std::min(r.w, r.h) * 0.3f;
    d.rrectStroke({r.cx() - s, r.cy() - s, 2 * s, 2 * s}, 2, 1.4f, c);
    d.rect({r.cx() + s * 0.3f, r.cy() - s, s * 0.7f, 2 * s}, withAlpha(c, 160));
}
inline void dock(DrawList& d, R r, Color c) {
    const float s = std::min(r.w, r.h) * 0.3f;
    d.rrectStroke({r.cx() - s, r.cy() - s, 2 * s, 2 * s}, 2, 1.4f, c);
    d.rect({r.cx() - s, r.cy() + s * 0.2f, 2 * s, s * 0.8f}, withAlpha(c, 160));
}
inline void search(DrawList& d, R r, Color c) {
    const float s = std::min(r.w, r.h) * 0.22f;
    d.ring(r.cx() - s * 0.3f, r.cy() - s * 0.3f, s, 1.5f, c);
    d.line(r.cx() + s * 0.5f, r.cy() + s * 0.5f, r.cx() + s * 1.4f, r.cy() + s * 1.4f, 1.8f, c);
}
inline void follow(DrawList& d, R r, Color c) {
    const float s = std::min(r.w, r.h) * 0.28f;
    d.line(r.cx() - s, r.cy(), r.cx() + s * 0.6f, r.cy(), 1.6f, c);
    d.triangle({r.cx() + s * 0.5f, r.cy() - s * 0.7f}, {r.cx() + s * 0.5f, r.cy() + s * 0.7f}, {r.cx() + s * 1.2f, r.cy()}, c);
    d.line(r.cx() - s, r.cy() - s, r.cx() - s, r.cy() + s, 1.6f, c);
}
inline void plus(DrawList& d, R r, Color c) {
    const float s = std::min(r.w, r.h) * 0.25f;
    d.line(r.cx() - s, r.cy(), r.cx() + s, r.cy(), 1.8f, c);
    d.line(r.cx(), r.cy() - s, r.cx(), r.cy() + s, 1.8f, c);
}
inline void minus(DrawList& d, R r, Color c) { const float s = std::min(r.w, r.h) * 0.25f; d.line(r.cx() - s, r.cy(), r.cx() + s, r.cy(), 1.8f, c); }
inline void fit(DrawList& d, R r, Color c) {
    const float s = std::min(r.w, r.h) * 0.28f;
    d.line(r.cx() - s, r.cy(), r.cx() + s, r.cy(), 1.4f, c);
    d.triangle({r.cx() - s, r.cy() - 3}, {r.cx() - s, r.cy() + 3}, {r.cx() - s + 4, r.cy()}, c);
    d.triangle({r.cx() + s, r.cy() - 3}, {r.cx() + s, r.cy() + 3}, {r.cx() + s - 4, r.cy()}, c);
}

}  // namespace classic

using R = const Rect&;

/// The glyph of 'icon' in the current style, em square about 72 % of the button; false = draw it the classic way.
inline bool material(DrawList& d, R r, Color c, Icon icon) {
    const std::uint32_t cp = IconTheme::instance().glyphFor(icon);
    if (!cp) return false;
    d.glyph(r.cx(), r.cy(), std::min(r.w, r.h) * 0.72f, cp, c);
    return true;
}

/// A glyph of the current icon style centred on (cx, cy) with an em square of 'em' dp; false = the caller draws its classic shape.
inline bool glyph(DrawList& d, float cx, float cy, float em, Color c, Icon icon) {
    const std::uint32_t cp = IconTheme::instance().glyphFor(icon);
    if (!cp) return false;
    d.glyph(cx, cy, em, cp, c);
    return true;
}

#define MINIDAW_ICON(fn, ic) inline void fn(DrawList& d, R r, Color c) { if (!material(d, r, c, Icon::ic)) classic::fn(d, r, c); }
MINIDAW_ICON(play, Play) MINIDAW_ICON(stop, Stop) MINIDAW_ICON(record, Record) MINIDAW_ICON(rewind, Rewind) MINIDAW_ICON(loop, Loop)
MINIDAW_ICON(metronome, Metronome) MINIDAW_ICON(pointer, Pointer) MINIDAW_ICON(pencil, Pencil) MINIDAW_ICON(scissors, Scissors)
MINIDAW_ICON(eraser, Eraser) MINIDAW_ICON(mute, Mute) MINIDAW_ICON(browser, Browser) MINIDAW_ICON(inspector, Inspector) MINIDAW_ICON(dock, Dock)
MINIDAW_ICON(search, Search) MINIDAW_ICON(follow, Follow) MINIDAW_ICON(plus, Plus) MINIDAW_ICON(minus, Minus) MINIDAW_ICON(fit, Fit)
#undef MINIDAW_ICON

}  // namespace icons
}  // namespace vkui
