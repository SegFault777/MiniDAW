// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Windows shell of the Qt-free user interface (v0.9): one window, GDI text, WASAPI audio through src/platform.
// Everything here only moves pixels and input events between Windows and ui::NativeUi.
// windows.h must not define the min / max macros: dsp/Param.h has members called min and max.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "../core/Session.h"
#include "../platform/Audio.h"
#include "../core/I18n.h"
#include "../platform/Decode.h"
#include "../platform/Midi.h"
#include "../ui/Ui.h"

#ifndef MINIDAW_VERSION
#define MINIDAW_VERSION "0.10_1"
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

// ---- text: GDI renders glyph coverage, the canvas blends it (all of Unicode, system fonts) ----
class GdiText : public ui::TextRenderer {
public:
    GdiText() {
        m_dc = CreateCompatibleDC(nullptr);
        const struct { int px, weight; } spec[4] = {{-12, FW_NORMAL}, {-14, FW_NORMAL}, {-12, FW_BOLD}, {-14, FW_BOLD}};
        for (int i = 0; i < 4; ++i)
            m_font[i] = CreateFontW(spec[i].px, 0, 0, 0, spec[i].weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }
    ~GdiText() override { for (HFONT f : m_font) DeleteObject(f); DeleteDC(m_dc); }

    int width(const std::string& s, ui::Font f) override { return glyphs(s, f).w; }
    int lineHeight(ui::Font f) override {
        SelectObject(m_dc, m_font[int(f)]);
        TEXTMETRICW tm{};
        GetTextMetricsW(m_dc, &tm);
        return tm.tmHeight + 1;
    }
    void draw(ui::Canvas& c, int x, int y, const std::string& s, ui::Rgba color, ui::Font f) override {
        const Bitmap& b = glyphs(s, f);
        for (int row = 0; row < b.h; ++row)
            for (int col = 0; col < b.w; ++col) {
                const int cov = b.cov[size_t(row) * size_t(b.w) + size_t(col)];
                if (cov) c.blend(x + col, y + row, color, cov);
            }
    }

private:
    struct Bitmap { int w = 0, h = 0; std::vector<unsigned char> cov; };

    const Bitmap& glyphs(const std::string& s, ui::Font f) {
        const std::string key = char('0' + int(f)) + s;
        auto it = m_cache.find(key);
        if (it != m_cache.end()) return it->second;
        if (m_cache.size() > 4000) m_cache.clear();
        Bitmap bm;
        const std::wstring w = widen(s);
        SelectObject(m_dc, m_font[int(f)]);
        SIZE sz{};
        GetTextExtentPoint32W(m_dc, w.c_str(), int(w.size()), &sz);
        bm.w = sz.cx;
        bm.h = sz.cy + 1;
        if (bm.w > 0 && bm.h > 0) {
            BITMAPINFO bi{};
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = bm.w;
            bi.bmiHeader.biHeight = -bm.h;
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            void* bits = nullptr;
            HBITMAP dib = CreateDIBSection(m_dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (dib && bits) {
                HGDIOBJ old = SelectObject(m_dc, dib);
                SelectObject(m_dc, m_font[int(f)]);
                std::fill_n(static_cast<unsigned char*>(bits), size_t(bm.w) * size_t(bm.h) * 4, (unsigned char)0);
                SetBkMode(m_dc, TRANSPARENT);
                SetTextColor(m_dc, RGB(255, 255, 255));
                TextOutW(m_dc, 0, 0, w.c_str(), int(w.size()));
                GdiFlush();
                bm.cov.resize(size_t(bm.w) * size_t(bm.h));
                const unsigned char* px = static_cast<const unsigned char*>(bits);
                for (size_t i = 0; i < bm.cov.size(); ++i) bm.cov[i] = px[i * 4 + 1];   // grey-scale anti-aliasing: any channel is the coverage
                SelectObject(m_dc, old);
            }
            if (dib) DeleteObject(dib);
        }
        return m_cache.emplace(key, std::move(bm)).first->second;
    }

    HDC m_dc = nullptr;
    HFONT m_font[4] = {};
    std::unordered_map<std::string, Bitmap> m_cache;
};

// ---- the application ----
struct App {
    core::Session session;
    std::unique_ptr<ui::NativeUi> ui;
    ui::Canvas canvas;
    std::unique_ptr<GdiText> text;
    std::unique_ptr<platform::AudioStream> out, in;
    std::unique_ptr<platform::MidiPort> midi;
    HWND hwnd = nullptr;
    std::atomic<int> decoding{0};
    std::string device;
    std::map<std::string, std::string> settings;
    std::string settingsPath, dataDir, exeDir;
    int autosaveMinutes = 2;
};
App* gApp = nullptr;
constexpr UINT kJobMessage = WM_APP + 1;   // a worker thread hands a function to the UI thread
struct Job { std::function<void()> fn; };

int mods() {
    return (GetKeyState(VK_CONTROL) < 0 ? ui::ModCtrl : 0) | (GetKeyState(VK_SHIFT) < 0 ? ui::ModShift : 0) | (GetKeyState(VK_MENU) < 0 ? ui::ModAlt : 0);
}

int mapKey(WPARAM vk) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return int(vk);
    switch (vk) {
    case VK_SPACE: return ui::KeySpace;
    case VK_DELETE: return ui::KeyDelete;
    case VK_BACK: return ui::KeyBackspace;
    case VK_LEFT: return ui::KeyLeft;
    case VK_RIGHT: return ui::KeyRight;
    case VK_UP: return ui::KeyUp;
    case VK_DOWN: return ui::KeyDown;
    case VK_HOME: return ui::KeyHome;
    case VK_END: return ui::KeyEnd;
    case VK_RETURN: return ui::KeyEnter;
    case VK_ESCAPE: return ui::KeyEscape;
    case VK_F2: return ui::KeyF2;
    case VK_TAB: return '\t';
    case VK_OEM_COMMA: return ',';
    case VK_OEM_4: return '[';
    case VK_OEM_6: return ']';
    case VK_OEM_PLUS: case VK_ADD: return ui::KeyPlus;
    case VK_OEM_MINUS: case VK_SUBTRACT: return ui::KeyMinus;
    default: return 0;
    }
}

std::string fileDialog(HWND owner, const std::string& title, const std::string& filter, bool save) {
    // "Name|*.a;*.b" -> "Name\0*.a;*.b\0All files\0*.*\0\0"
    std::wstring f = widen(filter);
    std::replace(f.begin(), f.end(), L'|', L'\0');
    f.push_back(L'\0');
    f += L"All files";
    f.push_back(L'\0');
    f += L"*.*";
    f.push_back(L'\0');
    f.push_back(L'\0');
    wchar_t path[MAX_PATH * 4] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = f.c_str();
    ofn.lpstrFile = path;
    ofn.nMaxFile = DWORD(std::size(path));
    const std::wstring t = widen(title);
    ofn.lpstrTitle = t.c_str();
    ofn.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST));
    const BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    return ok ? narrow(path) : std::string();
}

