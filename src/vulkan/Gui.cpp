// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Gui.h"
#include "Icons.h"
#include <cstdio>
#include "Text.h"
#include "Theme.h"

namespace vkui {

using namespace theme;

namespace {
constexpr float kPi = 3.14159265358979f;

size_t prevCp(const std::string& s, size_t i) {
    if (i == 0) return 0;
    --i;
    while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) --i;
    return i;
}
size_t nextCpPos(const std::string& s, size_t i) {
    if (i >= s.size()) return s.size();
    size_t j = i;
    nextCodepoint(s, j);
    return j;
}
std::string fmtNumber(double v, int decimals) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.*f", std::max(0, decimals), v);
    return buf;
}
}  // namespace

// ------------------------------------------------------------------------------------------------------------------ input

void Gui::onMouseMove(float x, float y) { m_mx = x; m_my = y; m_mouseIn = true; m_pendingInput = true; }

void Gui::onMouseButton(int b, bool down, double t) {
    if (b < 0 || b > 2) return;
    m_pendingInput = true;
    if (down) {
        m_down[b] = true;
        m_pressedEdge[b] = true;
        if (b == 0) {
            const double now = t >= 0 ? t : m_time;
            m_doubleEdge = now - m_lastClickTime < 0.4 && std::fabs(m_mx - m_lastClickX) < 5 && std::fabs(m_my - m_lastClickY) < 5;
            m_lastClickTime = m_doubleEdge ? -10 : now;
            m_lastClickX = m_mx;
            m_lastClickY = m_my;
        }
    } else {
        m_down[b] = false;
        m_releasedEdge[b] = true;
    }
}

void Gui::onWheel(float dx, float dy) { m_wheelX += dx; m_wheelY += dy; m_pendingInput = true; }

void Gui::onKey(int key, int mods, bool down, bool repeat) {
    m_mods = mods;
    if (down) { m_keys.push_back({key, mods, repeat}); m_pendingInput = true; }
}

void Gui::onText(const std::string& s) {
    for (char c : s) if ((unsigned char)c >= 32 && c != 127) m_text.push_back(c);
    m_pendingInput = true;
}

void Gui::beginFrame(DrawList& dl, double t) {
    m_dt = m_time > 0 ? float(std::clamp(t - m_time, 0.001, 0.1)) : 0.016f;
    m_time = t;
    m_dl = &dl;
    m_cursor = Cursor::Arrow;
    m_animating = false;
    m_inputEnabled = !m_menuOpen;
    m_hot = 0;
    m_tipText.clear();
    m_tipId = 0;
    m_pressClaimed = false;
}

bool Gui::key(int k, int mods, bool allowRepeat, bool evenInText) {
    if (!m_inputEnabled || (m_textFocus && !evenInText)) return false;
    for (size_t i = 0; i < m_keys.size(); ++i)
        if (m_keys[i].key == k && m_keys[i].mods == mods && (allowRepeat || !m_keys[i].repeat)) { m_keys.erase(m_keys.begin() + std::ptrdiff_t(i)); return true; }
    return false;
}

bool Gui::keyAny(int k, bool allowRepeat) {
    if (!m_inputEnabled || m_textFocus) return false;
    for (size_t i = 0; i < m_keys.size(); ++i)
        if (m_keys[i].key == k && (allowRepeat || !m_keys[i].repeat)) { m_keys.erase(m_keys.begin() + std::ptrdiff_t(i)); return true; }
    return false;
}

Id Gui::id(const std::string& name) const {
    Id h = m_idStack.empty() ? 2166136261u : m_idStack.back();
    for (unsigned char c : name) h = (h ^ c) * 16777619u;
    return h ? h : 1u;
}
Id Gui::id(const std::string& name, int n) const { return id(name + "#" + std::to_string(n)); }

float Gui::anim(Id i, float target, float speed) {
    auto it = m_anim.find(i);
    if (it == m_anim.end()) it = m_anim.emplace(i, target).first;
    float& v = it->second;
    v += (target - v) * std::min(1.f, m_dt * speed);
    if (std::fabs(target - v) < 0.003f) v = target;
    else m_animating = true;
    return v;
}

bool Gui::hovering(const Rect& r) const { return m_inputEnabled && m_mouseIn && r.contains(m_mx, m_my) && m_dl->clip().contains(m_mx, m_my); }

Interact Gui::interact(Id i, const Rect& r, bool allowDrag) {
    Interact it;
    it.mx = m_mx; it.my = m_my;
    const bool inside = hovering(r);
    it.hovered = inside && (m_active == 0 || m_active == i);
    if (it.hovered) m_hot = i;
    if (it.hovered && m_pressedEdge[1]) { it.rightClicked = true; m_pressedEdge[1] = false; m_pressClaimed = true; }
    if (it.hovered && m_pressedEdge[0] && m_active == 0) {
        m_active = i;
        m_focus = i;
        m_startX = m_mx; m_startY = m_my;
        it.pressed = true;
        it.doubleClicked = m_doubleEdge;
        m_pressedEdge[0] = false;
        m_pressClaimed = true;
    }
    if (m_active == i) {
        it.held = m_down[0];
        it.startX = m_startX; it.startY = m_startY;
        if (allowDrag) { it.dx = m_mx - m_pmx; it.dy = m_my - m_pmy; }
        if (m_releasedEdge[0] || !m_down[0]) {
            it.released = true;
            it.clicked = r.contains(m_mx, m_my);
            m_active = 0;
            m_releasedEdge[0] = false;
        }
    }
    return it;
}

