// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Vulkan UI, layer 2: an immediate-mode GUI toolkit. Every frame the program describes its widgets with plain function calls
// (button(), slider(), ...); the Gui turns mouse / keyboard input into their return values and the DrawList into pixels.
// There is no widget tree to keep in sync with the Session, which keeps the DAW screens short and easy to test.
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "Draw.h"

namespace vkui {

enum Mod { ModCtrl = 1, ModShift = 2, ModAlt = 4 };

enum Key : int {
    KeyNone = 0, KeySpace = ' ', KeyComma = ',', KeyPeriod = '.', KeySlash = '/', KeyMinus = '-', KeyPlus = '+', KeyLBracket = '[', KeyRBracket = ']',
    KeyEnter = 256, KeyEscape, KeyBackspace, KeyDelete, KeyTab, KeyLeft, KeyRight, KeyUp, KeyDown, KeyHome, KeyEnd, KeyPageUp, KeyPageDown,
    KeyF1 = 300, KeyF2, KeyF3, KeyF4, KeyF5, KeyF6, KeyF7, KeyF8, KeyF9, KeyF10, KeyF11, KeyF12,
};   // letters and digits use their upper-case ASCII code

struct KeyEvent { int key = 0; int mods = 0; bool repeat = false; };

enum class Cursor { Arrow, Hand, ResizeH, ResizeV, IBeam, Cross, Move, Pencil, Scissors, Eraser, NotAllowed };

using Id = std::uint32_t;

struct Interact {
    bool hovered = false, pressed = false, held = false, released = false, clicked = false, doubleClicked = false, rightClicked = false;
    float dx = 0, dy = 0;          // mouse movement since the previous frame while held
    float mx = 0, my = 0;          // mouse position
    float startX = 0, startY = 0;  // where the press began
};

struct MenuItem {
    std::string label;
    std::string shortcut;
    bool enabled = true, checked = false, separator = false;
    std::function<void()> action;
    std::vector<MenuItem> children;
    static MenuItem sep() { MenuItem m; m.separator = true; return m; }
};

struct TopMenu {
    std::string title;
    std::function<std::vector<MenuItem>()> build;
};

enum class ButtonKind { Normal, Accent, Danger, Flat };

class Gui {
public:
    // ---- the platform feeds events in, then asks for a frame ----
    void onMouseMove(float x, float y);
    void onMouseButton(int button, bool down, double timeSeconds = -1);   // 0 left, 1 right, 2 middle; time for double-click detection
    void onWheel(float dx, float dy);
    void onKey(int key, int mods, bool down, bool repeat);
    void onText(const std::string& utf8);
    void setMods(int mods) { m_mods = mods; }
    void onMouseLeave() { m_mouseIn = false; }

    void beginFrame(DrawList& dl, double timeSeconds);
    void endFrame();   // menus, tooltips; clears the per-frame input
    bool needsFrame() const { return m_animating || m_pendingInput || m_menuOpen; }
    Cursor cursor() const { return m_cursor; }

    DrawList& dl() { return *m_dl; }
    double time() const { return m_time; }
    float dt() const { return m_dt; }
    float mouseX() const { return m_mx; }
    float mouseY() const { return m_my; }
    int mods() const { return m_mods; }
    bool ctrl() const { return m_mods & ModCtrl; }
    bool shift() const { return m_mods & ModShift; }
    bool alt() const { return m_mods & ModAlt; }
    bool mouseDown(int b = 0) const { return m_down[b]; }
    float wheelY() const { return m_inputEnabled ? m_wheelY : 0.f; }
    float wheelX() const { return m_inputEnabled ? m_wheelX : 0.f; }
    void consumeWheel() { m_wheelX = m_wheelY = 0; }
    void setCursor(Cursor c) { m_cursor = c; }

    /// Widgets ignore the mouse and the keyboard while this is false (the studio disables the screen below a dialog or menu).
    void setInputEnabled(bool on) { m_inputEnabled = on; }
    bool inputEnabled() const { return m_inputEnabled; }

    // ---- ids ----
    Id id(const std::string& name) const;
    Id id(const std::string& name, int n) const;
    void pushId(Id i) { m_idStack.push_back(i); }
    void popId() { if (!m_idStack.empty()) m_idStack.pop_back(); }

    // ---- low level interaction ----
    Interact interact(Id id, const Rect& r, bool allowDrag = true);
    bool hovering(const Rect& r) const;   // mouse inside r (clipped), ignoring who else is hovered
    bool isActive(Id id) const { return m_active == id; }
    bool isFocused(Id id) const { return m_focus == id; }
    bool anyActive() const { return m_active != 0; }
    void setFocus(Id id) { m_focus = id; }
    void clearFocus() { m_focus = 0; }
    bool textFocused() const { return m_textFocus; }
    /// IME composition (Korean / Japanese / Chinese typing): the text being composed is shown inside the focused text field at the caret,
    /// underlined, and joins the text when the platform commits it (onText). The platform layer sets it from WM_IME_COMPOSITION.
    void setPreedit(const std::string& utf8) { m_preedit = utf8; m_pendingInput = true; }
    const std::string& preedit() const { return m_preedit; }
    /// The caret of the focused text field in window coordinates (logical px: x, top y, height), for placing the IME windows. False without one.
    bool imeCaret(float& x, float& y, float& h) const { x = m_caretX; y = m_caretY; h = m_caretH; return m_caretValid; }
    /// A key press (consumed when matched). Not delivered while a text field has the focus, unless 'evenInText'.
    bool key(int key, int mods = 0, bool allowRepeat = false, bool evenInText = false);
    bool keyAny(int key, bool allowRepeat = false);   // any modifiers
    std::string takeText() { std::string t; t.swap(m_text); return t; }
    float anim(Id id, float target, float speed = 16.f);
    void requestFrame() { m_animating = true; }
    void tip(Id id, const std::string& text);

