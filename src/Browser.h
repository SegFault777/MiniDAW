// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QWidget>
#include "Engine.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;

/// Loop Browser: lists the audio files of a loops folder, previews them in time with nothing else
/// and lets you drag them onto the timeline. A tempo written in the file name ("Funk 100 bpm.wav")
/// makes the loop follow the project tempo.
class LoopBrowser : public QWidget {
    Q_OBJECT
public:
    explicit LoopBrowser(Engine* e, QWidget* parent = nullptr);
    void setFolder(const QString& folder);
    QString folder() const { return m_folder; }
    static QString defaultFolder();
signals:
    void addRequested(const QString& path);   ///< Double-click: add to the selected track at the playhead.
private:
    void reload();
    Engine* m_e;
    QString m_folder;
    QLineEdit* m_search;
    QListWidget* m_list;
    QLabel* m_info;
    QCheckBox* m_auto;
};
