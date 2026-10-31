// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Qt-free audio devices. The engine always works with 44.1 kHz stereo; the platform converts to and from
// whatever the sound card uses (WASAPI shared mode with the system's sample-rate converter).
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace platform {

struct AudioDevice {
    std::string id;     ///< opaque, stable identifier (UTF-8); "" selects the system default
    std::string name;   ///< user-visible name
    bool isDefault = false;
};

/// Active output / input devices. Without a sound system there is one device with the id "null".
std::vector<AudioDevice> audioOutputs();
std::vector<AudioDevice> audioInputs();

/// Called on the audio thread: fill 'frames' frames of interleaved 16-bit stereo at 44.1 kHz.
using RenderFn = std::function<void(std::int16_t* stereo, int frames)>;
/// Called on the audio thread with 'frames' frames of interleaved float stereo at 44.1 kHz.
using CaptureFn = std::function<void(const float* stereo, int frames)>;

class AudioStream {
public:
    virtual ~AudioStream() = default;
    virtual std::string status() const = 0;   ///< for the status bar, for example "Speakers — 44100 Hz, 16-bit, 46 ms"
    virtual bool failed() const = 0;          ///< the device went away or stopped delivering
};

/// Opens and starts an output stream. 'bufferFrames' is the requested latency (128 .. 16384 frames).
/// Returns nullptr and sets *error when the device cannot be opened. The id "null" is a silent device that
/// pulls audio in real time (tests, machines without sound hardware).
std::unique_ptr<AudioStream> openOutput(const std::string& deviceId, int bufferFrames, RenderFn render, std::string* error);
std::unique_ptr<AudioStream> openInput(const std::string& deviceId, CaptureFn capture, std::string* error);

}  // namespace platform
