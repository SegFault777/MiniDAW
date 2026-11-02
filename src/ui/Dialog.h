// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Modal dialog of the Qt-free UI: a titled panel with rows of simple controls (number, slider, check box, choice,
// text, label) and OK / Cancel. Dialogs only collect values; the caller applies them in the OK callback.
#include <functional>
#include <string>
#include <vector>
#include "Canvas.h"

namespace ui {

struct Field {
    enum Kind { Number, Slider, Check, Choice, Text, Label, Button } kind = Number;
    std::string label;
    double value = 0, min = 0, max = 100, step = 1;
    int decimals = 0;
    std::string suffix;
    bool checked = false;
    std::vector<std::string> choices;
    int index = 0;
    std::string text;
    std::function<void()> onPress;   ///< Button fields run this
    bool closes = false;             ///< ... and the dialog closes afterwards when true

    static Field number(std::string label, double v, double lo, double hi, double step = 1, int decimals = 0, std::string suffix = {});
    static Field slider(std::string label, double v, double lo, double hi, int decimals = 2, std::string suffix = {});
    static Field check(std::string label, bool on);
    static Field choice(std::string label, std::vector<std::string> items, int index);
    static Field input(std::string label, std::string text);
    static Field note(std::string text);
    static Field button(std::string label, std::function<void()> press, bool closesDialog = false);
};

class Dialog {
public:
    using Values = std::vector<Field>;
    Dialog(std::string title, Values fields, std::function<void(const Values&)> onOk, std::string okLabel = "OK", bool showCancel = true);

    void paint(Canvas& c);
    bool mouseDown(int x, int y, int button);
    void mouseMove(int x, int y);
    void mouseUp(int x, int y);
    /// Returns true when the key was used. Enter confirms, Escape cancels.
    bool key(int key, int mods);
    void text(const std::string& utf8);

    bool finished() const { return m_done; }
    bool accepted() const { return m_accepted; }
    const Values& values() const { return m_fields; }
    void setScreen(int w, int h) { m_sw = w; m_sh = h; layout(); }
    void close() { m_done = true; m_accepted = false; }

private:
    void layout();
    void accept();
    void cancel() { m_done = true; m_accepted = false; }
    void commitEdit();
    double rowValueAt(const Field& f, int x, const Rect& control) const;

    std::string m_title, m_ok;
    Values m_fields;
    std::function<void(const Values&)> m_onOk;
    bool m_showCancel = true, m_done = false, m_accepted = false;
    int m_sw = 1280, m_sh = 720;
    Rect m_panel, m_okRect, m_cancelRect;
    std::vector<Rect> m_rows;   // control area of every field
    int m_focus = -1, m_drag = -1, m_hover = -1;
    std::string m_edit;         // text being typed into a number / text field
    bool m_editing = false, m_fresh = false;   // m_fresh: the next typed character replaces the whole text
};

}  // namespace ui