void importDropped(App& a, const std::string& path) {
    a.ui->message("Importing " + path);
    a.session.importAudio(path, -1, a.session.positionBeats());
}

LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    App* a = gApp;
    if (!a) return DefWindowProcW(h, m, w, l);
    switch (m) {
    case WM_SIZE: {
        const int cw = LOWORD(l), ch = HIWORD(l);
        if (cw > 0 && ch > 0) { a->canvas.resize(cw, ch); a->ui->resize(cw, ch); InvalidateRect(h, nullptr, FALSE); }
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        a->ui->paint(a->canvas);
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = a->canvas.width();
        bi.bmiHeader.biHeight = -a->canvas.height();
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        SetDIBitsToDevice(dc, 0, 0, DWORD(a->canvas.width()), DWORD(a->canvas.height()), 0, 0, 0, UINT(a->canvas.height()), a->canvas.pixels(), &bi, DIB_RGB_COLORS);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_TIMER:
        if (w == 2) {   // autosave
            if (a->session.isDirty() && !a->session.isRecording()) a->session.autosave();
            return 0;
        }
        if (a->out && a->out->failed()) a->ui->setDeviceStatus("Audio device lost - restart MiniDAW");
        if (a->ui->tick()) InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN: SetCapture(h); a->ui->mouseDown(GET_X_LPARAM(l), GET_Y_LPARAM(l), 0, mods()); InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_LBUTTONDBLCLK: SetCapture(h); a->ui->mouseDown(GET_X_LPARAM(l), GET_Y_LPARAM(l), 0, mods()); a->ui->doubleClick(GET_X_LPARAM(l), GET_Y_LPARAM(l)); InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_LBUTTONUP: ReleaseCapture(); a->ui->mouseUp(GET_X_LPARAM(l), GET_Y_LPARAM(l), 0, mods()); InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_RBUTTONDOWN: a->ui->mouseDown(GET_X_LPARAM(l), GET_Y_LPARAM(l), 1, mods()); InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_RBUTTONUP: a->ui->mouseUp(GET_X_LPARAM(l), GET_Y_LPARAM(l), 1, mods()); return 0;
    case WM_MOUSEMOVE: a->ui->mouseMove(GET_X_LPARAM(l), GET_Y_LPARAM(l), mods()); InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: {
        POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        ScreenToClient(h, &p);
        const double d = double(GET_WHEEL_DELTA_WPARAM(w)) / WHEEL_DELTA;
        if (m == WM_MOUSEWHEEL) a->ui->mouseWheel(p.x, p.y, 0, d, mods());
        else a->ui->mouseWheel(p.x, p.y, d, 0, mods());
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_KEYDOWN: case WM_SYSKEYDOWN: {
        const int k = mapKey(w);
        const bool repeat = (l & (1 << 30)) != 0;
        const bool repeatable = k == ui::KeyLeft || k == ui::KeyRight || k == ui::KeyUp || k == ui::KeyDown || k == ui::KeyDelete || k == ui::KeyBackspace || k == ui::KeyPlus || k == ui::KeyMinus;
        if (k && repeat && !repeatable) return 0;   // holding Space must not toggle playback again and again
        if (k && a->ui->key(k, mods())) { InvalidateRect(h, nullptr, FALSE); return 0; }
        break;
    }
    case WM_KEYUP: case WM_SYSKEYUP: {   // ends the notes of musical typing
        const int k = mapKey(w);
        if (k && a->ui->keyUp(k)) return 0;
        break;
    }
    case WM_CHAR: {
        if (w >= 32 && w != 127) {
            static wchar_t high = 0;
            std::wstring ws;
            if (w >= 0xD800 && w < 0xDC00) { high = wchar_t(w); return 0; }
            if (high) { ws.push_back(high); high = 0; }
            ws.push_back(wchar_t(w));
            a->ui->text(narrow(ws));
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    }
    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(w);
        const UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < n; ++i) {
            wchar_t buf[MAX_PATH * 4];
            DragQueryFileW(drop, i, buf, UINT(std::size(buf)));
            importDropped(*a, narrow(buf));
        }
        DragFinish(drop);
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
        DestroyWindow(h);
        return 0;
    case kJobMessage: {   // also reached from inside modal dialogs and menus, so it is handled here and not in the main loop
        std::unique_ptr<Job> job(reinterpret_cast<Job*>(l));
        job->fn();
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default: break;
    }
    return DefWindowProcW(h, m, w, l);
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
    std::ofstream out(a.settingsPath, std::ios::trunc);
    for (const auto& kv : a.settings) out << kv.first << '=' << kv.second << '\n';
}

void openAudio(App& a) {
    a.out.reset();
    std::string err;
    const std::string id = a.settings.count("output") ? a.settings["output"] : std::string();
    const int buffer = a.settings.count("buffer") ? std::max(128, std::atoi(a.settings["buffer"].c_str())) : 2048;
    a.out = platform::openOutput(id, buffer, [&a](std::int16_t* o, int n) { a.session.fillInt16(o, n); }, &err);
    if (!a.out && !id.empty()) a.out = platform::openOutput("", buffer, [&a](std::int16_t* o, int n) { a.session.fillInt16(o, n); }, &err);   // the saved device is gone
    a.ui->setDeviceStatus(a.out ? a.out->status() : "No audio output: " + err);
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
                const int track = a.ui ? a.ui->selectedTrack() : -1;
                if (track < 0) return;
                if (cmd == 0x90 && d2 > 0) a.session.noteOn(track, d1, d2);
                else if (cmd == 0x80 || (cmd == 0x90 && d2 == 0)) a.session.noteOff(track, d1);
            });
            return;
        }
}

