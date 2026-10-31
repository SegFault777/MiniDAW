// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Qt-free MIDI keyboard input (winmm on Windows).
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace platform {

/// Names of the MIDI input devices; the index is what openMidiInput takes.
std::vector<std::string> midiInputs();

/// Called on a system thread for every short message: status byte, two data bytes.
using MidiFn = std::function<void(int status, int data1, int data2)>;

class MidiPort {
public:
    virtual ~MidiPort() = default;
    virtual std::string name() const = 0;
};

/// Opens and starts a device. Returns nullptr when it cannot be opened (or on platforms without MIDI).
std::unique_ptr<MidiPort> openMidiInput(int index, MidiFn fn);

}  // namespace platform