void Gui::tip(Id i, const std::string& text) { if (m_hot == i) { m_tipText = text; m_tipId = i; } }

// --------------------------------------------------------------------------------------------------------------- widgets

bool Gui::button(Id i, const Rect& r, const std::string& label, ButtonKind kind, bool enabled) {
    const Interact it = enabled ? interact(i, r, false) : Interact{};
    const float h = anim(i, it.hovered ? 1.f : 0.f), p = anim(i ^ 0x9e3779b9u, it.held ? 1.f : 0.f, 30.f);
    Color bg, fg = enabled ? text : textMute;
    switch (kind) {
    case ButtonKind::Accent: bg = mix(mix(accent, lighten(accent, 0.18f), h), darken(accent, 0.2f), p); fg = rgb(255, 255, 255); break;
    case ButtonKind::Danger: bg = mix(mix(rgb(0x8a, 0x2b, 0x34), record, h), darken(record, 0.3f), p); fg = rgb(255, 255, 255); break;
    case ButtonKind::Flat: bg = mix(mix(withAlpha(bg3, 0), bg3, h), bg4, p); break;
    default: bg = mix(mix(bg2, bg3, h), bg4, p); break;
    }
    m_dl->rrect(r, radius, bg);
    if (kind == ButtonKind::Normal) m_dl->rrectStroke(r, radius, 1, mix(border, accent, h * 0.6f));
    // the flat "x" buttons (remove an effect, a device card, a snapshot) show the close icon of the chosen icon style
    if (!(kind == ButtonKind::Flat && label == "x" && icons::glyph(*m_dl, r.cx(), r.cy(), std::min(r.w, r.h) * 0.62f, fg, Icon::Close)))
        m_dl->textIn(r.inset(4, 0), tr(label), fg, normal, Align::Center);
    if (it.hovered) setCursor(Cursor::Hand);
    return enabled && it.clicked;
}

bool Gui::iconButton(Id i, const Rect& r, const IconFn& icon, const std::string& tooltip, bool on, Color onColor, bool enabled) {
    const Interact it = enabled ? interact(i, r, false) : Interact{};
    const float h = anim(i, it.hovered ? 1.f : 0.f), p = anim(i ^ 0x9e3779b9u, it.held ? 1.f : 0.f, 30.f);
    const Color oc = onColor ? onColor : accent;
    Color bg = mix(mix(bg2, bg3, h), bg4, p);
    if (on) bg = mix(mix(withAlpha(oc, 255), bg2, 0.62f), lighten(oc, 0.1f), h * 0.35f);
    m_dl->rrect(r, radius, bg);
    m_dl->rrectStroke(r, radius, 1, on ? oc : mix(border, accent, h * 0.5f));
    icon(*m_dl, r, !enabled ? textMute : on ? rgb(255, 255, 255) : mix(textDim, text, h));
    if (it.hovered) { setCursor(Cursor::Hand); tip(i, tooltip); }
    return enabled && it.clicked;
}

bool Gui::toggle(Id i, const Rect& r, const std::string& label, bool& value, Color onColor) {
    const Interact it = interact(i, r, false);
    const float h = anim(i, it.hovered ? 1.f : 0.f), on = anim(i ^ 0x51ed270bu, value ? 1.f : 0.f, 22.f);
    const Color oc = onColor ? onColor : accent;
    const Color bg = mix(mix(bg2, bg3, h), mix(bg2, oc, 0.55f), on);
    m_dl->rrect(r, radius, bg);
    m_dl->rrectStroke(r, radius, 1, mix(mix(border, accent, h * 0.5f), oc, on));
    m_dl->textIn(r.inset(4, 0), tr(label), mix(textDim, rgb(255, 255, 255), std::max(on, h * 0.6f)), normal, Align::Center);
    if (it.hovered) setCursor(Cursor::Hand);
    if (it.clicked) { value = !value; return true; }
    return false;
}

bool Gui::checkbox(Id i, const Rect& r, const std::string& label, bool& value) {
    const Interact it = interact(i, r, false);
    const float h = anim(i, it.hovered ? 1.f : 0.f), on = anim(i ^ 0x51ed270bu, value ? 1.f : 0.f, 22.f);
    const Rect box{r.x, r.cy() - 7, 14, 14};
    m_dl->rrect(box, 3, mix(mix(bg2, bg3, h), accent, on));
    m_dl->rrectStroke(box, 3, 1, mix(border, accent, std::max(on, h)));
    if (on > 0.05f && !icons::glyph(*m_dl, box.cx(), box.cy(), 19, scaleAlpha(rgb(255, 255, 255), on), Icon::Check)) {
        m_dl->line(box.x + 3, box.y + 7.5f, box.x + 6, box.y + 10.5f, 1.8f, scaleAlpha(rgb(255, 255, 255), on));
        m_dl->line(box.x + 6, box.y + 10.5f, box.x + 11, box.y + 3.5f, 1.8f, scaleAlpha(rgb(255, 255, 255), on));
    }
    m_dl->textIn({r.x + 22, r.y, r.w - 22, r.h}, tr(label), text, normal);
    if (it.hovered) setCursor(Cursor::Hand);
    if (it.clicked) { value = !value; return true; }
    return false;
}

