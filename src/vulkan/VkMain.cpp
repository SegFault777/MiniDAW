// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Windows shell of the Vulkan user interface (MiniDAWVulkan.exe): the window, input, GDI glyph rasteriser, audio, MIDI,
// plug-ins and settings. It moves events into vkui::Gui, runs vkui::Studio once per frame and hands the draw list to the GPU.
// windows.h must not define the min / max macros: dsp/Param.h has members called min and max.
#ifndef NOMINMAX
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>
#include <imm.h>
#pragma comment(lib, "imm32.lib")

#include <algorithm>
#include <atomic>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../core/AppPaths.h"
#include "../core/I18n.h"
#include "../core/Session.h"
#include "../native/Vst3Native.h"
#include "../platform/Audio.h"
#include "../platform/Decode.h"
#include "../platform/Mp3Encode.h"
#include "../platform/Midi.h"
#include "../platform/PluginHost.h"
#include "Studio.h"
#include "Theme.h"
#include "VkRenderer.h"

#ifndef MINIDAW_VERSION
#define MINIDAW_VERSION "0.12"
#endif

namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(std::max(n, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}
std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(std::max(n, 0)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

// ------------------------------------------------------------------------------------------------ glyphs from GDI
// Segoe UI through GDI, one character at a time: full Unicode (Korean, Japanese, ...) through Windows' font linking, any size,
// so the interface is sharp at every display scale.
class GdiFontSource : public vkui::FontSource {
public:
    GdiFontSource() {
        m_dc = CreateCompatibleDC(nullptr);
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = kW;
        bi.bmiHeader.biHeight = -kH;   // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        m_bmp = CreateDIBSection(m_dc, &bi, DIB_RGB_COLORS, &m_bits, nullptr, 0);
        SelectObject(m_dc, m_bmp);
        SetBkMode(m_dc, TRANSPARENT);
        SetTextColor(m_dc, RGB(255, 255, 255));
    }
    ~GdiFontSource() override {
        for (auto& kv : m_fonts) DeleteObject(kv.second);
        DeleteObject(m_bmp);
        DeleteDC(m_dc);
    }
    bool rasterize(std::uint32_t cp, bool bold, int px, vkui::GlyphBitmap& out) override {
        wchar_t buf[2];
        int n = 1;
        if (cp >= 0x10000) { cp -= 0x10000; buf[0] = wchar_t(0xD800 + (cp >> 10)); buf[1] = wchar_t(0xDC00 + (cp & 0x3FF)); n = 2; }
        else buf[0] = wchar_t(cp);
        SelectObject(m_dc, font(bold, px));
        SIZE sz{};
        GetTextExtentPoint32W(m_dc, buf, n, &sz);
        TEXTMETRICW tm{};
        GetTextMetricsW(m_dc, &tm);
        out.advance = float(sz.cx);
        const int pad = px / 4 + 2;
        const int w = std::min<int>(kW, int(sz.cx) + 2 * pad), h = std::min<int>(kH, int(tm.tmHeight) + 2);
        if (cp == ' ' || w <= 0 || h <= 0) return true;
        std::memset(m_bits, 0, size_t(kW) * size_t(h) * 4);
        TextOutW(m_dc, pad, 0, buf, n);
        GdiFlush();
        out.a8.resize(size_t(w) * size_t(h));
        bool any = false;
        const unsigned char* src = static_cast<const unsigned char*>(m_bits);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const unsigned char v = src[(size_t(y) * kW + size_t(x)) * 4 + 1];
                out.a8[size_t(y) * size_t(w) + size_t(x)] = v;
                any = any || v > 0;
            }
        if (!any) { out.a8.clear(); return true; }
        out.w = w; out.h = h; out.bearingX = -pad; out.bearingY = 0;
        return true;
    }
    float lineHeight(bool bold, int px) override {
        SelectObject(m_dc, font(bold, px));
        TEXTMETRICW tm{};
        GetTextMetricsW(m_dc, &tm);
        return float(tm.tmHeight);
    }

private:
    static constexpr int kW = 192, kH = 160;
    HFONT font(bool bold, int px) {
        const int key = px * 2 + (bold ? 1 : 0);
        auto it = m_fonts.find(key);
        if (it != m_fonts.end()) return it->second;
        HFONT f = CreateFontW(-px, 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        m_fonts[key] = f;
        return f;
    }
    HDC m_dc = nullptr;
    HBITMAP m_bmp = nullptr;
    void* m_bits = nullptr;
    std::map<int, HFONT> m_fonts;
};

// ----------------------------------------------------------------------------------------------------------- the app
struct Catalog {
    std::mutex mutex;
    std::vector<vst3n::PluginInfo> vst3;
    std::atomic<bool> scanning{false};
};

struct App {
    Catalog catalog;
    core::Session session;
    std::shared_ptr<vkui::Fonts> fonts;
    vkui::Gui gui;
    vkui::DrawList dl;
    std::unique_ptr<vkui::Studio> studio;
    std::unique_ptr<vkui::VkRenderer> renderer;
    std::unique_ptr<platform::AudioStream> out, in;
    std::unique_ptr<platform::MidiPort> midi;
    HWND hwnd = nullptr;
    std::atomic<int> decoding{0};
    std::map<std::string, std::string> settings;
    std::string settingsPath, dataDir, exeDir;
    int autosaveMinutes = 2;
    float dpiScale = 1.f, userScale = 1.f;
    bool dirty = true, inFrame = false, mouseTracked = false, portable = false, imeOn = true;
    std::string audioStatus;
    LARGE_INTEGER freq{}, start{};
};
App* gApp = nullptr;
constexpr UINT kJobMessage = WM_APP + 1;   // a worker thread hands a function to the UI thread
constexpr UINT kWake = WM_APP + 3;         // something changed on another thread
struct Job { std::function<void()> fn; };

double nowSeconds(const App& a) {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return double(t.QuadPart - a.start.QuadPart) / double(a.freq.QuadPart) + 1.0;
}

int mods() {
    return (GetKeyState(VK_CONTROL) < 0 ? vkui::ModCtrl : 0) | (GetKeyState(VK_SHIFT) < 0 ? vkui::ModShift : 0) | (GetKeyState(VK_MENU) < 0 ? vkui::ModAlt : 0);
}

int mapKey(WPARAM vk) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return int(vk);
    if (vk >= VK_F1 && vk <= VK_F12) return vkui::KeyF1 + int(vk - VK_F1);
    switch (vk) {
    case VK_SPACE: return vkui::KeySpace;
    case VK_DELETE: return vkui::KeyDelete;
    case VK_BACK: return vkui::KeyBackspace;
    case VK_LEFT: return vkui::KeyLeft;
    case VK_RIGHT: return vkui::KeyRight;
    case VK_UP: return vkui::KeyUp;
    case VK_DOWN: return vkui::KeyDown;
    case VK_HOME: return vkui::KeyHome;
    case VK_END: return vkui::KeyEnd;
    case VK_PRIOR: return vkui::KeyPageUp;
    case VK_NEXT: return vkui::KeyPageDown;
    case VK_RETURN: return vkui::KeyEnter;
    case VK_ESCAPE: return vkui::KeyEscape;
    case VK_TAB: return vkui::KeyTab;
    case VK_OEM_COMMA: return ',';
    case VK_OEM_PERIOD: return '.';
    case VK_OEM_2: return '/';
    case VK_OEM_4: return '[';
    case VK_OEM_6: return ']';
    case VK_OEM_PLUS: return (GetKeyState(VK_SHIFT) < 0) ? vkui::KeyPlus : '=';
    case VK_ADD: return vkui::KeyPlus;
    case VK_OEM_MINUS: case VK_SUBTRACT: return vkui::KeyMinus;
    default: return 0;
    }
}

std::string fileDialog(HWND owner, const std::string& title, const std::string& filter, bool save) {
    std::wstring f = widen(filter);   // "Name|*.a;*.b" -> "Name\0*.a;*.b\0All files\0*.*\0\0"
    std::replace(f.begin(), f.end(), L'|', L'\0');
    f.push_back(L'\0');
    f += L"All files";
    f.push_back(L'\0');
    f += L"*.*";
    f.push_back(L'\0');
    f.push_back(L'\0');
    wchar_t file[MAX_PATH * 4] = {};
    const std::wstring t = widen(title);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = f.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = DWORD(std::size(file));
    ofn.lpstrTitle = t.c_str();
    ofn.Flags = OFN_EXPLORER | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    const BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    return ok ? narrow(file) : std::string();
}

std::string folderDialog(HWND owner, const std::string& title) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const std::wstring t = widen(title);
    BROWSEINFOW bi{};
    bi.hwndOwner = owner;
    bi.lpszTitle = t.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return {};
    wchar_t path[MAX_PATH * 2] = {};
    const BOOL ok = SHGetPathFromIDListW(pidl, path);
    CoTaskMemFree(pidl);
    return ok ? narrow(path) : std::string();
}

