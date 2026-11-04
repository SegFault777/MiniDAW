// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// VST3 host for the native (Qt-free) application, built on the hosting helpers of the Steinberg VST3 SDK
// (dual licensed GPL-3.0 / proprietary). Derived from src/vst3/Vst3Host.cpp with Qt replaced by the standard
// library and Win32. VST is a trademark of Steinberg Media Technologies GmbH.
#ifdef MINIDAW_VST3
#include "Vst3Native.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <string>

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace vst3n {
namespace {

constexpr int kMaxBlock = 2048;

IHostApplication* hostContext() {
    static HostApplication* host = new HostApplication;   // lives until the process exits
    return static_cast<IHostApplication*>(host);
}

// ---- Minimal COM-style helpers that the plug-in calls back into ----
class MemStream : public IBStream {
public:
    MemStream() = default;
    explicit MemStream(std::vector<unsigned char> d) : m_data(std::move(d)) {}
    const std::vector<unsigned char>& data() const { return m_data; }
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (FUnknownPrivate::iidEqual(iid, IBStream::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = static_cast<IBStream*>(this);
            addRef();
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++m_ref; }
    uint32 PLUGIN_API release() override { return --m_ref; }   // owned by value on the stack
    tresult PLUGIN_API read(void* buffer, int32 numBytes, int32* numBytesRead) override {
        const int32 n = std::min<int32>(numBytes, int32(m_data.size() - m_pos));
        if (n > 0) memcpy(buffer, m_data.data() + m_pos, size_t(n));
        m_pos += std::max<int32>(0, n);
        if (numBytesRead) *numBytesRead = std::max<int32>(0, n);
        return kResultOk;
    }
    tresult PLUGIN_API write(void* buffer, int32 numBytes, int32* numBytesWritten) override {
        if (size_t(m_pos + numBytes) > m_data.size()) m_data.resize(size_t(m_pos + numBytes));
        memcpy(m_data.data() + m_pos, buffer, size_t(numBytes));
        m_pos += numBytes;
        if (numBytesWritten) *numBytesWritten = numBytes;
        return kResultOk;
    }
    tresult PLUGIN_API seek(int64 pos, int32 mode, int64* result) override {
        int64 p = mode == kIBSeekSet ? pos : mode == kIBSeekCur ? m_pos + pos : m_data.size() + pos;
        m_pos = int(std::clamp<int64>(p, 0, m_data.size()));
        if (result) *result = m_pos;
        return kResultOk;
    }
    tresult PLUGIN_API tell(int64* pos) override {
        if (pos) *pos = m_pos;
        return kResultOk;
    }
private:
    std::vector<unsigned char> m_data;
    int m_pos = 0;
    uint32 m_ref = 1;
};

class EventList : public IEventList {
public:
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (FUnknownPrivate::iidEqual(iid, IEventList::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = static_cast<IEventList*>(this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 2; }
    uint32 PLUGIN_API release() override { return 1; }
    int32 PLUGIN_API getEventCount() override { return int32(m_events.size()); }
    tresult PLUGIN_API getEvent(int32 index, Event& e) override {
        if (index < 0 || index >= int32(m_events.size())) return kResultFalse;
        e = m_events[size_t(index)];
        return kResultTrue;
    }
    tresult PLUGIN_API addEvent(Event& e) override {
        m_events.push_back(e);
        return kResultTrue;
    }
    void clear() { m_events.clear(); }
private:
    std::vector<Event> m_events;
};

class NoParameterChanges : public IParameterChanges {
public:
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (FUnknownPrivate::iidEqual(iid, IParameterChanges::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = static_cast<IParameterChanges*>(this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 2; }
    uint32 PLUGIN_API release() override { return 1; }
    int32 PLUGIN_API getParameterCount() override { return 0; }
    IParamValueQueue* PLUGIN_API getParameterData(int32) override { return nullptr; }
    IParamValueQueue* PLUGIN_API addParameterData(const ParamID&, int32&) override { return nullptr; }
};

class ComponentHandler : public IComponentHandler {
public:
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (FUnknownPrivate::iidEqual(iid, IComponentHandler::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = static_cast<IComponentHandler*>(this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 2; }
    uint32 PLUGIN_API release() override { return 1; }
    tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID, ParamValue) override { return kResultOk; }
    tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32) override { return kResultOk; }
};

/// One loaded plug-in instance (component, processor and controller).
struct Plugin {
    VST3::Hosting::Module::Ptr module;
    IPtr<IComponent> component;
    IPtr<IAudioProcessor> processor;
    IPtr<IEditController> controller;
    ComponentHandler handler;
    EventList events;
    NoParameterChanges noParams;
    std::string id, name;
    bool hasInput = false, active = false, separateController = false;
    std::vector<float> inL, inR, outL, outR;

    ~Plugin() {
        if (processor && active) processor->setProcessing(false);
        if (component && active) component->setActive(false);
        if (controller) {
            controller->setComponentHandler(nullptr);
            if (separateController) controller->terminate();
        }
        if (component) component->terminate();
    }

    bool load(const std::string& path, const std::string& classId, std::string& error) {
        module = VST3::Hosting::Module::create(path, error);
        if (!module) return false;
        auto& factory = module->getFactory();
        const VST3::Hosting::ClassInfo* found = nullptr;
        std::vector<VST3::Hosting::ClassInfo> infos = factory.classInfos();
        for (const auto& ci : infos)
            if (ci.category() == kVstAudioEffectClass && ci.ID().toString() == classId) found = &ci;
        if (!found) { error = "Plug-in class not found."; return false; }
        name = found->name();
        component = factory.createInstance<IComponent>(found->ID());
        if (!component) { error = "Could not create the plug-in component."; return false; }
        if (component->initialize(hostContext()) != kResultOk) { error = "The plug-in failed to initialize."; return false; }

        IEditController* raw = nullptr;
        if (component->queryInterface(IEditController::iid, reinterpret_cast<void**>(&raw)) == kResultOk && raw) {
            controller = IPtr<IEditController>(raw, false);   // single-component plug-in
        } else {
            TUID cid;
            if (component->getControllerClassId(cid) == kResultOk) {
                IEditController* c = nullptr;
                factory.get()->createInstance(cid, IEditController::iid, reinterpret_cast<void**>(&c));
                if (c) {
                    controller = IPtr<IEditController>(c, false);
                    controller->initialize(hostContext());
                    separateController = true;
                }
            }
        }
        if (controller) {
            controller->setComponentHandler(&handler);
            FUnknownPtr<IConnectionPoint> a(component), b(controller);
            if (a && b) { a->connect(b); b->connect(a); }
        }
        IAudioProcessor* proc = nullptr;
        if (component->queryInterface(IAudioProcessor::iid, reinterpret_cast<void**>(&proc)) != kResultOk || !proc) {
            error = "The plug-in has no audio processor.";
            return false;
        }
        processor = IPtr<IAudioProcessor>(proc, false);

        const int32 inBuses = component->getBusCount(kAudio, kInput), outBuses = component->getBusCount(kAudio, kOutput);
        hasInput = inBuses > 0;
        SpeakerArrangement stereo = SpeakerArr::kStereo;
        if (hasInput) processor->setBusArrangements(&stereo, 1, &stereo, 1);
        else processor->setBusArrangements(nullptr, 0, &stereo, 1);
        for (int32 i = 0; i < inBuses; ++i) component->activateBus(kAudio, kInput, i, i == 0);
        for (int32 i = 0; i < outBuses; ++i) component->activateBus(kAudio, kOutput, i, i == 0);
        if (component->getBusCount(kEvent, kInput) > 0) component->activateBus(kEvent, kInput, 0, true);

        ProcessSetup setup{kRealtime, kSample32, kMaxBlock, 44100.0};
        if (processor->setupProcessing(setup) != kResultOk) { error = "The plug-in does not support 44.1 kHz / 32-bit processing."; return false; }
        component->setActive(true);
        processor->setProcessing(true);
        active = true;
        inL.assign(kMaxBlock, 0.f); inR.assign(kMaxBlock, 0.f);
        outL.assign(kMaxBlock, 0.f); outR.assign(kMaxBlock, 0.f);
        return true;
    }

    /// Processes interleaved stereo. Effects replace the buffer; instruments add their output to it.
    void run(float* io, int frames, bool effect) {
        for (int off = 0; off < frames; off += kMaxBlock) {
            const int n = std::min(kMaxBlock, frames - off);
            if (effect)
                for (int i = 0; i < n; ++i) { inL[size_t(i)] = io[2 * (off + i)]; inR[size_t(i)] = io[2 * (off + i) + 1]; }
            float* inPtrs[2] = {inL.data(), inR.data()};
            float* outPtrs[2] = {outL.data(), outR.data()};
            AudioBusBuffers in{}, out{};
            in.numChannels = 2;
            in.channelBuffers32 = inPtrs;
            out.numChannels = 2;
            out.channelBuffers32 = outPtrs;
            ProcessData d{};
            d.processMode = kRealtime;
            d.symbolicSampleSize = kSample32;
            d.numSamples = n;
            d.numInputs = hasInput ? 1 : 0;
            d.inputs = hasInput ? &in : nullptr;
            d.numOutputs = 1;
            d.outputs = &out;
            d.inputEvents = &events;
            d.inputParameterChanges = &noParams;
            processor->process(d);
            events.clear();   // events apply to the first chunk only
            for (int i = 0; i < n; ++i) {
                if (effect) { io[2 * (off + i)] = outL[size_t(i)]; io[2 * (off + i) + 1] = outR[size_t(i)]; }
                else { io[2 * (off + i)] += outL[size_t(i)]; io[2 * (off + i) + 1] += outR[size_t(i)]; }
            }
        }
    }
};

std::shared_ptr<Plugin> loadPlugin(const std::string& id, std::string* error) {
    // id = "vst3:<module path>|<class id>"
    const std::string rest = id.size() > 5 ? id.substr(5) : std::string();
    const size_t bar = rest.rfind('|');
    if (bar == std::string::npos) { if (error) *error = "Invalid plug-in id."; return nullptr; }
    auto p = std::make_shared<Plugin>();
    std::string err;
    if (!p->load(rest.substr(0, bar), rest.substr(bar + 1), err)) {
        if (error) *error = err;
        return nullptr;
    }
    p->id = id;
    return p;
}

class Vst3Effect : public dsp::Effect {
public:
    explicit Vst3Effect(std::shared_ptr<Plugin> p) : plugin(std::move(p)) {}
    const char* id() const override { return plugin->id.c_str(); }
    const char* displayName() const override { return plugin->name.c_str(); }
    void process(float* io, int frames) override { plugin->run(io, frames, true); }
    std::shared_ptr<Plugin> plugin;
};

class Vst3Instrument : public dsp::Instrument {
public:
    explicit Vst3Instrument(std::shared_ptr<Plugin> p) : plugin(std::move(p)) {}
    void noteOn(int note, int velocity) override {
        Event e{};
        e.type = Event::kNoteOnEvent;
        e.noteOn.channel = 0;
        e.noteOn.pitch = int16(note);
        e.noteOn.velocity = float(velocity) / 127.f;
        e.noteOn.noteId = -1;
        e.noteOn.length = 0;
        e.noteOn.tuning = 0.f;
        e.sampleOffset = 0;
        plugin->events.addEvent(e);
        m_held.insert(note);
    }
    void noteOff(int note) override {
        Event e{};
        e.type = Event::kNoteOffEvent;
        e.noteOff.channel = 0;
        e.noteOff.pitch = int16(note);
        e.noteOff.velocity = 0.f;
        e.noteOff.noteId = -1;
        e.noteOff.tuning = 0.f;
        e.sampleOffset = 0;
        plugin->events.addEvent(e);
        m_held.erase(note);
    }
    void allNotesOff() override {
        const std::set<int> held = m_held;
        for (int n : held) noteOff(n);
    }
    void reset() override { allNotesOff(); }
    void render(float* io, int frames) override { plugin->run(io, frames, false); }
    const char* kind() const override { return plugin->id.c_str(); }
    std::shared_ptr<Plugin> plugin;
private:
    std::set<int> m_held;
};

Plugin* pluginOf(const dsp::ParamSet& set) {
    if (auto* e = dynamic_cast<const Vst3Effect*>(&set)) return e->plugin.get();
    if (auto* i = dynamic_cast<const Vst3Instrument*>(&set)) return i->plugin.get();
    return nullptr;
}

// ---- the editor window: a plain Win32 window that hosts the plug-in's IPlugView ----
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(std::max(n, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

struct EditorHost : IPlugFrame {
    HWND hwnd = nullptr;
    Plugin* key = nullptr;
    std::shared_ptr<dsp::ParamSet> keep;     // keeps the plug-in alive while its window is open
    std::shared_ptr<Plugin> plugin;
    IPtr<IPlugView> view;
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if (FUnknownPrivate::iidEqual(iid, IPlugFrame::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = static_cast<IPlugFrame*>(this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 2; }
    uint32 PLUGIN_API release() override { return 1; }
    void fit(int w, int h) {
        RECT rc{0, 0, w, h};
        AdjustWindowRect(&rc, DWORD(GetWindowLongW(hwnd, GWL_STYLE)), FALSE);
        SetWindowPos(hwnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    tresult PLUGIN_API resizeView(IPlugView* v, ViewRect* r) override {
        if (hwnd && r) fit(r->getWidth(), r->getHeight());
        if (v && r) v->onSize(r);
        return kResultTrue;
    }
};

std::map<Plugin*, HWND>& openWindows() { static std::map<Plugin*, HWND> m; return m; }

LRESULT CALLBACK editorProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto* host = reinterpret_cast<EditorHost*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_CLOSE) { DestroyWindow(h); return 0; }
    if (m == WM_NCDESTROY && host) {
        openWindows().erase(host->key);
        if (host->view) { host->view->removed(); host->view->setFrame(nullptr); host->view = nullptr; }
        delete host;
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
    }
    return DefWindowProcW(h, m, w, l);
}

void registerEditorClass() {
    static bool done = false;
    if (done) return;
    done = true;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = editorProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"MiniDAWVst3Editor";
    RegisterClassExW(&wc);
}

// ---- base64 for the plug-in state inside project files ----
const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::string toBase64(const std::vector<unsigned char>& d) {
    std::string out;
    for (size_t i = 0; i < d.size(); i += 3) {
        const unsigned v = (unsigned(d[i]) << 16) | (i + 1 < d.size() ? unsigned(d[i + 1]) << 8 : 0u) | (i + 2 < d.size() ? unsigned(d[i + 2]) : 0u);
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
        out += i + 1 < d.size() ? kB64[(v >> 6) & 63] : '=';
        out += i + 2 < d.size() ? kB64[v & 63] : '=';
    }
    return out;
}
std::vector<unsigned char> fromBase64(const std::string& s) {
    std::vector<unsigned char> out;
    unsigned acc = 0;
    int bits = 0;
    for (char c : s) {
        const char* p = c == '=' ? nullptr : std::strchr(kB64, c);
        if (!p || !c) continue;
        acc = (acc << 6) | unsigned(p - kB64);
        bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back((unsigned char)((acc >> bits) & 255)); }
    }
    return out;
}

void scanDir(const std::filesystem::path& dir, int depth, std::vector<std::string>& out) {
    namespace fs = std::filesystem;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::string name = it->path().filename().string();
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        const bool isVst = lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".vst3") == 0;
        if (isVst) {   // a bundle folder or a single file; forward slashes like the Qt version, so projects open in both
            std::string full = it->path().string();
            std::replace(full.begin(), full.end(), '\\', '/');
            out.push_back(full);
        }
        else if (it->is_directory(ec) && depth < 3) scanDir(it->path(), depth + 1, out);
    }
}

}  // namespace

bool available() { return true; }

std::vector<std::string> defaultFolders() {
    std::vector<std::string> f;
    const char* common = std::getenv("CommonProgramFiles");
    f.push_back(std::string(common ? common : "C:/Program Files/Common Files") + "/VST3");
    if (const char* local = std::getenv("LOCALAPPDATA")) f.push_back(std::string(local) + "/Programs/Common/VST3");
    return f;
}

std::vector<PluginInfo> scan(const std::vector<std::string>& folders) {
    std::vector<PluginInfo> result;
    std::vector<std::string> modules;
    std::error_code ec;
    for (const std::string& f : folders)
        if (std::filesystem::exists(f, ec)) scanDir(f, 0, modules);
    for (const std::string& path : modules) {
        std::string err;
        auto module = VST3::Hosting::Module::create(path, err);
        if (!module) continue;
        std::vector<VST3::Hosting::ClassInfo> infos = module->getFactory().classInfos();
        for (const auto& ci : infos) {
            if (ci.category() != kVstAudioEffectClass) continue;
            PluginInfo pi;
            pi.name = ci.name();
            pi.vendor = ci.vendor();
            pi.path = path;
            pi.id = "vst3:" + path + "|" + ci.ID().toString();
            pi.instrument = ci.subCategoriesString().find("Instrument") != std::string::npos;
            result.push_back(pi);
        }
    }
    auto lower = [](std::string s) { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); }); return s; };
    std::sort(result.begin(), result.end(), [&](const PluginInfo& a, const PluginInfo& b) { return lower(a.name) < lower(b.name); });
    return result;
}

std::shared_ptr<dsp::Effect> createEffect(const std::string& id, std::string* error) {
    auto p = loadPlugin(id, error);
    if (!p) return nullptr;
    if (!p->hasInput) { if (error) *error = "This plug-in is an instrument, not an effect."; return nullptr; }
    return std::make_shared<Vst3Effect>(p);
}

std::shared_ptr<dsp::Instrument> createInstrument(const std::string& id, std::string* error) {
    auto p = loadPlugin(id, error);
    return p ? std::make_shared<Vst3Instrument>(p) : nullptr;
}

bool isVst3(const dsp::ParamSet& set) { return pluginOf(set) != nullptr; }

bool openEditor(const std::shared_ptr<dsp::ParamSet>& set, void* ownerWindow) {
    Plugin* raw = pluginOf(*set);
    if (!raw || !raw->controller) return false;
    auto open = openWindows().find(raw);
    if (open != openWindows().end() && IsWindow(open->second)) { ShowWindow(open->second, SW_RESTORE); SetForegroundWindow(open->second); return true; }
    IPtr<IPlugView> view = raw->controller->createView(ViewType::kEditor);
    if (!view) return false;
    std::shared_ptr<Plugin> keep;
    if (auto e = std::dynamic_pointer_cast<Vst3Effect>(set)) keep = e->plugin;
    else if (auto i = std::dynamic_pointer_cast<Vst3Instrument>(set)) keep = i->plugin;
    registerEditorClass();
    ViewRect r{};
    int w = 640, h = 480;
    if (view->getSize(&r) == kResultOk) { w = r.getWidth(); h = r.getHeight(); }
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rc{0, 0, w, h};
    AdjustWindowRect(&rc, style, FALSE);
    HWND hwnd = CreateWindowExW(0, L"MiniDAWVst3Editor", widen(raw->name).c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
                                static_cast<HWND>(ownerWindow), nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) return false;
    auto* host = new EditorHost;
    host->hwnd = hwnd;
    host->key = raw;
    host->keep = set;
    host->plugin = keep;
    host->view = view;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(host));
    openWindows()[raw] = hwnd;
    view->setFrame(host);
    view->attached(hwnd, kPlatformTypeHWND);
    ShowWindow(hwnd, SW_SHOW);
    return true;
}

std::string saveState(const dsp::ParamSet& set) {
    Plugin* p = pluginOf(set);
    if (!p || !p->component) return {};
    MemStream s;
    return p->component->getState(&s) == kResultOk ? toBase64(s.data()) : std::string();
}

void loadState(dsp::ParamSet& set, const std::string& base64) {
    Plugin* p = pluginOf(set);
    if (!p || !p->component) return;
    const std::vector<unsigned char> state = fromBase64(base64);
    if (state.empty()) return;
    MemStream s(state);
    p->component->setState(&s);
    MemStream c(state);
    if (p->controller) p->controller->setComponentState(&c);
}

}  // namespace vst3n
#endif  // MINIDAW_VST3