bool Gui::slider(Id i, const Rect& r, float& value, float lo, float hi, float def, const std::string& valueText) {
    const Interact it = interact(i, r);
    bool changed = false;
    const float span = std::max(1e-6f, hi - lo);
    Rect track = r.inset(7, 0);
    if (!valueText.empty()) track.w -= 44;
    if (it.held && !it.doubleClicked) {
        const float nv = std::clamp(lo + (m_mx - track.x) / std::max(1.f, track.w) * span, lo, hi);
        if (nv != value) { value = nv; changed = true; }
    }
    if (it.doubleClicked && value != def) { value = def; changed = true; }
    if (it.hovered && wheelY() != 0) { value = std::clamp(value + wheelY() * span * 0.02f, lo, hi); consumeWheel(); changed = true; }
    const float t = std::clamp((value - lo) / span, 0.f, 1.f), h = anim(i, (it.hovered || it.held) ? 1.f : 0.f);
    const Rect bar{track.x, r.cy() - 2, track.w, 4};
    m_dl->rrect(bar, 2, bg0);
    m_dl->rrect({bar.x, bar.y, bar.w * t, 4}, 2, accent);
    m_dl->circle(track.x + track.w * t, r.cy(), 5.5f + h, mix(rgb(0xd8, 0xdf, 0xee), rgb(255, 255, 255), h));
    if (!valueText.empty()) m_dl->textIn({r.right() - 42, r.y, 42, r.h}, valueText, textDim, small, Align::Right);
    if (it.hovered) setCursor(Cursor::Hand);
    return changed;
}

bool Gui::knob(Id i, const Rect& r, float& value, float lo, float hi, float def, const std::string& label, const std::string& valueText, Color color) {
    const Interact it = interact(i, r);
    bool changed = false;
    const float span = std::max(1e-6f, hi - lo);
    const float step = span / (shift() ? 600.f : 150.f);
    if (it.held && !it.doubleClicked && it.dy != 0) { value = std::clamp(value - it.dy * step, lo, hi); changed = true; }
    if (it.doubleClicked && value != def) { value = def; changed = true; }
    if (it.hovered && wheelY() != 0) { value = std::clamp(value + wheelY() * span * 0.03f, lo, hi); consumeWheel(); changed = true; }
    const float d = std::min(r.w, label.empty() && valueText.empty() ? r.h : r.h - 12.f);
    const float cx = r.cx(), cy = r.y + d * 0.5f, rad = d * 0.5f - 1.5f;
    const float h = anim(i, (it.hovered || it.held) ? 1.f : 0.f);
    const float t = std::clamp((value - lo) / span, 0.f, 1.f), a0 = 0.75f * kPi, a1 = 2.25f * kPi;
    const float defT = std::clamp((def - lo) / span, 0.f, 1.f);
    const Color c = color ? color : accent;
    m_dl->circle(cx, cy, rad, mix(bg2, bg3, h));
    m_dl->ring(cx, cy, rad, 1, border);
    m_dl->arc(cx, cy, rad - 3, a0, a1, 2.5f, bg0);
    const float from = a0 + (a1 - a0) * defT, to = a0 + (a1 - a0) * t;
    if (std::fabs(to - from) > 0.01f) m_dl->arc(cx, cy, rad - 3, std::min(from, to), std::max(from, to), 2.5f, mix(c, lighten(c, 0.3f), h));
    const float ang = to;
    m_dl->line(cx + std::cos(ang) * (rad * 0.25f), cy + std::sin(ang) * (rad * 0.25f), cx + std::cos(ang) * (rad - 4), cy + std::sin(ang) * (rad - 4), 2.f, rgb(0xf2, 0xf5, 0xfb));
    if (!label.empty() || !valueText.empty())
        m_dl->textIn({r.x - 4, r.y + d, r.w + 8, 12}, (it.hovered || it.held) && !valueText.empty() ? valueText : (label.empty() ? valueText : label), (it.hovered || it.held) ? text : textDim, small, Align::Center);
    if (it.hovered) setCursor(Cursor::ResizeV);
    return changed;
}

