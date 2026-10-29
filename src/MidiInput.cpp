// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MidiInput.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>

namespace {
void CALLBACK midiProc(HMIDIIN, UINT msg, DWORD_PTR instance, DWORD_PTR p1, DWORD_PTR) {
    if (msg != MIM_DATA) return;
    auto* self = reinterpret_cast<MidiInput*>(instance);
    const int status = int(p1 & 0xFF), d1 = int((p1 >> 8) & 0xFF), d2 = int((p1 >> 16) & 0xFF);
    const int kind = status & 0xF0;
    // This runs on a system thread: hand the event over to the UI thread.
    if (kind == 0x90 && d2 > 0) QMetaObject::invokeMethod(self, [self, d1, d2] { emit self->noteOn(d1, d2); }, Qt::QueuedConnection);
    else if (kind == 0x80 || kind == 0x90) QMetaObject::invokeMethod(self, [self, d1] { emit self->noteOff(d1); }, Qt::QueuedConnection);
    else if (kind == 0xB0) QMetaObject::invokeMethod(self, [self, d1, d2] { emit self->controller(d1, d2); }, Qt::QueuedConnection);
}
}  // namespace

QStringList MidiInput::deviceNames() {
    QStringList names;
    for (UINT i = 0; i < midiInGetNumDevs(); ++i) {
        MIDIINCAPSW caps;
        if (midiInGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) names << QString::fromWCharArray(caps.szPname);
    }
    return names;
}

bool MidiInput::open(int index) {
    close();
    HMIDIIN h = nullptr;
    if (midiInOpen(&h, UINT(index), reinterpret_cast<DWORD_PTR>(&midiProc), reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION) != MMSYSERR_NOERROR)
        return false;
    midiInStart(h);
    m_handle = h;
    const QStringList names = deviceNames();
    m_name = index >= 0 && index < names.size() ? names[index] : QString();
    return true;
}

void MidiInput::close() {
    if (!m_handle) return;
    auto h = static_cast<HMIDIIN>(m_handle);
    midiInStop(h);
    midiInReset(h);
    midiInClose(h);
    m_handle = nullptr;
    m_name.clear();
}
#else
QStringList MidiInput::deviceNames() { return {}; }
bool MidiInput::open(int) { return false; }
void MidiInput::close() {}
#endif