void applySettings(App& a, bool first) {
    a.autosaveMinutes = a.settings.count("autosave") ? std::max(0, std::atoi(a.settings["autosave"].c_str())) : 2;
    if (a.settings.count("recOffset")) a.session.setRecordingOffsetMs(std::atoi(a.settings["recOffset"].c_str()));
    if (a.autosaveMinutes > 0 && a.hwnd) SetTimer(a.hwnd, 2, UINT(a.autosaveMinutes) * 60000u, nullptr);
    else if (a.hwnd) KillTimer(a.hwnd, 2);
    openAudio(a);
    openMidi(a);
    (void)first;
}

}  // namespace

int main() {
    App app;
    gApp = &app;
    app.text = std::make_unique<GdiText>();
    ui::Canvas::setTextRenderer(app.text.get());

    // ---- folders, settings, language ----
    {
        wchar_t exe[MAX_PATH * 2];
        GetModuleFileNameW(nullptr, exe, DWORD(std::size(exe)));
        std::string path = narrow(exe);
        app.exeDir = path.substr(0, path.find_last_of("/\\"));
        const std::string appData = envDir("LOCALAPPDATA");
        app.dataDir = (appData.empty() ? app.exeDir : appData) + "\\MiniDAW";
        CreateDirectoryW(widen(app.dataDir).c_str(), nullptr);
        app.settingsPath = app.dataDir + "\\native-settings.ini";
        loadSettings(app);
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
                auto table = std::make_shared<core::TranslationTable>();
                if (table->load(lang.path)) { ui::Canvas::setTranslations(table); app.session.setTranslations(table); }
            }
    }

    // ---- session hooks: decoding on worker threads, microphone, messages ----
    core::Session::Hooks hooks;
    hooks.changed = [&app](bool) { if (app.ui) app.ui->invalidate(); };
    hooks.message = [&app](const std::string& s) { if (app.ui) app.ui->message(s); };
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
            // The session expects 'done' on the UI thread: hand it over with a window message.
            auto* job = new Job{[done = std::move(done), ok, data, err]() mutable { done(ok ? data : nullptr, ok ? std::string() : (err.empty() ? "Could not decode " + data->name : err)); }};
            PostMessageW(app.hwnd, kJobMessage, 0, reinterpret_cast<LPARAM>(job));
            --app.decoding;
        }).detach();
    };
    hooks.startInput = [&app] {
        std::string e;
        app.in = platform::openInput("", [&app](const float* st, int n) { app.session.pushRecorded(std::vector<float>(st, st + size_t(n) * 2)); }, &e);
        if (!app.in && app.ui) app.ui->message("Could not open the microphone: " + e);
    };
    hooks.stopInput = [&app] { app.in.reset(); };
    app.session.setHooks(std::move(hooks));

    ui::Host host;
    host.fileDialog = [&app](const std::string& t, const std::string& f, bool save) { return fileDialog(app.hwnd, t, f, save); };
    host.quit = [&app] { PostMessageW(app.hwnd, WM_CLOSE, 0, 0); };
    host.setTitle = [&app](const std::string& t) { SetWindowTextW(app.hwnd, widen(t).c_str()); };
    host.loopFolder = app.settings.count("loops") && !app.settings["loops"].empty() ? app.settings["loops"] : app.exeDir + "\\loops";
    host.settings = app.settings;
    host.audioDevices = [](bool input) {
        std::vector<std::pair<std::string, std::string>> v;
        for (const auto& d : input ? platform::audioInputs() : platform::audioOutputs()) v.emplace_back(d.id, d.name);
        return v;
    };
    host.midiDevices = [] { return platform::midiInputs(); };
    host.languages = [&app] {
        std::vector<std::pair<std::string, std::string>> v;
        for (const auto& l : core::findLanguages(app.exeDir + "\\translations")) v.emplace_back(l.code, l.name);
        return v;
    };
    host.applySettings = [&app](const std::map<std::string, std::string>& s) {
        app.settings = s;
        saveSettings(app);
        applySettings(app, false);
    };
    app.ui = std::make_unique<ui::NativeUi>(app.session, host);

    // ---- window ----
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"MiniDAWNativeWindow";
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
    RegisterClassExW(&wc);
    const std::wstring title = widen(std::string("MiniDAW ") + MINIDAW_VERSION);
    app.hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, wc.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 1360, 860, nullptr, nullptr, wc.hInstance, nullptr);
    if (!app.hwnd) return 1;
    DragAcceptFiles(app.hwnd, TRUE);

    // ---- audio ----
    app.session.setAutosaveDir(app.dataDir + "\\autosave");
    CreateDirectoryW(widen(app.dataDir + "\\autosave").c_str(), nullptr);
    const std::string lock = app.dataDir + "\\autosave\\running.lock";
    if (GetFileAttributesW(widen(lock).c_str()) != INVALID_FILE_ATTRIBUTES && app.session.hasAutosave() &&
        MessageBoxW(app.hwnd, L"MiniDAW did not close properly last time. Restore the last autosaved project?", L"MiniDAW", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        std::string e;
        if (app.session.loadProject(app.session.autosavePath(), &e)) { app.session.markRecovered(); app.ui->message("Restored the autosaved project."); }
    }
    { std::ofstream(lock) << "running"; }
    applySettings(app, true);
    SetTimer(app.hwnd, 1, 33, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    app.in.reset();
    app.midi.reset();
    app.out.reset();
    app.session.clearAutosave();
    DeleteFileW(widen(lock).c_str());
    for (int i = 0; i < 400 && app.decoding > 0; ++i) Sleep(5);
    return 0;
}
