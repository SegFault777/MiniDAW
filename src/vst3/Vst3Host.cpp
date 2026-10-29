// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// VST3 host built on the hosting helpers of the Steinberg VST3 SDK (dual licensed GPL-3.0 / proprietary).
// VST is a trademark of Steinberg Media Technologies GmbH.
#ifdef MINIDAW_VST3
#include "Vst3Host.h"

#include <QCloseEvent>
#include <QDialog>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QProcessEnvironment>
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

namespace vst3 {
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
    explicit MemStream(const QByteArray& d) : m_data(d) {}
    QByteArray data() const { return m_data; }
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
        if (n > 0) memcpy(buffer, m_data.constData() + m_pos, size_t(n));
        m_pos += std::max<int32>(0, n);
        if (numBytesRead) *numBytesRead = std::max<int32>(0, n);
        return kResultOk;
    }
    tresult PLUGIN_API write(void* buffer, int32 numBytes, int32* numBytesWritten) override {
        if (m_pos + numBytes > m_data.size()) m_data.resize(m_pos + numBytes);
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
    QByteArray m_data;
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

std::shared_ptr<Plugin> loadPlugin(const QString& id, QString* error) {
    // id = "vst3:<module path>|<class id>"
    const QString body = id.mid(5);
    const int bar = body.lastIndexOf('|');
    if (bar < 0) { if (error) *error = "Invalid plug-in id."; return nullptr; }
    auto p = std::make_shared<Plugin>();
    std::string err;
    if (!p->load(body.left(bar).toStdString(), body.mid(bar + 1).toStdString(), err)) {
        if (error) *error = QString::fromStdString(err);
        return nullptr;
    }
    p->id = id.toStdString();
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

/// The plug-in's own window. It keeps the plug-in alive while it is open.
class EditorWindow : public QDialog {
public:
    EditorWindow(std::shared_ptr<dsp::ParamSet> keepAlive, std::shared_ptr<Plugin> plugin, QWidget* parent)
        : QDialog(parent), m_keep(std::move(keepAlive)), m_plugin(std::move(plugin)) {
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowTitle(QString::fromStdString(m_plugin->name));
        setAttribute(Qt::WA_NativeWindow);
        m_view = m_plugin->controller ? m_plugin->controller->createView(ViewType::kEditor) : nullptr;
        if (!m_view) return;
        ViewRect r{};
        if (m_view->getSize(&r) == kResultOk) resize(r.getWidth(), r.getHeight());
        m_frame.owner = this;
        m_view->setFrame(&m_frame);
        m_view->attached(reinterpret_cast<void*>(winId()), kPlatformTypeHWND);
    }
    ~EditorWindow() override {
        if (m_view) {
            m_view->removed();
            m_view->setFrame(nullptr);
        }
    }
    bool hasView() const { return m_view != nullptr; }

private:
    struct Frame : IPlugFrame {
        EditorWindow* owner = nullptr;
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
        tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* r) override {
            if (owner && r) owner->resize(r->getWidth(), r->getHeight());
            if (view && r) view->onSize(r);
            return kResultTrue;
        }
    };
    std::shared_ptr<dsp::ParamSet> m_keep;
    std::shared_ptr<Plugin> m_plugin;
    IPtr<IPlugView> m_view;
    Frame m_frame;
};

void scanDir(const QDir& dir, int depth, QStringList& out) {
    for (const QFileInfo& fi : dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (fi.fileName().endsWith(".vst3", Qt::CaseInsensitive)) out << fi.absoluteFilePath();   // a bundle folder or a single file
        else if (fi.isDir() && depth < 3) scanDir(QDir(fi.absoluteFilePath()), depth + 1, out);
    }
}

}  // namespace

bool available() { return true; }

QStringList defaultFolders() {
    const auto env = QProcessEnvironment::systemEnvironment();
    QStringList f;
    const QString common = env.value("CommonProgramFiles", "C:/Program Files/Common Files");
    f << common + "/VST3";
    const QString local = env.value("LOCALAPPDATA");
    if (!local.isEmpty()) f << local + "/Programs/Common/VST3";
    return f;
}

std::vector<PluginInfo> scan(const QStringList& folders) {
    std::vector<PluginInfo> result;
    QStringList modules;
    for (const QString& f : folders)
        if (QDir(f).exists()) scanDir(QDir(f), 0, modules);
    for (const QString& path : modules) {
        std::string err;
        auto module = VST3::Hosting::Module::create(path.toStdString(), err);
        if (!module) continue;
        std::vector<VST3::Hosting::ClassInfo> infos = module->getFactory().classInfos();
        for (const auto& ci : infos) {
            if (ci.category() != kVstAudioEffectClass) continue;
            PluginInfo pi;
            pi.name = QString::fromStdString(ci.name());
            pi.vendor = QString::fromStdString(ci.vendor());
            pi.path = path;
            pi.id = "vst3:" + path + "|" + QString::fromStdString(ci.ID().toString());
            pi.instrument = ci.subCategoriesString().find("Instrument") != std::string::npos;
            result.push_back(pi);
        }
    }
    std::sort(result.begin(), result.end(), [](const PluginInfo& a, const PluginInfo& b) { return a.name.toLower() < b.name.toLower(); });
    return result;
}

std::shared_ptr<dsp::Effect> createEffect(const QString& id, QString* error) {
    auto p = loadPlugin(id, error);
    if (!p) return nullptr;
    if (!p->hasInput) { if (error) *error = "This plug-in is an instrument, not an effect."; return nullptr; }
    return std::make_shared<Vst3Effect>(p);
}

std::shared_ptr<dsp::Instrument> createInstrument(const QString& id, QString* error) {
    auto p = loadPlugin(id, error);
    return p ? std::make_shared<Vst3Instrument>(p) : nullptr;
}

QWidget* createEditor(const std::shared_ptr<dsp::ParamSet>& set, QWidget* parent) {
    Plugin* raw = pluginOf(*set);
    if (!raw) return nullptr;
    std::shared_ptr<Plugin> keep;
    if (auto e = std::dynamic_pointer_cast<Vst3Effect>(set)) keep = e->plugin;
    else if (auto i = std::dynamic_pointer_cast<Vst3Instrument>(set)) keep = i->plugin;
    auto* w = new EditorWindow(set, keep, parent);
    if (!w->hasView()) { delete w; return nullptr; }
    return w;
}

QByteArray saveState(const dsp::ParamSet& set) {
    Plugin* p = pluginOf(set);
    if (!p || !p->component) return {};
    MemStream s;
    return p->component->getState(&s) == kResultOk ? s.data() : QByteArray();
}

void loadState(dsp::ParamSet& set, const QByteArray& state) {
    Plugin* p = pluginOf(set);
    if (!p || !p->component || state.isEmpty()) return;
    MemStream s(state);
    p->component->setState(&s);
    MemStream c(state);
    if (p->controller) p->controller->setComponentState(&c);
}

}  // namespace vst3
#endif  // MINIDAW_VST3
