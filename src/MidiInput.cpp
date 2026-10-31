// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MidiInput.h"

#include <QMetaObject>

QStringList MidiInput::deviceNames() {
    QStringList names;
    for (const std::string& n : platform::midiInputs()) names << QString::fromStdString(n);
    return names;
}

bool MidiInput::open(int index) {
    close();
    m_port = platform::openMidiInput(index, [this](int status, int d1, int d2) {
        const int kind = status & 0xF0;
        // This runs on a system thread: hand the event over to the UI thread.
        if (kind == 0x90 && d2 > 0) QMetaObject::invokeMethod(this, [this, d1, d2] { emit noteOn(d1, d2); }, Qt::QueuedConnection);
        else if (kind == 0x80 || kind == 0x90) QMetaObject::invokeMethod(this, [this, d1] { emit noteOff(d1); }, Qt::QueuedConnection);
        else if (kind == 0xB0) QMetaObject::invokeMethod(this, [this, d1, d2] { emit controller(d1, d2); }, Qt::QueuedConnection);
    });
    if (!m_port) return false;
    m_name = QString::fromStdString(m_port->name());
    return true;
}

void MidiInput::close() {
    m_port.reset();   // stops the callbacks before anything else is destroyed
    m_name.clear();
}
