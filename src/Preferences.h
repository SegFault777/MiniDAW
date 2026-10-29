// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDialog>
#include "Engine.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;

/// Settings dialog: audio device, MIDI input, language, loops folder and VST3 plug-in folders.
/// Values are stored with QSettings; MainWindow applies them after the dialog is accepted.
class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    PreferencesDialog(Engine* e, QWidget* parent = nullptr);
    bool languageChanged() const { return m_languageChanged; }
private:
    void save();
    Engine* m_e;
    QComboBox *m_device, *m_buffer, *m_midi, *m_language, *m_input;
    QSpinBox* m_recOffset;
    QLabel* m_status;
    QLineEdit* m_loops;
    QListWidget *m_folders, *m_found;
    bool m_languageChanged = false;
    QString m_initialLanguage;
};
