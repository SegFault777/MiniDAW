// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <algorithm>
#include "Audio.h"

#include <atomic>
#include <chrono>
#include <thread>
#include "AudioBackends.h"

namespace platform {

namespace {
/// A device that never makes a sound but pulls and produces audio at the real-time rate.
class NullOutput : public AudioStream {
public:
    NullOutput(int bufferFrames, RenderFn fn) : m_fn(std::move(fn)), m_buffer(bufferFrames) {
        m_thread = std::thread([this] {
            std::vector<std::int16_t> buf(1024 * 2);
            auto next = std::chrono::steady_clock::now();
            while (!m_stop) {
                m_fn(buf.data(), 512);
                next += std::chrono::microseconds(512 * 1000000LL / 44100);
                std::this_thread::sleep_until(next);
            }
        });
    }
    ~NullOutput() override { m_stop = true; if (m_thread.joinable()) m_thread.join(); }
    std::string status() const override { return "Silent device — 44100 Hz, 16-bit, " + std::to_string(m_buffer * 1000 / 44100) + " ms"; }
    bool failed() const override { return false; }
private:
    RenderFn m_fn;
    int m_buffer;
    std::atomic<bool> m_stop{false};
    std::thread m_thread;
};

class NullInput : public AudioStream {
public:
    explicit NullInput(CaptureFn fn) : m_fn(std::move(fn)) {
        m_thread = std::thread([this] {
            std::vector<float> buf(1024 * 2, 0.f);
            auto next = std::chrono::steady_clock::now();
            while (!m_stop) {
                m_fn(buf.data(), 512);
                next += std::chrono::microseconds(512 * 1000000LL / 44100);
                std::this_thread::sleep_until(next);
            }
        });
    }
    ~NullInput() override { m_stop = true; if (m_thread.joinable()) m_thread.join(); }
    std::string status() const override { return "Silent input"; }
    bool failed() const override { return false; }
private:
    CaptureFn m_fn;
    std::atomic<bool> m_stop{false};
    std::thread m_thread;
};
}  // namespace

std::vector<AudioDevice> audioOutputs() {
    auto list = backend::enumerate(false);
    if (list.empty() && !backend::hasSystemAudio()) list.push_back({"null", "Silent device (no sound system)", true});
    return list;
}

std::vector<AudioDevice> audioInputs() {
    auto list = backend::enumerate(true);
    if (list.empty() && !backend::hasSystemAudio()) list.push_back({"null", "Silent input (no sound system)", true});
    return list;
}

std::unique_ptr<AudioStream> openOutput(const std::string& id, int bufferFrames, RenderFn render, std::string* error) {
    bufferFrames = std::clamp(bufferFrames, 128, 16384);
    if (id == "null" || !backend::hasSystemAudio()) return std::make_unique<NullOutput>(bufferFrames, std::move(render));
    return backend::openOutput(id, bufferFrames, std::move(render), error);
}

std::unique_ptr<AudioStream> openInput(const std::string& id, CaptureFn capture, std::string* error) {
    if (id == "null" || !backend::hasSystemAudio()) return std::make_unique<NullInput>(std::move(capture));
    return backend::openInput(id, std::move(capture), error);
}

}  // namespace platform
