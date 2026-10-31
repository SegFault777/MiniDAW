// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// System sound backend: WASAPI (shared mode, event driven) on Windows. Other platforms have no backend and
// use the silent device of Audio.cpp.
#include "AudioBackends.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <initguid.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <propidl.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <future>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace platform::backend {
namespace {

constexpr int kRate = 44100;

/// COM for the current thread. When the thread already has an apartment (the Qt GUI thread) this does
/// nothing and COM is simply used as it is.
struct ComScope {
    bool owned;
    ComScope() { owned = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)); }
    ~ComScope() { if (owned) CoUninitialize(); }
};

std::string toUtf8(const wchar_t* w) {
    if (!w || !*w) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(size_t(std::max(n, 1)) - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring toWide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(size_t(std::max(n, 1)) - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

std::string deviceId(IMMDevice* d) {
    LPWSTR w = nullptr;
    if (FAILED(d->GetId(&w)) || !w) return {};
    std::string s = toUtf8(w);
    CoTaskMemFree(w);
    return s;
}

std::string friendlyName(IMMDevice* d) {
    ComPtr<IPropertyStore> ps;
    if (FAILED(d->OpenPropertyStore(STGM_READ, &ps))) return {};
    PROPVARIANT v;
    PropVariantInit(&v);
    std::string name;
    if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR) name = toUtf8(v.pwszVal);
    PropVariantClear(&v);
    return name;
}

/// The device with this id, or the default device when the id is empty or unknown (unplugged).
HRESULT findDevice(bool capture, const std::string& id, ComPtr<IMMDevice>& dev) {
    ComPtr<IMMDeviceEnumerator> en;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en));
    if (FAILED(hr)) return hr;
    if (!id.empty() && SUCCEEDED(en->GetDevice(toWide(id).c_str(), &dev))) return S_OK;
    return en->GetDefaultAudioEndpoint(capture ? eCapture : eRender, eConsole, &dev);
}

std::string hrText(const char* what, HRESULT hr) {
    char b[96];
    std::snprintf(b, sizeof b, "%s (0x%08lX)", what, static_cast<unsigned long>(hr));
    return b;
}

/// One WASAPI stream on its own thread. start() returns after the device is open (or has failed).
class Stream : public AudioStream {
public:
    Stream(bool capture, std::string id, int bufferFrames) : m_capture(capture), m_id(std::move(id)), m_buffer(bufferFrames) {
        m_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    }
    ~Stream() override {
        m_stop = true;
        if (m_event) SetEvent(m_event);
        if (m_thread.joinable()) m_thread.join();
        if (m_event) CloseHandle(m_event);
    }
    std::string status() const override { return m_status; }
    bool failed() const override { return m_failed; }

