// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Mp3Encode.h"

#include <algorithm>
#include <cmath>

#include "../core/Base.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#endif

namespace platform {

#ifdef _WIN32

bool mp3EncoderAvailable() { return true; }

namespace {
// RAII for COM + Media Foundation (the call may come from the UI thread or from a worker)
struct MfSession {
    bool com = false, mf = false;
    MfSession() { const HRESULT h = CoInitializeEx(nullptr, COINIT_MULTITHREADED); com = SUCCEEDED(h); mf = SUCCEEDED(MFStartup(MF_VERSION)); }
    ~MfSession() { if (mf) MFShutdown(); if (com) CoUninitialize(); }
};
std::string hrText(const char* what, HRESULT hr) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "%s (0x%08lX)", what, static_cast<unsigned long>(hr));
    return buf;
}
}  // namespace

bool encodeMp3(const std::vector<float>& stereo, int sampleRate, int kbps, const std::string& path, std::string* error) {
    using Microsoft::WRL::ComPtr;
    if (stereo.size() < 2) return core::fail(error, "There is nothing to encode.");
    if (sampleRate != 44100 && sampleRate != 48000) return core::fail(error, "MP3 files can be 44.1 or 48 kHz.");
    MfSession mf;
    if (!mf.mf) return core::fail(error, "Media Foundation is not available.");

    const int wn = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring wpath(size_t(wn > 0 ? wn : 1) - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wn);

    // the writer chooses the MP3 container (frames only, no tag) from this attribute
    ComPtr<IMFAttributes> attr;
    HRESULT hr = MFCreateAttributes(&attr, 1);
    if (SUCCEEDED(hr)) hr = attr->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MP3);
    ComPtr<IMFSinkWriter> writer;
    if (SUCCEEDED(hr)) hr = MFCreateSinkWriterFromURL(wpath.c_str(), nullptr, attr.Get(), &writer);
    if (FAILED(hr)) return core::fail(error, hrText("Cannot create the MP3 file", hr));

    // output: MP3 at the chosen bit rate
    ComPtr<IMFMediaType> outType;
    hr = MFCreateMediaType(&outType);
    if (SUCCEEDED(hr)) {
        outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        outType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_MP3);
        outType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, UINT32(sampleRate));
        outType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
        outType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, UINT32(kbps) * 125u);   // kbit/s -> bytes/s
        outType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 1);
    }
    DWORD stream = 0;
    if (SUCCEEDED(hr)) hr = writer->AddStream(outType.Get(), &stream);
    // input: 16-bit PCM
    ComPtr<IMFMediaType> inType;
    if (SUCCEEDED(hr)) hr = MFCreateMediaType(&inType);
    if (SUCCEEDED(hr)) {
        inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        inType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        inType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        inType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, UINT32(sampleRate));
        inType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
        inType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
        inType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, UINT32(sampleRate) * 4u);
        hr = writer->SetInputMediaType(stream, inType.Get(), nullptr);
    }
    if (FAILED(hr)) return core::fail(error, hrText("This computer has no MP3 encoder for these settings", hr));
    hr = writer->BeginWriting();
    if (FAILED(hr)) return core::fail(error, hrText("Cannot start the MP3 encoder", hr));

    const size_t frames = stereo.size() / 2;
    const size_t chunk = size_t(sampleRate) / 2;   // half a second per sample
    for (size_t at = 0; at < frames; at += chunk) {
        const size_t n = std::min(chunk, frames - at);
        ComPtr<IMFMediaBuffer> buf;
        hr = MFCreateMemoryBuffer(DWORD(n * 4), &buf);
        BYTE* data = nullptr;
        if (SUCCEEDED(hr)) hr = buf->Lock(&data, nullptr, nullptr);
        if (FAILED(hr)) return core::fail(error, hrText("Out of memory while encoding", hr));
        auto* out = reinterpret_cast<std::int16_t*>(data);
        for (size_t i = 0; i < n * 2; ++i) {
            const float v = std::clamp(stereo[at * 2 + i], -1.f, 1.f);
            out[i] = std::int16_t(std::lround(v * 32767.f));
        }
        buf->Unlock();
        buf->SetCurrentLength(DWORD(n * 4));
        ComPtr<IMFSample> sample;
        hr = MFCreateSample(&sample);
        if (SUCCEEDED(hr)) hr = sample->AddBuffer(buf.Get());
        if (SUCCEEDED(hr)) hr = sample->SetSampleTime(LONGLONG(double(at) * 1e7 / sampleRate));
        if (SUCCEEDED(hr)) hr = sample->SetSampleDuration(LONGLONG(double(n) * 1e7 / sampleRate));
        if (SUCCEEDED(hr)) hr = writer->WriteSample(stream, sample.Get());
        if (FAILED(hr)) return core::fail(error, hrText("The MP3 encoder stopped", hr));
    }
    hr = writer->Finalize();
    if (FAILED(hr)) return core::fail(error, hrText("Cannot finish the MP3 file", hr));
    return true;
}

#else  // not Windows

bool mp3EncoderAvailable() { return false; }

bool encodeMp3(const std::vector<float>&, int, int, const std::string&, std::string* error) {
    return core::fail(error, "MP3 export needs the Windows MP3 encoder (Media Foundation).");
}

#endif

}  // namespace platform