std::string envDir(const char* name) {
    char buf[MAX_PATH * 2];
    const DWORD n = GetEnvironmentVariableA(name, buf, DWORD(sizeof buf));
    return n > 0 && n < sizeof buf ? std::string(buf, n) : std::string();
}

void loadSettings(App& a) {
    std::ifstream in(a.settingsPath);
    std::string line;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (eq != std::string::npos) a.settings[line.substr(0, eq)] = line.substr(eq + 1);
    }
}
void saveSettings(App& a) {
    CreateDirectoryW(widen(a.dataDir).c_str(), nullptr);   // the folder may have been removed while the program was running
    std::ofstream out(a.settingsPath, std::ios::trunc);
    out << "# MiniDAW (Vulkan) settings - written by the program, safe to delete\n";
    for (const auto& kv : a.settings) out << kv.first << '=' << kv.second << '\n';
}

void openAudio(App& a) {
    a.out.reset();
    std::string err;
    const std::string id = a.settings.count("output") ? a.settings["output"] : std::string();
    const int buffer = a.settings.count("buffer") ? std::max(128, std::atoi(a.settings["buffer"].c_str())) : 256;
    a.out = platform::openOutput(id, buffer, [&a](std::int16_t* o, int n) { a.session.fillInt16(o, n); }, &err);
    if (!a.out && !id.empty()) a.out = platform::openOutput("", buffer, [&a](std::int16_t* o, int n) { a.session.fillInt16(o, n); }, &err);   // the saved device is gone
    a.audioStatus = a.out ? a.out->status() : "No audio output: " + err;
    if (a.studio) a.studio->message(a.audioStatus);
}