bool Gui::fader(Id i, const Rect& r, float& value, float lo, float hi, float def, Color color) {
    const Interact it = interact(i, r);
    bool changed = false;
    const float span = std::max(1e-6f, hi - lo);
    const Rect track{r.cx() - 2, r.y + 8, 4, std::max(1.f, r.h - 16)};
    if (it.held && !it.doubleClicked) {
        const float nv = std::clamp(lo + (1.f - (m_my - track.y) / track.h) * span, lo, hi);
        if (nv != value) { value = nv; changed = true; }
    }
    if (it.doubleClicked && value != def) { value = def; changed = true; }
    if (it.hovered && wheelY() != 0) { value = std::clamp(value + wheelY() * span * 0.02f, lo, hi); consumeWheel(); changed = true; }
    const float t = std::clamp((value - lo) / span, 0.f, 1.f), h = anim(i, (it.hovered || it.held) ? 1.f : 0.f);
    const float dt = std::clamp((def - lo) / span, 0.f, 1.f);
    m_dl->rrect(track, 2, bg0);
    m_dl->rrect({track.x, track.bottom() - track.h * t, 4, track.h * t}, 2, mix(color ? color : accentDim, accent, 0.4f));
    const float ty = track.bottom() - track.h * dt;   // the 0 dB tick
    m_dl->hline(track.x - 6, track.x - 2, ty, textMute);
    m_dl->hline(track.right() + 2, track.right() + 6, ty, textMute);
    const Rect cap{r.cx() - 11, track.bottom() - track.h * t - 6, 22, 12};
    m_dl->shadow(cap, 3, 3, withAlpha(rgb(0, 0, 0), 120));
    m_dl->rrectGradient(cap, 3, mix(rgb(0x8a, 0x93, 0xa6), rgb(0xc8, 0xd0, 0xe0), h), mix(rgb(0x58, 0x60, 0x72), rgb(0x8a, 0x93, 0xa6), h));
    m_dl->hline(cap.x + 3, cap.right() - 3, cap.cy(), rgb(0x20, 0x24, 0x2c));
    if (it.hovered) setCursor(Cursor::ResizeV);
    return changed;
}

void Gui::applyCaretFromMouse(EditState& e, const std::string& s, const Rect& r, float mx) {
    float x = r.x + 6;
    size_t best = 0;
    float bestD = 1e9f;
    for (size_t p = 0;; p = nextCpPos(s, p)) {
        const float w = m_dl->textWidth(s.substr(0, p), normal);
        const float d = std::fabs(x + w - mx);
        if (d < bestD) { bestD = d; best = p; }
        if (p >= s.size()) break;
    }
    e.caret = int(best);
}