    bool start(RenderFn render, CaptureFn capture, std::string* error) {
        m_render = std::move(render);
        m_capture_fn = std::move(capture);
        std::promise<std::string> ready;
        std::future<std::string> result = ready.get_future();
        m_thread = std::thread([this, &ready] { run(ready); });
        if (result.wait_for(std::chrono::seconds(8)) != std::future_status::ready) {
            if (error) *error = "The audio device did not respond.";
            return false;   // the destructor stops the thread
        }
        const std::string e = result.get();
        if (!e.empty()) {
            if (error) *error = e;
            return false;
        }
        return true;
    }

private:
    void run(std::promise<std::string>& ready) {
        ComScope com;
        bool signalled = false;
        auto fail = [&](const std::string& text) {
            if (!signalled) { ready.set_value(text); signalled = true; }
            else m_failed = true;
        };
        ComPtr<IMMDevice> dev;
        HRESULT hr = findDevice(m_capture, m_id, dev);
        if (FAILED(hr) || !dev) { fail(hrText(m_capture ? "No audio input device was found." : "No audio output device was found.", hr)); return; }
        const std::string name = friendlyName(dev.Get());
        ComPtr<IAudioClient> client;
        hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.GetAddressOf()));
        if (FAILED(hr)) { fail(hrText("Could not activate the audio device", hr)); return; }

        WAVEFORMATEX fmt = {};
        fmt.nChannels = 2;
        fmt.nSamplesPerSec = kRate;
        if (m_capture) {
            fmt.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
            fmt.wBitsPerSample = 32;
        } else {
            fmt.wFormatTag = WAVE_FORMAT_PCM;
            fmt.wBitsPerSample = 16;
        }
        fmt.nBlockAlign = WORD(fmt.nChannels * fmt.wBitsPerSample / 8);
        fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
        const REFERENCE_TIME duration = m_capture ? 1000000 : REFERENCE_TIME(m_buffer) * 10000000 / kRate;   // 100 ns units
        // The system mixer converts sample rate, channel count and sample format to what the engine uses.
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                duration, 0, &fmt, nullptr);
        if (FAILED(hr)) { fail(hrText("The audio device rejected the format", hr)); return; }
        hr = client->SetEventHandle(m_event);
        UINT32 bufferFrames = 0;
        if (SUCCEEDED(hr)) hr = client->GetBufferSize(&bufferFrames);
        if (FAILED(hr)) { fail(hrText("Audio device setup failed", hr)); return; }

        ComPtr<IAudioRenderClient> render;
        ComPtr<IAudioCaptureClient> cap;
        if (m_capture) hr = client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(cap.GetAddressOf()));
        else hr = client->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(render.GetAddressOf()));
        if (FAILED(hr)) { fail(hrText("Audio device setup failed", hr)); return; }

        if (render) {   // start with silence so the first period does not click
            BYTE* p = nullptr;
            if (SUCCEEDED(render->GetBuffer(bufferFrames, &p))) render->ReleaseBuffer(bufferFrames, AUDCLNT_BUFFERFLAGS_SILENT);
        }
        hr = client->Start();
        if (FAILED(hr)) { fail(hrText("Could not start the audio device", hr)); return; }
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

        m_status = name + " — " + std::to_string(kRate) + " Hz, " + (m_capture ? "float" : "16-bit") + ", " +
                   std::to_string(int(bufferFrames) * 1000 / kRate) + " ms";
        ready.set_value({});
        signalled = true;

        std::vector<float> silence;
        while (!m_stop) {
            const DWORD w = WaitForSingleObject(m_event, 500);
            if (m_stop) break;
            if (render) {
                UINT32 padding = 0;
                hr = client->GetCurrentPadding(&padding);
                if (FAILED(hr)) { m_failed = true; break; }   // for example AUDCLNT_E_DEVICE_INVALIDATED
                const UINT32 avail = bufferFrames > padding ? bufferFrames - padding : 0;
                BYTE* p = nullptr;
                if (avail > 0 && SUCCEEDED(render->GetBuffer(avail, &p))) {
                    m_render(reinterpret_cast<std::int16_t*>(p), int(avail));
                    render->ReleaseBuffer(avail, 0);
                }
            } else {
                UINT32 packet = 0;
                for (;;) {
                    hr = cap->GetNextPacketSize(&packet);
                    if (FAILED(hr)) { m_failed = true; break; }
                    if (packet == 0) break;
                    BYTE* data = nullptr;
                    UINT32 got = 0;
                    DWORD flags = 0;
                    if (FAILED(cap->GetBuffer(&data, &got, &flags, nullptr, nullptr))) { m_failed = true; break; }
                    if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                        silence.assign(size_t(got) * 2, 0.f);
                        m_capture_fn(silence.data(), int(got));
                    } else {
                        m_capture_fn(reinterpret_cast<const float*>(data), int(got));
                    }
                    cap->ReleaseBuffer(got);
                }
                if (m_failed) break;
            }
            (void)w;
        }
        client->Stop();
    }

    bool m_capture;
    std::string m_id;
    int m_buffer;
    HANDLE m_event = nullptr;
    RenderFn m_render;
    CaptureFn m_capture_fn;
    std::string m_status;
    std::atomic<bool> m_stop{false}, m_failed{false};
    std::thread m_thread;
};

}  // namespace

bool hasSystemAudio() { return true; }

std::vector<AudioDevice> enumerate(bool capture) {
    std::vector<AudioDevice> out;
    ComScope com;
    ComPtr<IMMDeviceEnumerator> en;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en)))) return out;
    std::string defaultId;
    ComPtr<IMMDevice> def;
    if (SUCCEEDED(en->GetDefaultAudioEndpoint(capture ? eCapture : eRender, eConsole, &def)) && def) defaultId = deviceId(def.Get());
    ComPtr<IMMDeviceCollection> col;
    if (FAILED(en->EnumAudioEndpoints(capture ? eCapture : eRender, DEVICE_STATE_ACTIVE, &col))) return out;
    UINT n = 0;
    col->GetCount(&n);
    for (UINT i = 0; i < n; ++i) {
        ComPtr<IMMDevice> d;
        if (FAILED(col->Item(i, &d))) continue;
        AudioDevice a;
        a.id = deviceId(d.Get());
        a.name = friendlyName(d.Get());
        a.isDefault = a.id == defaultId;
        if (!a.id.empty()) out.push_back(std::move(a));
    }
    return out;
}

std::unique_ptr<AudioStream> openOutput(const std::string& id, int bufferFrames, RenderFn render, std::string* error) {
    auto s = std::make_unique<Stream>(false, id, bufferFrames);
    if (!s->start(std::move(render), {}, error)) return nullptr;
    return s;
}

std::unique_ptr<AudioStream> openInput(const std::string& id, CaptureFn capture, std::string* error) {
    auto s = std::make_unique<Stream>(true, id, 0);
    if (!s->start({}, std::move(capture), error)) return nullptr;
    return s;
}

}  // namespace platform::backend

#else  // ------------------------------------------------------------------ platforms without a backend

namespace platform::backend {
bool hasSystemAudio() { return false; }
std::vector<AudioDevice> enumerate(bool) { return {}; }
std::unique_ptr<AudioStream> openOutput(const std::string&, int, RenderFn, std::string* error) {
    if (error) *error = "No audio backend on this platform.";
    return nullptr;
}
std::unique_ptr<AudioStream> openInput(const std::string&, CaptureFn, std::string* error) {
    if (error) *error = "No audio backend on this platform.";
    return nullptr;
}
}  // namespace platform::backend

#endif