void openMidi(App& a) {
    a.midi.reset();
    const std::string want = a.settings.count("midiInput") ? a.settings["midiInput"] : std::string();
    if (want.empty()) return;
    const auto names = platform::midiInputs();
    for (size_t i = 0; i < names.size(); ++i)
        if (names[i] == want) {
            a.midi = platform::openMidiInput(int(i), [&a](int status, int d1, int d2) {
                const int cmd = status & 0xF0;
                const int track = a.studio ? a.studio->selectedTrack() : -1;
                if (track < 0) return;
                if (cmd == 0x90 && d2 > 0) a.session.noteOn(track, d1, d2);
                else if (cmd == 0x80 || (cmd == 0x90 && d2 == 0)) a.session.noteOff(track, d1);
            });
            if (a.midi) return;
            break;
        }
    if (a.studio) a.studio->message("Could not open the MIDI input " + want);
}

std::vector<std::string> splitFolders(const std::string& text) {
    std::vector<std::string> v;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(';', start);
        if (end == std::string::npos) end = text.size();
        std::string part = text.substr(start, end - start);
        while (!part.empty() && part.front() == ' ') part.erase(part.begin());
        while (!part.empty() && part.back() == ' ') part.pop_back();
        if (!part.empty()) v.push_back(part);
        start = end + 1;
    }
    return v;
}

void rescanPlugins(App& a) {
    if (a.catalog.scanning.exchange(true)) return;
    std::vector<std::string> nativeFolders = {a.exeDir + "\\plugins", a.dataDir + "\\plugins"};
    std::vector<std::string> vstFolders = a.settings.count("vst3Folders") ? splitFolders(a.settings["vst3Folders"]) : std::vector<std::string>();
    if (vstFolders.empty()) vstFolders = vst3n::defaultFolders();
    platform::scanNativePlugins(nativeFolders);
    std::thread([&a, vstFolders] {
        auto found = vst3n::scan(vstFolders);
        {
            std::lock_guard<std::mutex> lock(a.catalog.mutex);
            a.catalog.vst3 = std::move(found);
        }
        a.catalog.scanning = false;
        if (a.hwnd) PostMessageW(a.hwnd, WM_APP + 2, 0, 0);
    }).detach();
}

std::vector<vkui::PluginEntry> pluginEntries(App& a) {
    std::vector<vkui::PluginEntry> v;
    for (const auto& p : platform::nativePlugins()) v.push_back({p.id, p.name, p.vendor, false});
    std::lock_guard<std::mutex> lock(a.catalog.mutex);
    for (const auto& p : a.catalog.vst3) v.push_back({p.id, p.name, p.vendor, p.instrument});
    return v;
}

void applySettings(App& a) {
    a.autosaveMinutes = a.settings.count("autosave") ? std::max(0, std::atoi(a.settings["autosave"].c_str())) : 2;
    a.userScale = a.settings.count("uiScale") ? std::clamp(float(std::atof(a.settings["uiScale"].c_str())), 0.75f, 2.0f) : 1.f;
    if (a.settings.count("recOffset")) a.session.setRecordingOffsetMs(std::atoi(a.settings["recOffset"].c_str()));
    if (a.autosaveMinutes > 0 && a.hwnd) SetTimer(a.hwnd, 2, UINT(a.autosaveMinutes) * 60000u, nullptr);
    else if (a.hwnd) KillTimer(a.hwnd, 2);
    openAudio(a);
    openMidi(a);
    a.dirty = true;
}