bool Gui::textField(Id i, const Rect& r, std::string& s, const std::string& hint, bool* changed, bool password) {
    EditState& e = m_edit[i];
    const Interact it = interact(i, r);
    const bool focused = m_focus == i;
    bool submitted = false;
    if (changed) *changed = false;
    e.caret = std::clamp(e.caret, 0, int(s.size()));
    e.sel = std::clamp(e.sel, 0, int(s.size()));
    if (it.pressed) {
        if (it.doubleClicked) { e.sel = 0; e.caret = int(s.size()); }
        else { applyCaretFromMouse(e, s, r, m_mx); e.sel = e.caret; }
    } else if (it.held) {
        applyCaretFromMouse(e, s, r, m_mx);
    }
    if (focused) {
        m_textFocusSeen = true;
        m_focusIsText = true;
        if (e.fresh) { e.original = s; e.fresh = false; }
        auto eraseSel = [&] {
            const int a = std::min(e.caret, e.sel), b = std::max(e.caret, e.sel);
            if (a != b) { s.erase(size_t(a), size_t(b - a)); e.caret = e.sel = a; if (changed) *changed = true; return true; }
            return false;
        };
        for (size_t k = 0; k < m_keys.size();) {
            const KeyEvent ev = m_keys[k];
            bool used = true;
            const bool shiftHeld = ev.mods & ModShift, ctrlHeld = ev.mods & ModCtrl;
            if (ev.key == KeyLeft) { e.caret = int(ctrlHeld ? 0 : prevCp(s, size_t(e.caret))); if (!shiftHeld) e.sel = e.caret; }
            else if (ev.key == KeyRight) { e.caret = int(ctrlHeld ? s.size() : nextCpPos(s, size_t(e.caret))); if (!shiftHeld) e.sel = e.caret; }
            else if (ev.key == KeyHome) { e.caret = 0; if (!shiftHeld) e.sel = 0; }
            else if (ev.key == KeyEnd) { e.caret = int(s.size()); if (!shiftHeld) e.sel = e.caret; }
            else if (ev.key == KeyBackspace) { if (!eraseSel() && e.caret > 0) { const size_t p = prevCp(s, size_t(e.caret)); s.erase(p, size_t(e.caret) - p); e.caret = e.sel = int(p); if (changed) *changed = true; } }
            else if (ev.key == KeyDelete) { if (!eraseSel() && size_t(e.caret) < s.size()) { s.erase(size_t(e.caret), nextCpPos(s, size_t(e.caret)) - size_t(e.caret)); if (changed) *changed = true; } }
            else if (ev.key == KeyEnter) {
                if (!m_text.empty()) { eraseSel(); s.insert(size_t(e.caret), m_text); e.caret += int(m_text.size()); e.sel = e.caret; m_text.clear(); if (changed) *changed = true; }   // the last IME syllable arrives together with Enter
                submitted = true; e.fresh = true; m_focus = 0;
            }
            else if (ev.key == KeyEscape) { s = e.original; e.fresh = true; m_focus = 0; if (changed) *changed = true; }
            else if (ctrlHeld && ev.key == 'A') { e.sel = 0; e.caret = int(s.size()); }
            else if (ctrlHeld && (ev.key == 'C' || ev.key == 'X')) {
                const int a = std::min(e.caret, e.sel), b = std::max(e.caret, e.sel);
                if (a != b && clipboardSet) clipboardSet(s.substr(size_t(a), size_t(b - a)));
                if (ev.key == 'X') eraseSel();
            }
            else if (ctrlHeld && ev.key == 'V') { if (clipboardGet) { eraseSel(); std::string c = clipboardGet(); for (char& ch : c) if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' '; s.insert(size_t(e.caret), c); e.caret += int(c.size()); e.sel = e.caret; if (changed) *changed = true; } }
            else used = false;
            if (used) m_keys.erase(m_keys.begin() + std::ptrdiff_t(k)); else ++k;
        }
        if (!m_text.empty() && m_focus == i) { eraseSel(); s.insert(size_t(e.caret), m_text); e.caret += int(m_text.size()); e.sel = e.caret; m_text.clear(); if (changed) *changed = true; }
        m_animating = true;   // the caret blinks
    } else {
        e.fresh = true;
    }
    const float f = anim(i, focused ? 1.f : (it.hovered ? 0.4f : 0.f));
    m_dl->rrect(r, radius, bg0);
    m_dl->rrectStroke(r, radius, 1, mix(border, accent, f));
    m_dl->pushClip(r.inset(5, 1));
    const std::string shown = password ? std::string(s.size() / 1, '*') : s;
    const std::string pre = focused && !password ? m_preedit : std::string();   // text being composed by the IME, shown at the caret
    const float tw = m_dl->textWidth(shown.substr(0, size_t(e.caret)), normal);
    const float pw = pre.empty() ? 0.f : m_dl->textWidth(pre, normal);
    const float shiftX = focused ? std::max(0.f, tw + pw - (r.w - 14)) : 0.f;
    if (focused) {
        m_caretSeen = true;
        m_caretX = r.x + 6 + tw + pw - shiftX; m_caretY = r.y + 4; m_caretH = r.h - 8;
    }
    if (s.empty() && !focused) m_dl->textIn({r.x + 6, r.y, r.w - 12, r.h}, tr(hint), textMute, normal);
    else if (!pre.empty()) {
        const float ty = r.y + (r.h - m_dl->lineHeight(normal)) * 0.5f;
        m_dl->text(r.x + 6 - shiftX, ty, shown.substr(0, size_t(e.caret)), text, normal);
        m_dl->text(r.x + 6 + tw - shiftX, ty, pre, accent, normal);
        m_dl->rect({r.x + 6 + tw - shiftX, r.y + r.h - 6, pw, 1}, accent);   // underline: not committed yet
        m_dl->text(r.x + 6 + tw + pw - shiftX, ty, shown.substr(size_t(e.caret)), text, normal);
    }
    else {
        if (focused && e.caret != e.sel) {
            const float a = m_dl->textWidth(shown.substr(0, size_t(std::min(e.caret, e.sel))), normal), b = m_dl->textWidth(shown.substr(0, size_t(std::max(e.caret, e.sel))), normal);
            m_dl->rect({r.x + 6 + a - shiftX, r.y + 4, b - a, r.h - 8}, withAlpha(accent, 120));
        }
        m_dl->text(r.x + 6 - shiftX, r.y + (r.h - m_dl->lineHeight(normal)) * 0.5f, shown, text, normal);
    }
    if (focused && std::fmod(m_time, 1.0) < 0.6) m_dl->rect({r.x + 6 + tw + pw - shiftX, r.y + 4, 1, r.h - 8}, text);
    m_dl->popClip();
    if (it.hovered) setCursor(Cursor::IBeam);
    return submitted;
}

bool Gui::numberField(Id i, const Rect& r, double& value, double lo, double hi, double step, int decimals, const std::string& suffix, double def) {
    bool changed = false;
    if (m_typingNumber == i) {
        std::string& s = m_numText[i];
        if (m_focus != i) m_focus = i;
        if (textField(i, r, s)) {
            char* end = nullptr;
            const double v = std::strtod(s.c_str(), &end);
            if (end != s.c_str()) { value = std::clamp(v, lo, hi); changed = true; }
            m_typingNumber = 0;
        } else if (m_focus != i) {
            m_typingNumber = 0;
        }
        return changed;
    }
    const Interact it = interact(i, r);
    float& acc = m_anim[i ^ 0x7f4a7c15u];
    if (it.held && !it.doubleClicked && it.dy != 0) {
        acc -= it.dy / 4.f;   // four pixels per step
        const double unit = step * (shift() ? 0.1 : ctrl() ? 10.0 : 1.0);
        const int whole = int(acc);
        if (whole != 0) { acc -= float(whole); value = std::clamp(value + whole * unit, lo, hi); changed = true; }
    }
    if (it.hovered && wheelY() != 0) { value = std::clamp(value + wheelY() * step, lo, hi); consumeWheel(); changed = true; }
    if (it.doubleClicked) {
        if (ctrl() || value == def || def == 0) { m_typingNumber = i; m_numText[i] = fmtNumber(value, decimals); m_focus = i; m_edit[i].sel = 0; m_edit[i].caret = int(m_numText[i].size()); m_edit[i].fresh = true; }
        else { value = def; changed = true; }
    }
    const float h = anim(i, (it.hovered || it.held) ? 1.f : 0.f);
    m_dl->rrect(r, 4, mix(lcdBg, rgb(0x14, 0x1c, 0x26), h));
    m_dl->rrectStroke(r, 4, 1, mix(rgb(0x22, 0x2c, 0x3a), accent, h * 0.7f));
    m_dl->textIn(r.inset(4, 0), fmtNumber(value, decimals) + suffix, lcdText, bold, Align::Center);
    if (it.hovered) setCursor(Cursor::ResizeV);
    return changed;
}