    // ---- widgets ----
    bool button(Id id, const Rect& r, const std::string& label, ButtonKind kind = ButtonKind::Normal, bool enabled = true);
    using IconFn = std::function<void(DrawList&, const Rect&, Color)>;
    /// A square-ish button with a drawn icon; 'on' shows it pressed in the accent colour.
    bool iconButton(Id id, const Rect& r, const IconFn& icon, const std::string& tooltip, bool on = false, Color onColor = 0, bool enabled = true);
    bool toggle(Id id, const Rect& r, const std::string& label, bool& value, Color onColor = 0);
    bool checkbox(Id id, const Rect& r, const std::string& label, bool& value);
    /// Horizontal slider; returns true when the value changed. Double-click resets to 'def'.
    bool slider(Id id, const Rect& r, float& value, float lo, float hi, float def, const std::string& valueText = {});
    /// Rotary knob driven by vertical dragging.
    bool knob(Id id, const Rect& r, float& value, float lo, float hi, float def, const std::string& label = {}, const std::string& valueText = {}, Color color = 0);
    /// Vertical fader with a drawn level in 0 .. 1 (not 0 dB based: the caller maps dB to this scale).
    bool fader(Id id, const Rect& r, float& value, float lo, float hi, float def, Color color = 0);
    /// Single line text editor. Returns true on Enter (value committed); 'changed' is set on every edit.
    bool textField(Id id, const Rect& r, std::string& text, const std::string& hint = {}, bool* changed = nullptr, bool password = false);
    /// A number that drags vertically, scrolls with the wheel and types on double-click (LCD style).
    bool numberField(Id id, const Rect& r, double& value, double lo, double hi, double step, int decimals, const std::string& suffix = {}, double def = 0);
    /// Choice popup: a button showing the current choice that opens a menu. Returns true when changed.
    bool dropdown(Id id, const Rect& r, int& index, const std::vector<std::string>& options);
    void scrollBar(Id id, const Rect& r, float& offset, float contentSize, float viewSize, bool vertical);
    void separator(const Rect& r, bool vertical = false);

    // ---- menus ----
    void openMenu(float x, float y, std::vector<MenuItem> items);
    void closeMenus();
    bool menuOpen() const { return m_menuOpen; }
    /// Draws a menu bar into r. Returns the width used.
    void menuBar(const Rect& r, const std::vector<TopMenu>& menus);

    // ---- clipboard (the platform installs these) ----
    std::function<std::string()> clipboardGet;
    std::function<void(const std::string&)> clipboardSet;

    // ---- translation of every string the toolkit itself draws ----
    std::function<const std::string&(const std::string&)> translate;
    const std::string& tr(const std::string& s) const { return translate ? translate(s) : s; }

private:
    struct EditState { int caret = 0, sel = 0; std::string original; bool fresh = true; };
    struct Menu { std::vector<MenuItem> items; Rect rect; int hover = -1; float scroll = 0; };
    void drawMenus();
    Rect placeMenu(Menu& m, float x, float y, bool submenu, float parentRight);
    void applyCaretFromMouse(EditState& e, const std::string& text, const Rect& r, float mx);

    DrawList* m_dl = nullptr;
    double m_time = 0;
    float m_dt = 0.016f;
    // mouse
    float m_mx = -1, m_my = -1, m_pmx = -1, m_pmy = -1;
    bool m_down[3] = {false, false, false}, m_pressedEdge[3] = {false, false, false}, m_releasedEdge[3] = {false, false, false};
    bool m_mouseIn = true;
    float m_wheelX = 0, m_wheelY = 0;
    double m_lastClickTime = -10;
    float m_lastClickX = 0, m_lastClickY = 0;
    bool m_doubleEdge = false;
    int m_mods = 0;
    std::vector<KeyEvent> m_keys;
    std::string m_text;
    bool m_inputEnabled = true, m_pendingInput = false, m_animating = false;
    Cursor m_cursor = Cursor::Arrow;
    // interaction state
    Id m_active = 0, m_hot = 0, m_focus = 0, m_prevHot = 0;
    float m_startX = 0, m_startY = 0;
    bool m_textFocus = false, m_textFocusSeen = false;
    std::string m_preedit;
    float m_caretX = 0, m_caretY = 0, m_caretH = 0;
    bool m_caretValid = false, m_caretSeen = false;
    std::vector<Id> m_idStack;
    std::map<Id, float> m_anim;
    std::map<Id, EditState> m_edit;
    Id m_typingNumber = 0;
    std::map<Id, std::string> m_numText;
    std::map<Id, int> m_picked;
    bool m_pressClaimed = false, m_focusIsText = false, m_tipShown = false;
    // tooltip
    Id m_tipHover = 0;
    double m_tipSince = 0;
    std::string m_tipText;
    Id m_tipId = 0;
    // menus
    std::vector<Menu> m_menus;
    bool m_menuOpen = false;
    std::function<void()> m_deferred;
    int m_barOpen = -1;
    float m_menuOpenedAt = 0;
};

}  // namespace vkui