// IME (Korean / Japanese / Chinese typing). While no text field has the focus the window's input context is taken away, so that a keyboard in
// Hangul mode still sends plain keys (Space, R, M, S ... keep working as shortcuts) instead of VK_PROCESSKEY. A text field gets it back, and the
// IME windows are put at its caret; the text being composed is drawn by the field itself (WM_IME_COMPOSITION below).
void syncIme(App& a) {
    const bool want = a.gui.textFocused();
    if (want != a.imeOn) {
        if (want) ImmAssociateContextEx(a.hwnd, nullptr, IACE_DEFAULT);
        else { ImmAssociateContext(a.hwnd, nullptr); a.gui.setPreedit({}); }
        a.imeOn = want;
    }
    float x = 0, y = 0, h = 0;
    if (!want || !a.gui.imeCaret(x, y, h)) return;
    if (HIMC c = ImmGetContext(a.hwnd)) {
        const float sc = std::max(0.75f, a.dpiScale * a.userScale);
        COMPOSITIONFORM cf{};
        cf.dwStyle = CFS_POINT;
        cf.ptCurrentPos = {LONG(x * sc), LONG(y * sc)};
        ImmSetCompositionWindow(c, &cf);
        CANDIDATEFORM cd{};
        cd.dwIndex = 0;
        cd.dwStyle = CFS_CANDIDATEPOS;
        cd.ptCurrentPos = {LONG(x * sc), LONG((y + h) * sc)};
        ImmSetCandidateWindow(c, &cd);
        ImmReleaseContext(a.hwnd, c);
    }
}

std::wstring imeString(HIMC c, DWORD what) {
    const LONG bytes = ImmGetCompositionStringW(c, what, nullptr, 0);
    if (bytes <= 0) return {};
    std::wstring s(size_t(bytes) / sizeof(wchar_t), L'\0');
    ImmGetCompositionStringW(c, what, s.data(), DWORD(bytes));
    return s;
}

void renderFrame(App& a) {
    if (a.inFrame || !a.renderer || !a.studio || IsIconic(a.hwnd)) return;
    RECT rc;
    GetClientRect(a.hwnd, &rc);
    const int w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;
    a.inFrame = true;
    const float scale = std::max(0.75f, a.dpiScale * a.userScale);
    a.renderer->resize(w, h);
    for (int pass = 0; pass < 2; ++pass) {   // a second pass only when the glyph atlas had to be cleared
        a.fonts->beginFrame();
        a.dl.begin(float(w) / scale, float(h) / scale, scale, a.fonts.get());
        const double t = nowSeconds(a);
        a.gui.beginFrame(a.dl, t);
        a.studio->frame(a.gui, t);
        a.gui.endFrame();
        if (!a.fonts->consumeOverflow()) break;
    }
    if (!a.renderer->render(a.dl, *a.fonts, vkui::theme::bg0)) a.dirty = true;   // the swap chain was rebuilt: draw again
    syncIme(a);
    if (a.fonts->takePending()) a.dirty = true;   // glyphs are still missing: draw the next frame at once (messages are handled in between)
    a.inFrame = false;
}

// While the user drags a window edge, Windows runs its own message loop and the main loop below does not run, so the new area
// would stay black until the mouse button is released. The window therefore draws itself from WM_SIZE and WM_PAINT: the content
// follows the edge while it is dragged (like Discord or a browser). A second pass is made when the swap chain had to be rebuilt.
void renderNow(App& a) {
    if (a.inFrame || !a.studio || !a.renderer || !a.hwnd || IsIconic(a.hwnd)) return;
    for (int tries = 0; tries < 2; ++tries) {
        a.dirty = false;
        renderFrame(a);
        if (!a.dirty) break;
    }
}

LPCWSTR cursorFor(vkui::Cursor c) {
    switch (c) {
    case vkui::Cursor::Hand: return IDC_HAND;
    case vkui::Cursor::ResizeH: return IDC_SIZEWE;
    case vkui::Cursor::ResizeV: return IDC_SIZENS;
    case vkui::Cursor::IBeam: return IDC_IBEAM;
    case vkui::Cursor::Cross: case vkui::Cursor::Pencil: case vkui::Cursor::Scissors: return IDC_CROSS;
    case vkui::Cursor::Move: return IDC_SIZEALL;
    case vkui::Cursor::Eraser: return IDC_NO;
    case vkui::Cursor::NotAllowed: return IDC_NO;
    default: return IDC_ARROW;
    }
}

// Dropped files and the file given on the command line ("open with"): projects, audio files and MIDI files are all accepted.
void importDropped(App& a, const std::string& path) { a.studio->openAny(path); }

LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    App* a = gApp;
    if (!a || !a->studio) return DefWindowProcW(h, m, w, l);
    auto px = [&](LPARAM lp, bool x) { return float(x ? GET_X_LPARAM(lp) : GET_Y_LPARAM(lp)) / std::max(0.75f, a->dpiScale * a->userScale); };
    auto feed = [&] { a->gui.setMods(mods()); a->dirty = true; };
    switch (m) {
    case WM_SIZE:
        if (w == SIZE_MINIMIZED) return 0;
        a->dirty = true;
        renderNow(*a);   // draw at the new size right now, not later in the main loop (which is blocked while the edge is dragged)
        return 0;
    case WM_ENTERSIZEMOVE: a->dirty = true; return 0;
    case WM_EXITSIZEMOVE: a->dirty = true; return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); EndPaint(h, &ps); renderNow(*a); return 0; }
    case WM_DPICHANGED: {
        a->dpiScale = float(HIWORD(w)) / 96.f;
        const RECT* r = reinterpret_cast<const RECT*>(l);
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        a->dirty = true;
        return 0;
    }
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT) { SetCursor(LoadCursorW(nullptr, cursorFor(a->gui.cursor()))); return TRUE; }
        break;
    case WM_TIMER:
        if (w == 2) { if (a->session.isDirty() && !a->session.isRecording()) a->session.autosave(); return 0; }
        if (a->out && a->out->failed()) a->studio->message("Audio device lost - restart MiniDAW");
        return 0;
    case WM_MOUSEMOVE:
        if (!a->mouseTracked) { TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, h, 0}; TrackMouseEvent(&t); a->mouseTracked = true; }
        a->gui.onMouseMove(px(l, true), px(l, false)); feed(); return 0;
    case WM_MOUSELEAVE: a->mouseTracked = false; a->gui.onMouseLeave(); a->dirty = true; return 0;
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: SetCapture(h); a->gui.onMouseMove(px(l, true), px(l, false)); a->gui.setMods(mods()); a->gui.onMouseButton(0, true, nowSeconds(*a)); a->dirty = true; return 0;
    case WM_LBUTTONUP: ReleaseCapture(); a->gui.onMouseMove(px(l, true), px(l, false)); a->gui.onMouseButton(0, false); feed(); return 0;
    case WM_RBUTTONDOWN: a->gui.onMouseMove(px(l, true), px(l, false)); a->gui.setMods(mods()); a->gui.onMouseButton(1, true, nowSeconds(*a)); a->dirty = true; return 0;
    case WM_RBUTTONUP: a->gui.onMouseButton(1, false); feed(); return 0;
    case WM_MBUTTONDOWN: a->gui.onMouseButton(2, true); feed(); return 0;
    case WM_MBUTTONUP: a->gui.onMouseButton(2, false); feed(); return 0;
    case WM_MOUSEWHEEL: a->gui.setMods(mods()); a->gui.onWheel(0, float(GET_WHEEL_DELTA_WPARAM(w)) / float(WHEEL_DELTA)); a->dirty = true; return 0;
    case WM_MOUSEHWHEEL: a->gui.onWheel(float(GET_WHEEL_DELTA_WPARAM(w)) / float(WHEEL_DELTA), 0); a->dirty = true; return 0;
    case WM_KEYDOWN: case WM_SYSKEYDOWN: {
        if (m == WM_SYSKEYDOWN && (w == VK_F4 || w == VK_SPACE)) break;   // Alt+F4 and the system menu stay with Windows
        const int k = mapKey(w);
        if (k) a->gui.onKey(k, mods(), true, (l & (1 << 30)) != 0);
        feed();
        return 0;
    }
    case WM_KEYUP: case WM_SYSKEYUP: { const int k = mapKey(w); if (k) a->gui.onKey(k, mods(), false, false); feed(); return m == WM_SYSKEYUP ? 0 : 0; }
    case WM_CHAR: {
        if (w >= 32 && w != 127 && !(GetKeyState(VK_CONTROL) < 0 && GetKeyState(VK_MENU) >= 0)) {
            static wchar_t high = 0;
            std::wstring ws;
            if (w >= 0xD800 && w <= 0xDBFF) { high = wchar_t(w); return 0; }
            if (high) { ws.push_back(high); high = 0; }
            ws.push_back(wchar_t(w));
            a->gui.onText(narrow(ws));
            a->dirty = true;
        }
        return 0;
    }
    case WM_IME_STARTCOMPOSITION: return 0;   // the composition is drawn inside the text field, not in the system's own window
    case WM_IME_COMPOSITION: {
        if (HIMC c = ImmGetContext(h)) {
            if (l & GCS_RESULTSTR) { const std::wstring r = imeString(c, GCS_RESULTSTR); if (!r.empty()) a->gui.onText(narrow(r)); a->gui.setPreedit({}); }
            if (l & GCS_COMPSTR) a->gui.setPreedit(narrow(imeString(c, GCS_COMPSTR)));
            ImmReleaseContext(h, c);
        }
        a->dirty = true;
        return 0;   // handled here: no WM_CHAR for the result
    }
    case WM_IME_ENDCOMPOSITION: a->gui.setPreedit({}); a->dirty = true; return 0;
    case WM_KILLFOCUS: a->gui.setMods(0); return 0;
    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(w);
        const UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < n; ++i) {
            wchar_t buf[MAX_PATH * 4];
            DragQueryFileW(drop, i, buf, UINT(std::size(buf)));
            importDropped(*a, narrow(buf));
        }
        DragFinish(drop);
        a->dirty = true;
        return 0;
    }
    case WM_CLOSE:
        if (a->session.isDirty()) {
            const int r = MessageBoxW(h, L"Save the project before closing?", L"MiniDAW", MB_YESNOCANCEL | MB_ICONQUESTION);
            if (r == IDCANCEL) return 0;
            if (r == IDYES) {
                std::string p = a->session.projectPath(), err;
                if (p.empty()) p = fileDialog(h, "Save Project", "MiniDAW project|*.mdaw", true);
                if (p.empty()) return 0;
                if (p.find('.') == std::string::npos) p += ".mdaw";
                if (!a->session.saveProject(p, &err)) { MessageBoxW(h, widen("Could not save: " + err).c_str(), L"MiniDAW", MB_OK | MB_ICONERROR); return 0; }
            }
        }
        if (a->studio) {   // remember the layout (and create the settings file if it is not there yet)
            for (const auto& kv : a->studio->layoutSettings()) a->settings[kv.first] = kv.second;
            saveSettings(*a);
        }
        DestroyWindow(h);
        return 0;
    case kJobMessage: {   // also reached from inside modal dialogs and menus
        std::unique_ptr<Job> job(reinterpret_cast<Job*>(l));
        job->fn();
        a->dirty = true;
        return 0;
    }
    case kWake: a->dirty = true; return 0;
    case WM_APP + 2: a->studio->message("Plug-in scan finished."); a->dirty = true; return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default: break;
    }
    return DefWindowProcW(h, m, w, l);
}