bool Gui::dropdown(Id i, const Rect& r, int& index, const std::vector<std::string>& options) {
    bool changed = false;
    auto pick = m_picked.find(i);
    if (pick != m_picked.end()) { index = pick->second; m_picked.erase(pick); changed = true; }
    const Interact it = interact(i, r, false);
    const float h = anim(i, it.hovered ? 1.f : 0.f);
    m_dl->rrect(r, radius, mix(bg2, bg3, h));
    m_dl->rrectStroke(r, radius, 1, mix(border, accent, h * 0.6f));
    m_dl->textIn({r.x + 8, r.y, r.w - 24, r.h}, index >= 0 && index < int(options.size()) ? tr(options[size_t(index)]) : std::string(), text, normal);
    if (!icons::glyph(*m_dl, r.right() - 13, r.cy(), 20, textDim, Icon::DropDown))
        m_dl->triangle({r.right() - 15, r.cy() - 2}, {r.right() - 7, r.cy() - 2}, {r.right() - 11, r.cy() + 3}, textDim);
    if (it.hovered) setCursor(Cursor::Hand);
    if (it.clicked) {
        std::vector<MenuItem> items;
        for (size_t k = 0; k < options.size(); ++k) {
            MenuItem m;
            m.label = options[k];
            m.checked = int(k) == index;
            m.action = [this, i, k] { m_picked[i] = int(k); m_animating = true; m_pendingInput = true; };
            items.push_back(std::move(m));
        }
        openMenu(r.x, r.bottom() + 2, std::move(items));
    }
    return changed;
}

void Gui::scrollBar(Id i, const Rect& r, float& offset, float content, float view, bool vertical) {
    if (content <= view + 0.5f) { offset = 0; return; }
    const float len = vertical ? r.h : r.w, maxOff = content - view;
    offset = std::clamp(offset, 0.f, maxOff);
    const float thumbLen = std::max(24.f, len * view / content), travel = std::max(1.f, len - thumbLen);
    const float pos = travel * offset / maxOff;
    const Rect thumb = vertical ? Rect{r.x + 1, r.y + pos, r.w - 2, thumbLen} : Rect{r.x + pos, r.y + 1, thumbLen, r.h - 2};
    const Interact it = interact(i, thumb);
    if (it.held) offset = std::clamp(offset + (vertical ? it.dy : it.dx) / travel * maxOff, 0.f, maxOff);
    const Interact tr2 = interact(i ^ 0x2545f491u, r, false);
    if (tr2.pressed) {
        const float p = ((vertical ? m_my - r.y : m_mx - r.x) - thumbLen * 0.5f) / travel;
        offset = std::clamp(p * maxOff, 0.f, maxOff);
    }
    const float h = anim(i, (it.hovered || it.held) ? 1.f : 0.f);
    m_dl->rrect(thumb, 3, mix(withAlpha(textMute, 150), textDim, h));
}

void Gui::separator(const Rect& r, bool vertical) {
    if (vertical) m_dl->vline(r.cx(), r.y, r.bottom(), border); else m_dl->hline(r.x, r.right(), r.cy(), border);
}

// ---------------------------------------------------------------------------------------------------------------- menus

constexpr float kMenuRow = 24.f, kMenuSep = 9.f, kMenuPad = 4.f;

Rect Gui::placeMenu(Menu& m, float x, float y, bool submenu, float parentRight) {
    float w = 150, h = kMenuPad * 2;
    for (const MenuItem& it : m.items) {
        if (it.separator) { h += kMenuSep; continue; }
        h += kMenuRow;
        float iw = m_dl->textWidth(tr(it.label), normal) + 52;
        if (!it.shortcut.empty()) iw += m_dl->textWidth(it.shortcut, small) + 24;
        if (!it.children.empty()) iw += 14;
        w = std::max(w, iw);
    }
    const float maxH = m_dl->height() - 8;
    const float vis = std::min(h, maxH);
    if (submenu && x + w > m_dl->width() - 4) x = std::max(4.f, parentRight - w - 6);   // flip to the left of the parent
    x = std::clamp(x, 4.f, std::max(4.f, m_dl->width() - w - 4));
    y = std::clamp(y, 4.f, std::max(4.f, m_dl->height() - vis - 4));
    return {x, y, w, vis};
}

