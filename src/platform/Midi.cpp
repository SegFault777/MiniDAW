// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Midi.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>

namespace platform {
namespace {

std::string toUtf8(const wchar_t* w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n > 0 ? n : 1) - 1, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

class WinMidi : public MidiPort {
public:
    WinMidi(MidiFn fn) : m_fn(std::move(fn)) {}
    ~WinMidi() override {
        if (m_handle) {
            midiInStop(m_handle);
            midiInReset(m_handle);
            midiInClose(m_handle);
        }
    }
    bool open(int index) {
        if (midiInOpen(&m_handle, UINT(index), reinterpret_cast<DWORD_PTR>(&proc), reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION) != MMSYSERR_NOERROR) {
            m_handle = nullptr;
            return false;
        }
        const auto names = midiInputs();
        m_name = index >= 0 && index < int(names.size()) ? names[size_t(index)] : std::string();
        midiInStart(m_handle);
        return true;
    }
    std::string name() const override { return m_name; }

private:
    static void CALLBACK proc(HMIDIIN, UINT msg, DWORD_PTR instance, DWORD_PTR p1, DWORD_PTR) {
        if (msg != MIM_DATA) return;
        auto* self = reinterpret_cast<WinMidi*>(instance);
        self->m_fn(int(p1 & 0xFF), int((p1 >> 8) & 0xFF), int((p1 >> 16) & 0xFF));
    }
    MidiFn m_fn;
    HMIDIIN m_handle = nullptr;
    std::string m_name;
};
}  // namespace

std::vector<std::string> midiInputs() {
    std::vector<std::string> names;
    for (UINT i = 0; i < midiInGetNumDevs(); ++i) {
        MIDIINCAPSW caps;
        if (midiInGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) names.push_back(toUtf8(caps.szPname));
    }
    return names;
}

std::unique_ptr<MidiPort> openMidiInput(int index, MidiFn fn) {
    auto port = std::make_unique<WinMidi>(std::move(fn));
    if (!port->open(index)) return nullptr;
    return port;
}

}  // namespace platform

#else

namespace platform {
std::vector<std::string> midiInputs() { return {}; }
std::unique_ptr<MidiPort> openMidiInput(int, MidiFn) { return nullptr; }
}  // namespace platform

#endif