std::string getClipboardText(HWND h) {
    std::string s;
    if (!OpenClipboard(h)) return s;
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(data))) { s = narrow(p); GlobalUnlock(data); }
    }
    CloseClipboard();
    return s;
}
void setClipboardText(HWND h, const std::string& text) {
    const std::wstring w = widen(text);
    if (!OpenClipboard(h)) return;
    EmptyClipboard();
    if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t))) {
        std::memcpy(GlobalLock(mem), w.c_str(), (w.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(mem);
        SetClipboardData(CF_UNICODETEXT, mem);
    }
    CloseClipboard();
}

}  // namespace

int main() {
    // sharp text on every monitor: ask for per-monitor DPI awareness before any window exists
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        using SetCtx = BOOL(WINAPI*)(HANDLE);
        if (auto fn = reinterpret_cast<SetCtx>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"))) fn(reinterpret_cast<HANDLE>(-4));   // PER_MONITOR_AWARE_V2
    }
    App app;
    gApp = &app;
    QueryPerformanceFrequency(&app.freq);
    QueryPerformanceCounter(&app.start);
    app.fonts = std::make_shared<vkui::Fonts>(std::make_unique<GdiFontSource>());
    app.fonts->setFrameBudgetMs(8);   // a page of new Korean / Japanese glyphs must not freeze the window: the rest follows in the next frames

    std::shared_ptr<core::TranslationTable> table;
    {
        wchar_t exe[MAX_PATH * 2];
        GetModuleFileNameW(nullptr, exe, DWORD(std::size(exe)));
        std::string path = narrow(exe);
        app.exeDir = path.substr(0, path.find_last_of("/\\"));
        const core::AppPaths paths = core::resolveAppPaths(app.exeDir, envDir("LOCALAPPDATA"), [](const std::string& f) { return GetFileAttributesW(widen(f).c_str()) != INVALID_FILE_ATTRIBUTES; });
        app.dataDir = paths.dataDir;   // "<exe folder>\data" in a portable copy, else %LOCALAPPDATA%\MiniDAW
        app.portable = paths.portable;
        CreateDirectoryW(widen(app.dataDir).c_str(), nullptr);
        app.settingsPath = app.dataDir + "\\vulkan-settings.ini";
        loadSettings(app);
        if (GetFileAttributesW(widen(app.settingsPath).c_str()) == INVALID_FILE_ATTRIBUTES) saveSettings(app);   // first start: create the file
        const auto languages = core::findLanguages(app.exeDir + "\\translations");
        std::string code = app.settings.count("language") ? app.settings["language"] : "auto";
        if (code == "auto") {
            wchar_t loc[LOCALE_NAME_MAX_LENGTH] = {};
            GetUserDefaultLocaleName(loc, LOCALE_NAME_MAX_LENGTH);
            std::string l = narrow(loc);
            std::replace(l.begin(), l.end(), '-', '_');
            code = core::pickLanguage(languages, l);
        }
        for (const auto& lang : languages)
            if (lang.code == code) {
                auto t = std::make_shared<core::TranslationTable>();
                if (t->load(lang.path)) { table = t; app.session.setTranslations(t); }
            }
    }

    core::Session::Hooks hooks;
    hooks.changed = [&app](bool) { if (app.studio) app.studio->invalidate(); if (app.hwnd) PostMessageW(app.hwnd, kWake, 0, 0); };
    hooks.message = [&app](const std::string& s) { if (app.studio) app.studio->message(s); };
    hooks.encodeMp3 = [](const std::vector<float>& pcm, int rate, int kbps, const std::string& path, std::string* err) { return platform::encodeMp3(pcm, rate, kbps, path, err); };
    hooks.decode = [&app](const std::string& path, core::Session::DecodeDone done) {
        ++app.decoding;
        std::thread([&app, path, done = std::move(done)]() mutable {
            auto data = std::make_shared<core::AudioData>();
            const size_t slash = path.find_last_of("/\\");
            data->path = path;
            data->name = path.substr(slash == std::string::npos ? 0 : slash + 1, path.find_last_of('.') - (slash == std::string::npos ? 0 : slash + 1));
            std::string err;
            const bool ok = platform::decodeFile(path, data->pcm, &err) && data->frames() > 0;
            if (ok) core::computePeaks(*data);
            auto* job = new Job{[done = std::move(done), ok, data, err]() mutable { done(ok ? data : nullptr, ok ? std::string() : (err.empty() ? "Could not decode " + data->name : err)); }};
            PostMessageW(app.hwnd, kJobMessage, 0, reinterpret_cast<LPARAM>(job));
            --app.decoding;
        }).detach();
    };
    hooks.startInput = [&app] {
        std::string e;
        app.in = platform::openInput("", [&app](const float* st, int n) { app.session.pushRecorded(std::vector<float>(st, st + size_t(n) * 2)); }, &e);
        if (!app.in && app.studio) app.studio->message("Could not open the microphone: " + e);
    };
    hooks.stopInput = [&app] { app.in.reset(); };
    hooks.makeEffect = [&app](const std::string& id) -> std::shared_ptr<dsp::Effect> {
        if (id.rfind("vst3:", 0) == 0) { std::string e; auto fx = vst3n::createEffect(id, &e); if (!fx && app.studio) app.studio->message("Could not load the plug-in: " + e); return fx; }
        return platform::createNativePlugin(id);
    };
    hooks.makeInstrument = [&app](const std::string& id) -> std::shared_ptr<dsp::Instrument> {
        std::string e;
        auto inst = vst3n::createInstrument(id, &e);
        if (!inst && app.studio) app.studio->message("Could not load the plug-in: " + e);
        return inst;
    };
    hooks.saveState = [](dsp::ParamSet& p) { return vst3n::saveState(p); };
    hooks.loadState = [](dsp::ParamSet& p, const std::string& s) { vst3n::loadState(p, s); };
    app.session.setHooks(std::move(hooks));

    vkui::Host host;
    host.fileDialog = [&app](const std::string& t, const std::string& f, bool save) { return fileDialog(app.hwnd, t, f, save); };
    host.folderDialog = [&app](const std::string& t) { return folderDialog(app.hwnd, t); };
    host.quit = [&app] { PostMessageW(app.hwnd, WM_CLOSE, 0, 0); };
    host.setTitle = [&app](const std::string& t) { SetWindowTextW(app.hwnd, widen(t).c_str()); };
    host.loopFolder = app.settings.count("loops") && !app.settings["loops"].empty() ? app.settings["loops"] : app.exeDir + "\\loops";
    host.settings = app.settings;
    host.dataDir = app.dataDir;
    host.iconFontDirs = {app.exeDir + "\\fonts", app.dataDir + "\\fonts"};
    host.portable = app.portable;
    // The device lists live in a cache that a worker thread fills (reading them can block for seconds on some drivers); Preferences only reads the cache.
    struct DeviceCache {
        std::mutex m;
        std::vector<std::pair<std::string, std::string>> outs, ins;
        std::vector<std::string> midis;
        bool ready = false;
        std::atomic<bool> busy{false};
    };
    auto devices = std::make_shared<DeviceCache>();
    host.refreshDevices = [devices] {
        if (devices->busy.exchange(true)) return;   // already reading
        std::thread([devices] {
            std::vector<std::pair<std::string, std::string>> o, i;
            for (const auto& d : platform::audioOutputs()) o.emplace_back(d.id, d.name);
            for (const auto& d : platform::audioInputs()) i.emplace_back(d.id, d.name);
            std::vector<std::string> mi = platform::midiInputs();
            {
                std::lock_guard<std::mutex> l(devices->m);
                devices->outs = std::move(o); devices->ins = std::move(i); devices->midis = std::move(mi); devices->ready = true;
            }
            devices->busy = false;
        }).detach();
    };
    host.devicesReady = [devices] { std::lock_guard<std::mutex> l(devices->m); return devices->ready; };
    host.audioDevices = [devices](bool input) { std::lock_guard<std::mutex> l(devices->m); return input ? devices->ins : devices->outs; };
    host.midiDevices = [devices] { std::lock_guard<std::mutex> l(devices->m); return devices->midis; };
    host.refreshDevices();   // at the start, so the lists are there when Preferences is opened
    host.plugins = [&app] { return pluginEntries(app); };
    host.rescanPlugins = [&app] { rescanPlugins(app); };
    host.openPluginEditor = [&app](const std::shared_ptr<dsp::ParamSet>& p) { return vst3n::openEditor(p, app.hwnd); };
    host.languages = [&app] {
        std::vector<std::pair<std::string, std::string>> v;
        for (const auto& l : core::findLanguages(app.exeDir + "\\translations")) v.emplace_back(l.code, l.name);
        return v;
    };
    host.storeSettings = [&app](const std::map<std::string, std::string>& s) { for (const auto& kv : s) app.settings[kv.first] = kv.second; saveSettings(app); };
    host.applySettings = [&app](const std::map<std::string, std::string>& s) { app.settings = s; saveSettings(app); applySettings(app); };
    app.studio = std::make_unique<vkui::Studio>(app.session, host);
    if (table) app.studio->setTranslations(table);
    app.gui.clipboardGet = [&app] { return getClipboardText(app.hwnd); };
    app.gui.clipboardSet = [&app](const std::string& s) { setClipboardText(app.hwnd, s); };

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"MiniDAWVulkanWindow";
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
    RegisterClassExW(&wc);
    const std::wstring title = widen(std::string("MiniDAW ") + MINIDAW_VERSION + " (Vulkan)");
    app.hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, wc.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 1440, 900, nullptr, nullptr, wc.hInstance, nullptr);
    if (!app.hwnd) return 1;
    DragAcceptFiles(app.hwnd, TRUE);
    app.dpiScale = float(GetDpiForWindow(app.hwnd)) / 96.f;
    applySettings(app);

    app.renderer = std::make_unique<vkui::VkRenderer>();
    {
        RECT rc;
        GetClientRect(app.hwnd, &rc);
        app.renderer->resize(rc.right, rc.bottom);
        std::string err;
        if (!app.renderer->init(wc.hInstance, app.hwnd, app.exeDir + "\\shaders", err)) {
            const std::wstring text = widen("The Vulkan interface could not start:\n\n" + err + "\n\nStart the Native version (MiniDAW.exe) instead?");
            if (MessageBoxW(app.hwnd, text.c_str(), L"MiniDAW (Vulkan)", MB_YESNO | MB_ICONWARNING) == IDYES) ShellExecuteW(nullptr, L"open", widen(app.exeDir + "\\MiniDAW.exe").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 1;
        }
    }

    app.session.setAutosaveDir(app.dataDir + "\\autosave");
    CreateDirectoryW(widen(app.dataDir + "\\autosave").c_str(), nullptr);
    const std::string lock = app.dataDir + "\\autosave\\running-vulkan.lock";
    if (GetFileAttributesW(widen(lock).c_str()) != INVALID_FILE_ATTRIBUTES && app.session.hasAutosave() &&
        MessageBoxW(app.hwnd, L"MiniDAW did not close properly last time. Restore the last autosaved project?", L"MiniDAW", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        std::string e;
        if (app.session.loadProject(app.session.autosavePath(), &e)) { app.session.markRecovered(); app.studio->message("Restored the autosaved project."); }
    }
    { std::ofstream(lock) << "running"; }
    rescanPlugins(app);
    {
        int argc = 0;
        wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv && argc >= 2) app.studio->openAny(narrow(argv[1]));
        if (argv) LocalFree(argv);
    }
    SetTimer(app.hwnd, 1, 250, nullptr);

    bool running = true;
    while (running) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running = false; break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running) break;
        if (app.dirty || app.studio->wantsFrame() || app.gui.needsFrame()) {
            app.dirty = false;
            renderFrame(app);
            if (app.studio->quitRequested()) PostMessageW(app.hwnd, WM_CLOSE, 0, 0);
        } else {
            MsgWaitForMultipleObjectsEx(0, nullptr, 100, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
        if (IsIconic(app.hwnd)) MsgWaitForMultipleObjectsEx(0, nullptr, 100, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
    app.in.reset();
    app.midi.reset();
    app.out.reset();
    app.renderer.reset();
    app.session.clearAutosave();
    DeleteFileW(widen(lock).c_str());
    for (int i = 0; i < 400 && app.decoding > 0; ++i) Sleep(5);
    return 0;
}