void Gui::openMenu(float x, float y, std::vector<MenuItem> items) {
    m_menus.clear();
    Menu m;
    m.items = std::move(items);
    m.rect = placeMenu(m, x, y, false, 0);
    m_menus.push_back(std::move(m));
    m_menuOpen = true;
    m_menuOpenedAt = float(m_time);
    m_animating = true;
}

void Gui::closeMenus() { m_menus.clear(); m_menuOpen = false; m_barOpen = -1; m_pendingInput = true; }

void Gui::drawMenus() {
    if (!m_menuOpen || m_menus.empty()) return;
    DrawList& dl = *m_dl;
    // keyboard
    auto enabledAt = [&](const Menu& m, int from, int dir) {
        const int n = int(m.items.size());
        for (int k = 1; k <= n; ++k) {
            const int j = ((from + dir * k) % n + n) % n;
            if (!m.items[size_t(j)].separator && m.items[size_t(j)].enabled) return j;
        }
        return from;
    };
    std::function<void()> chosen;
    for (size_t k = 0; k < m_keys.size();) {
        const int key = m_keys[k].key;
        Menu& top = m_menus.back();
        bool used = true;
        if (key == KeyEscape) { if (m_menus.size() > 1) m_menus.pop_back(); else closeMenus(); }
        else if (key == KeyDown) top.hover = enabledAt(top, top.hover < 0 ? -1 : top.hover, 1);
        else if (key == KeyUp) top.hover = enabledAt(top, top.hover < 0 ? 0 : top.hover, -1);
        else if (key == KeyRight && top.hover >= 0 && !top.items[size_t(top.hover)].children.empty()) {
            Menu sub;
            sub.items = top.items[size_t(top.hover)].children;
            sub.rect = placeMenu(sub, top.rect.right() - 2, top.rect.y + kMenuPad, true, top.rect.x);
            sub.hover = enabledAt(sub, -1, 1);
            m_menus.push_back(std::move(sub));
        }
        else if (key == KeyLeft && m_menus.size() > 1) m_menus.pop_back();
        else if (key == KeyEnter && top.hover >= 0) {
            MenuItem& it = top.items[size_t(top.hover)];
            if (it.enabled && it.children.empty()) { chosen = it.action; closeMenus(); }
        }
        else used = false;
        if (used) m_keys.erase(m_keys.begin() + std::ptrdiff_t(k)); else ++k;
        if (!m_menuOpen) break;
    }
    if (m_menuOpen) {
        // wheel scrolling of tall menus
        for (Menu& m : m_menus) {
            float content = kMenuPad * 2;
            for (const MenuItem& it : m.items) content += it.separator ? kMenuSep : kMenuRow;
            if (content > m.rect.h && m.rect.contains(m_mx, m_my) && m_wheelY != 0) { m.scroll = std::clamp(m.scroll - m_wheelY * 40.f, 0.f, content - m.rect.h); m_wheelY = 0; }
        }
        bool insideAny = false;
        for (size_t lv = 0; lv < m_menus.size(); ++lv) {
            Menu& m = m_menus[lv];
            if (m.rect.contains(m_mx, m_my)) insideAny = true;
            dl.shadow(m.rect, 7, 10, withAlpha(rgb(0, 0, 0), 150));
            dl.rrect(m.rect, 7, bg1);
            dl.rrectStroke(m.rect, 7, 1, border);
            dl.pushClip(m.rect);
            float y = m.rect.y + kMenuPad - m.scroll;
            for (size_t k = 0; k < m.items.size(); ++k) {
                MenuItem& it = m.items[k];
                if (it.separator) { dl.hline(m.rect.x + 8, m.rect.right() - 8, y + kMenuSep * 0.5f, border); y += kMenuSep; continue; }
                const Rect row{m.rect.x + 4, y, m.rect.w - 8, kMenuRow};
                if (row.contains(m_mx, m_my) && m.rect.contains(m_mx, m_my) && (m_mouseIn)) {
                    if (m.hover != int(k)) {
                        m.hover = int(k);
                        if (m_menus.size() > lv + 1) m_menus.resize(lv + 1);
                        if (!it.children.empty() && it.enabled) {
                            Menu sub;
                            sub.items = it.children;
                            sub.rect = placeMenu(sub, m.rect.right() - 2, row.y - kMenuPad, true, m.rect.x);
                            m_menus.push_back(std::move(sub));
                            dl.popClip();
                            return;   // the vectors changed: finish this frame next time
                        }
                    }
                }
                const bool hot = m.hover == int(k) && (lv + 1 == m_menus.size() || !it.children.empty());
                if (hot && it.enabled) dl.rrect(row, 4, accentDim);
                const Color fg = it.enabled ? text : textMute;
                if (it.checked) { dl.line(row.x + 8, row.cy(), row.x + 11, row.cy() + 3.5f, 1.8f, accent); dl.line(row.x + 11, row.cy() + 3.5f, row.x + 17, row.cy() - 4, 1.8f, accent); }
                dl.textIn({row.x + 26, row.y, row.w - 40, row.h}, tr(it.label), fg, normal);
                if (!it.shortcut.empty()) dl.textIn({row.x + 26, row.y, row.w - 34, row.h}, it.shortcut, textMute, small, Align::Right);
                if (!it.children.empty() && !icons::glyph(dl, row.right() - 11, row.cy(), 18, fg, Icon::ChevronRight))
                    dl.triangle({row.right() - 12, row.cy() - 4}, {row.right() - 12, row.cy() + 4}, {row.right() - 7, row.cy()}, fg);
                if (m_pressedEdge[0] && row.contains(m_mx, m_my) && m.rect.contains(m_mx, m_my) && it.enabled && it.children.empty()) { chosen = it.action; m_pressedEdge[0] = false; }
                y += kMenuRow;
            }
            dl.popClip();
        }
        if (chosen) closeMenus();
        else if (m_pressedEdge[0] && !insideAny && float(m_time) - m_menuOpenedAt > 0.01f) { m_pressedEdge[0] = false; closeMenus(); }
        else if (m_pressedEdge[1] && !insideAny) { m_pressedEdge[1] = false; closeMenus(); }
    }
    if (chosen) m_deferred = chosen;
}

