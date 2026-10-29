// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QString>
#include <QStringList>

/// MIDI keyboard input through the Windows multimedia API (winmm). Signals are delivered on the UI thread.
class MidiInput : public QObject {
    Q_OBJECT
public:
    explicit MidiInput(QObject* parent = nullptr) : QObject(parent) {}
    ~MidiInput() override { close(); }

    static QStringList deviceNames();
    bool open(int index);
    void close();
    bool isOpen() const { return m_handle != nullptr; }
    QString deviceName() const { return m_name; }

signals:
    void noteOn(int pitch, int velocity);
    void noteOff(int pitch);
    void controller(int cc, int value);

private:
    void* m_handle = nullptr;
    QString m_name;
};
