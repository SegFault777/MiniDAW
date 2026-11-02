// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Dialog.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include "Ui.h"

namespace ui {

namespace {
constexpr Rgba kPanel = rgba(41, 45, 54), kField = rgba(20, 22, 27), kLine = rgba(14, 15, 18), kText = rgba(226, 230, 238), kDim = rgba(137, 146, 163), kAccent = rgba(38, 222, 196);
constexpr int kRowH = 34, kPanelW = 440, kLabelW = 168, kTitleH = 40, kFooterH = 56;

std::string fmt(double v, int decimals, const std::string& suffix) {
    char b[64];
    std::snprintf(b, sizeof b, "%.*f", decimals, v);
    return std::string(b) + suffix;
}
}  // namespace

Field Field::number(std::string l, double v, double lo, double hi, double st, int dec, std::string suf) { Field f; f.kind = Number; f.label = std::move(l); f.value = v; f.min = lo; f.max = hi; f.step = st; f.decimals = dec; f.suffix = std::move(suf); return f; }
Field Field::slider(std::string l, double v, double lo, double hi, int dec, std::string suf) { Field f; f.kind = Slider; f.label = std::move(l); f.value = v; f.min = lo; f.max = hi; f.decimals = dec; f.suffix = std::move(suf); return f; }
Field Field::check(std::string l, bool on) { Field f; f.kind = Check; f.label = std::move(l); f.checked = on; return f; }
Field Field::choice(std::string l, std::vector<std::string> items, int idx) { Field f; f.kind = Choice; f.label = std::move(l); f.choices = std::move(items); f.index = idx; return f; }
Field Field::input(std::string l, std::string t) { Field f; f.kind = Text; f.label = std::move(l); f.text = std::move(t); return f; }
Field Field::note(std::string t) { Field f; f.kind = Label; f.text = std::move(t); return f; }
Field Field::button(std::string l, std::function<void()> p, bool c) { Field f; f.kind = Button; f.label = std::move(l); f.onPress = std::move(p); f.closes = c; return f; }

Dialog::Dialog(std::string title, Values fields, std::function<void(const Values&)> onOk, std::string okLabel, bool showCancel)
    : m_title(std::move(title)), m_ok(std::move(okLabel)), m_fields(std::move(fields)), m_onOk(std::move(onOk)), m_showCancel(showCancel) {
    layout();
}

void Dialog::layout() {
    int h = kTitleH + kFooterH + 12;
    for (const Field& f : m_fields) h += f.kind == Field::Label ? 40 : kRowH;
    m_panel = {(m_sw - kPanelW) / 2, std::max(10, (m_sh - h) / 3), kPanelW, h};
    m_rows.clear();
    int y = m_panel.y + kTitleH + 6;
    for (const Field& f : m_fields) {
        const int rh = f.kind == Field::Label ? 40 : kRowH;
        if (f.kind == Field::Label) m_rows.push_back({m_panel.x + 20, y, kPanelW - 40, rh});
        else if (f.kind == Field::Button) m_rows.push_back({m_panel.x + 20 + kLabelW, y + 3, kPanelW - 40 - kLabelW, rh - 6});
        else m_rows.push_back({m_panel.x + 20 + kLabelW, y + 4, kPanelW - 40 - kLabelW, rh - 8});
        y += rh;
    }
    m_okRect = {m_panel.right() - 20 - 96, m_panel.bottom() - 46, 96, 32};
    m_cancelRect = {m_okRect.x - 104, m_okRect.y, 96, 32};
}

void Dialog::accept() {
    commitEdit();
    m_done = true;
    m_accepted = true;
    if (m_onOk) m_onOk(m_fields);
}

void Dialog::commitEdit() {
    if (!m_editing || m_focus < 0) return;
    Field& f = m_fields[size_t(m_focus)];
    if (f.kind == Field::Number) {
        try { f.value = std::clamp(std::stod(m_edit), f.min, f.max); } catch (...) {}
    } else if (f.kind == Field::Text) f.text = m_edit;
    m_editing = false;
}

double Dialog::rowValueAt(const Field& f, int x, const Rect& r) const {
    const double t = std::clamp(double(x - r.x) / std::max(1, r.w), 0.0, 1.0);
    return f.min + t * (f.max - f.min);
}

void Dialog::paint(Canvas& c) {
    c.fillRect({0, 0, m_sw, m_sh}, rgba(0, 0, 0, 150));
    c.fillRoundRect(m_panel.inset(-3), 14, rgba(0, 0, 0, 90));
    c.fillRoundRect(m_panel, 12, kPanel);
    c.strokeRoundRect(m_panel, 12, rgba(84, 91, 108));
    c.pushClip(m_panel);
    c.fillGradient({m_panel.x, m_panel.y, m_panel.w, kTitleH}, rgba(58, 64, 77), rgba(48, 53, 64));
    c.popClip();
    c.textIn({m_panel.x + 20, m_panel.y, m_panel.w - 40, kTitleH}, m_title, kText, Font::Bold13);
    for (size_t i = 0; i < m_fields.size(); ++i) {
        const Field& f = m_fields[i];
        const Rect r = m_rows[i];
        const bool focus = int(i) == m_focus;
        if (f.kind == Field::Label) { c.textIn(r, f.text, kDim, Font::Regular11); continue; }
        c.textIn({m_panel.x + 20, r.y, kLabelW - 8, r.h}, f.label, kText, Font::Regular11);
        switch (f.kind) {
        case Field::Number: {
            c.fillRoundRect(r, 6, kField);
            c.strokeRoundRect(r, 6, focus ? kAccent : rgba(70, 76, 90));
            c.textIn(r.inset(30, 0), focus && m_editing ? m_edit + "|" : fmt(f.value, f.decimals, f.suffix), kText, Font::Regular11, Align::Center);
            c.textIn({r.x, r.y, 28, r.h}, "-", kDim, Font::Bold13, Align::Center);
            c.textIn({r.right() - 28, r.y, 28, r.h}, "+", kDim, Font::Bold13, Align::Center);
            break;
        }
        case Field::Slider: {
            const Rect groove{r.x, r.y + r.h / 2 - 3, r.w - 64, 6};
            c.fillRoundRect(groove, 3, kField);
            const double t = (f.value - f.min) / std::max(1e-9, f.max - f.min);
            c.pushClip(groove);
            c.fillRoundRect({groove.x, groove.y, int(groove.w * t), groove.h}, 3, kAccent);
            c.popClip();
            c.fillCircle(groove.x + groove.w * t, groove.y + 3, 7, rgba(236, 240, 247));
            c.textIn({groove.right() + 8, r.y, 56, r.h}, fmt(f.value, f.decimals, f.suffix), kDim, Font::Regular11, Align::Right);
            break;
        }
        case Field::Check: {
            const Rect box{r.x, r.y + r.h / 2 - 9, 18, 18};
            c.fillRoundRect(box, 4, f.checked ? kAccent : kField);
            c.strokeRoundRect(box, 4, focus ? kAccent : rgba(70, 76, 90));
            if (f.checked) { c.line(box.x + 4, box.y + 9, box.x + 8, box.y + 13, rgba(10, 36, 32)); c.line(box.x + 8, box.y + 13, box.x + 14, box.y + 5, rgba(10, 36, 32)); }
            break;
        }
        case Field::Choice: {
            c.fillRoundRect(r, 6, kField);
            c.strokeRoundRect(r, 6, focus ? kAccent : rgba(70, 76, 90));
            c.textIn(r.inset(28, 0), f.choices.empty() ? "" : f.choices[size_t(std::clamp(f.index, 0, int(f.choices.size()) - 1))], kText, Font::Regular11, Align::Center);
            c.fillTriangle(r.x + 14, r.y + r.h / 2, r.x + 20, r.y + r.h / 2 - 5, r.x + 20, r.y + r.h / 2 + 5, kDim);
            c.fillTriangle(r.right() - 14, r.y + r.h / 2, r.right() - 20, r.y + r.h / 2 - 5, r.right() - 20, r.y + r.h / 2 + 5, kDim);
            break;
        }
        case Field::Text: {
            c.fillRoundRect(r, 6, kField);
            c.strokeRoundRect(r, 6, focus ? kAccent : rgba(70, 76, 90));
            c.textIn(r.inset(10, 0), focus && m_editing ? m_edit + "|" : f.text, kText, Font::Regular11);
            break;
        }
        case Field::Button: {
            c.fillRoundRect(r, 6, int(i) == m_hover ? rgba(66, 72, 86) : rgba(52, 57, 68));
            c.strokeRoundRect(r, 6, rgba(84, 91, 108));
            c.textIn(r, f.label, kText, Font::Regular11, Align::Center);
            c.textIn({m_panel.x + 20, r.y, kLabelW - 8, r.h}, "", kText);
            break;
        }
        default: break;
        }
    }
    if (m_showCancel) {
        c.fillRoundRect(m_cancelRect, 8, rgba(52, 57, 68));
        c.strokeRoundRect(m_cancelRect, 8, rgba(84, 91, 108));
        c.textIn(m_cancelRect, "Cancel", kText, Font::Bold11, Align::Center);
    }
    c.fillRoundRect(m_okRect, 8, kAccent);
    c.textIn(m_okRect, m_ok, rgba(8, 34, 30), Font::Bold11, Align::Center);
}

bool Dialog::mouseDown(int x, int y, int button) {
    (void)button;
    if (m_okRect.contains(x, y)) { accept(); return true; }
    if (m_showCancel && m_cancelRect.contains(x, y)) { cancel(); return true; }
    commitEdit();
    for (size_t i = 0; i < m_fields.size(); ++i) {
        Field& f = m_fields[i];
        const Rect r = m_rows[i];
        if (!r.contains(x, y) && !(f.kind == Field::Check && Rect{m_panel.x + 20, r.y - 4, kPanelW - 40, kRowH}.contains(x, y))) continue;
        m_focus = int(i);
        switch (f.kind) {
        case Field::Number:
            if (x < r.x + 28) f.value = std::clamp(f.value - f.step, f.min, f.max);
            else if (x >= r.right() - 28) f.value = std::clamp(f.value + f.step, f.min, f.max);
            else { m_editing = true; m_fresh = true; m_edit = fmt(f.value, f.decimals, ""); }
            break;
        case Field::Slider: m_drag = int(i); f.value = rowValueAt(f, x, {r.x, r.y, r.w - 64, r.h}); if (f.decimals == 0) f.value = std::round(f.value); break;
        case Field::Check: f.checked = !f.checked; break;
        case Field::Choice: {
            const int n = int(f.choices.size());
            if (n > 0) f.index = (f.index + (x < r.x + r.w / 2 ? n - 1 : 1)) % n;
            break;
        }
        case Field::Text: m_editing = true; m_fresh = false; m_edit = f.text; break;
        case Field::Button: if (f.onPress) f.onPress(); if (f.closes) m_done = true; break;
        default: break;
        }
        return true;
    }
    return true;   // modal: swallow every click
}

void Dialog::mouseMove(int x, int y) {
    m_hover = -1;
    for (size_t i = 0; i < m_fields.size(); ++i) if (m_rows[i].contains(x, y)) m_hover = int(i);
    if (m_drag >= 0) {
        Field& f = m_fields[size_t(m_drag)];
        const Rect r = m_rows[size_t(m_drag)];
        f.value = rowValueAt(f, x, {r.x, r.y, r.w - 64, r.h});
        if (f.decimals == 0) f.value = std::round(f.value);
    }
}

void Dialog::mouseUp(int, int) { m_drag = -1; }

bool Dialog::key(int k, int mods) {
    (void)mods;
    if (k == KeyEscape) { m_editing ? (void)(m_editing = false) : cancel(); return true; }
    if (k == KeyEnter) { if (m_editing) commitEdit(); else accept(); return true; }
    if (m_editing && k == KeyBackspace) { if (m_fresh) { m_edit.clear(); m_fresh = false; } else if (!m_edit.empty()) m_edit.pop_back(); return true; }
    if (k == KeyUp || k == KeyDown) {
        if (m_focus >= 0 && m_fields[size_t(m_focus)].kind == Field::Number) {
            Field& f = m_fields[size_t(m_focus)];
            f.value = std::clamp(f.value + (k == KeyUp ? f.step : -f.step), f.min, f.max);
            return true;
        }
    }
    if ((k == KeyLeft || k == KeyRight) && m_focus >= 0) {
        Field& f = m_fields[size_t(m_focus)];
        const int d = k == KeyRight ? 1 : -1;
        if (f.kind == Field::Choice && !f.choices.empty()) { const int n = int(f.choices.size()); f.index = (f.index + d + n) % n; return true; }
        if (f.kind == Field::Slider) { f.value = std::clamp(f.value + d * (f.max - f.min) / 50.0, f.min, f.max); return true; }
    }
    if (k == KeySpace && m_focus >= 0 && !m_editing && m_fields[size_t(m_focus)].kind == Field::Check) { m_fields[size_t(m_focus)].checked ^= true; return true; }
    if (k == '\t') { m_focus = (m_focus + 1) % std::max<int>(1, int(m_fields.size())); return true; }
    return true;   // modal: keys never leak to the window behind
}

void Dialog::text(const std::string& utf8) {
    if (!m_editing) {
        // typing while a number field is focused starts editing
        if (m_focus >= 0 && m_fields[size_t(m_focus)].kind == Field::Number && !utf8.empty() && (std::isdigit((unsigned char)utf8[0]) || utf8[0] == '-' || utf8[0] == '.')) { m_editing = true; m_edit.clear(); }
        else return;
    }
    if (m_fresh) { m_edit.clear(); m_fresh = false; }
    if (m_focus >= 0 && m_fields[size_t(m_focus)].kind == Field::Number) { for (char ch : utf8) if (std::isdigit((unsigned char)ch) || ch == '.' || ch == '-') m_edit.push_back(ch); }
    else for (char ch : utf8) if ((unsigned char)ch >= 32) m_edit.push_back(ch);
}

}  // namespace ui