void Gui::menuBar(const Rect& r, const std::vector<TopMenu>& menus) {
    DrawList& dl = *m_dl;
    float x = r.x + 6;
    for (size_t k = 0; k < menus.size(); ++k) {
        const std::string title = tr(menus[k].title);
        const float w = dl.textWidth(title, normal) + 22;
        const Rect b{x, r.y + 2, w, r.h - 4};
        const bool inside = m_mouseIn && b.contains(m_mx, m_my);
        const bool open = m_menuOpen && m_barOpen == int(k);
        const float h = anim(id("menubar", int(k)), (inside || open) ? 1.f : 0.f);
        if (h > 0.01f) dl.rrect(b, 4, mix(withAlpha(bg3, 0), open ? bg4 : bg3, h));
        dl.textIn(b, title, mix(textDim, text, h), normal, Align::Center);
        const bool openedByHover = m_menuOpen && m_barOpen >= 0 && m_barOpen != int(k) && inside && !m_pressedEdge[0] && std::fabs(m_mx - m_pmx) + std::fabs(m_my - m_pmy) > 0;
        if ((inside && m_pressedEdge[0]) || openedByHover) {
            const bool was = m_menuOpen && m_barOpen == int(k);
            m_pressedEdge[0] = false;
            m_pressClaimed = true;
            if (was) closeMenus();
            else { openMenu(b.x, r.bottom(), menus[k].build()); m_barOpen = int(k); }
        }
        x += w + 2;
    }
}

// ------------------------------------------------------------------------------------------------------------- frame end

void Gui::endFrame() {
    drawMenus();
    // tooltip
    if (m_hot != m_tipHover) { m_tipHover = m_hot; m_tipSince = m_time; m_tipShown = false; }
    if (m_tipId != 0 && m_tipId == m_hot && !m_down[0] && !m_menuOpen && !m_tipText.empty()) {
        if (m_time - m_tipSince > 0.55) {
            m_tipShown = true;
            DrawList& dl = *m_dl;
            const std::string t = tr(m_tipText);
            const float w = dl.textWidth(t, small) + 16, h = 22;
            const float x = std::clamp(m_mx + 12, 4.f, std::max(4.f, dl.width() - w - 4)), y = m_my + 22 + h > dl.height() ? m_my - h - 8 : m_my + 18;
            dl.shadow({x, y, w, h}, 5, 6, withAlpha(rgb(0, 0, 0), 120));
            dl.rrect({x, y, w, h}, 5, rgb(0x0f, 0x11, 0x16));
            dl.rrectStroke({x, y, w, h}, 5, 1, border);
            dl.textIn({x, y, w, h}, t, text, small, Align::Center);
        } else {
            m_animating = true;
        }
    }
    if (m_pressedEdge[0] && !m_pressClaimed) m_focus = 0;
    if (m_focusIsText && !m_textFocusSeen) { m_focus = 0; m_focusIsText = false; }
    m_textFocus = m_textFocusSeen;
    m_textFocusSeen = false;
    m_caretValid = m_caretSeen;
    m_caretSeen = false;
    if (m_focus == 0) m_focusIsText = false;
    for (int b = 0; b < 3; ++b) { m_pressedEdge[b] = false; m_releasedEdge[b] = false; }
    if (m_active != 0 && !m_down[0]) m_active = 0;
    m_wheelX = m_wheelY = 0;
    m_keys.clear();
    m_text.clear();
    m_doubleEdge = false;
    m_pmx = m_mx;
    m_pmy = m_my;
    m_pendingInput = false;
    if (m_deferred) { auto f = std::move(m_deferred); m_deferred = nullptr; f(); m_pendingInput = true; }
}

}  // namespace vkui
