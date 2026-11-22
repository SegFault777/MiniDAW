// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Decode.h"
#include "../core/FileTypes.h"

#include <algorithm>
#include <cctype>
#include "../core/AudioIO.h"
#include "../core/Base.h"
#include "../core/Mp3Gapless.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif

namespace platform {

namespace {
std::string lowerExtension(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string e = path.substr(dot);
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return e;
}

#ifdef _WIN32
bool decodeMediaFoundation(const std::string& path, std::vector<float>& pcm, std::string* error) {
    const bool comOwned = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));   // worker thread: its own apartment
    struct Cleanup {
        bool com;
        bool mf = false;
        ~Cleanup() { if (mf) MFShutdown(); if (com) CoUninitialize(); }
    } cleanup{comOwned};
    if (FAILED(MFStartup(MF_VERSION))) return core::fail(error, "Media Foundation is not available.");
    cleanup.mf = true;

    const int wn = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring wpath(size_t(wn > 0 ? wn : 1) - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wn);

    ComPtr<IMFSourceReader> reader;
    if (FAILED(MFCreateSourceReaderFromURL(wpath.c_str(), nullptr, &reader))) return core::fail(error, "This file format is not supported.");
    reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    if (FAILED(reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE))) return core::fail(error, "The file has no audio stream.");

    // Ask the source reader for 44.1 kHz stereo float; it inserts the decoder and the converter.
    ComPtr<IMFMediaType> want;
    MFCreateMediaType(&want);
    want->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    want->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
    want->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
    want->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kSampleRate);
    want->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);
    want->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 8);
    want->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kSampleRate * 8);
    want->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    if (FAILED(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, want.Get()))) return core::fail(error, "This audio format cannot be converted.");

    // What the reader really delivers (it may have kept the file's rate or channel count).
    UINT32 channels = 2, rate = kSampleRate;
    ComPtr<IMFMediaType> got;
    if (SUCCEEDED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &got))) {
        got->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
        got->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
    }
    if (channels < 1) channels = 2;

    core::Resampler resampler;
    pcm.clear();
    for (;;) {
        DWORD flags = 0;
        ComPtr<IMFSample> sample;
        if (FAILED(reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr, &sample))) return core::fail(error, "Reading the audio failed.");
        if (flags & (MF_SOURCE_READERF_ENDOFSTREAM | MF_SOURCE_READERF_ERROR)) break;
        if (!sample) continue;
        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) continue;
        BYTE* data = nullptr;
        DWORD len = 0;
        if (FAILED(buffer->Lock(&data, nullptr, &len))) continue;
        const size_t frames = len / (sizeof(float) * channels);
        std::vector<float> stereo(frames * 2);
        const float* f = reinterpret_cast<const float*>(data);
        for (size_t i = 0; i < frames; ++i) {
            stereo[2 * i] = f[i * channels];
            stereo[2 * i + 1] = f[i * channels + (channels > 1 ? 1 : 0)];   // mono is duplicated
        }
        buffer->Unlock();
        resampler.process(stereo, int(rate), pcm);
    }
    if (pcm.empty()) return core::fail(error, "The file contains no audio.");
    return true;
}
#endif
}  // namespace

bool decodeFile(const std::string& path, std::vector<float>& pcm, std::string* error) {
    const std::string ext = lowerExtension(path);
    if (ext == ".wav" || ext == ".wave") return core::readWavFile(path, pcm, error);
    if (ext == ".aif" || ext == ".aiff" || ext == ".aifc") {   // our own reader: no codec needed, and Media Foundation is not sure about AIFF
        std::string aerr;
        if (core::readAiffFile(path, pcm, &aerr)) return true;
#ifndef _WIN32
        return core::fail(error, aerr);
#endif
    }
#ifdef _WIN32
    if (!decodeMediaFoundation(path, pcm, error)) return false;
    if (ext == ".mp3") core::trimMp3Decoded(pcm, core::readGaplessHeader(path), kSampleRate);   // gapless: drop encoder / decoder delay
    return true;
#else
    // Other platforms: a WAV file with an unusual extension still works.
    if (core::readWavFile(path, pcm, nullptr)) return true;
    return core::fail(error, "This audio format is only supported on Windows.");
#endif
}

std::vector<std::string> decodableExtensions() {
#ifdef _WIN32
    return core::audioExtensions();   // the operating system decodes what it has a codec for; a failure is reported per file
#else
    return {".wav", ".wave", ".aif", ".aiff", ".aifc"};   // read by the core without a codec
#endif
}

}  // namespace platform
