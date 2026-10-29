// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Preferences.h"

#include <algorithm>
#include <QAudioDevice>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMediaDevices>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTimer>
#include <QTabWidget>
#include <QVBoxLayout>
#include "Browser.h"
#include "MidiInput.h"
#include "Panels.h"
#include "Translator.h"

PreferencesDialog::PreferencesDialog(Engine* e, QWidget* parent) : QDialog(parent), m_e(e) {
    setWindowTitle(tr("Preferences"));
    resize(560, 440);
    QSettings s;
    auto* root = new QVBoxLayout(this);
    auto* tabs = new QTabWidget;
    root->addWidget(tabs);

    // ---- Audio ----
    auto* audio = new QWidget;
    auto* af = new QFormLayout(audio);
    m_device = new QComboBox;
    m_device->addItem(tr("System Default"), QByteArray());
    for (const QAudioDevice& d : QMediaDevices::audioOutputs()) m_device->addItem(d.description(), d.id());
    const QByteArray savedDev = QByteArray::fromBase64(s.value("audio/device").toString().toLatin1());
    m_device->setCurrentIndex(std::max(0, m_device->findData(savedDev)));
    af->addRow(tr("Output device"), m_device);
    m_buffer = new QComboBox;
    for (int n : {256, 512, 1024, 2048, 4096}) m_buffer->addItem(tr("%1 samples (%2 ms)").arg(n).arg(n * 1000 / kSampleRate), n);
    m_buffer->setCurrentIndex(std::max(0, m_buffer->findData(s.value("audio/buffer", 2048).toInt())));
    af->addRow(tr("Buffer size"), m_buffer);
    m_input = new QComboBox;
    m_input->addItem(tr("System Default"), QByteArray());
    for (const QAudioDevice& d : QMediaDevices::audioInputs()) m_input->addItem(d.description(), d.id());
    const QByteArray savedIn = QByteArray::fromBase64(s.value("audio/input").toString().toLatin1());
    m_input->setCurrentIndex(std::max(0, m_input->findData(savedIn)));
    af->addRow(tr("Input device"), m_input);
    m_recOffset = new QSpinBox;
    m_recOffset->setRange(-500, 500);
    m_recOffset->setSuffix(" ms");
    m_recOffset->setValue(s.value("audio/recOffset", 0).toInt());
    m_recOffset->setToolTip(tr("Moves recorded audio earlier (positive) or later (negative) to make up for the latency of your audio interface."));
    af->addRow(tr("Recording latency compensation"), m_recOffset);
    auto* test = new QPushButton(tr("Play Test Tone"));
    af->addRow(QString(), test);
    m_status = new QLabel(m_e->audioStatus());
    m_status->setWordWrap(true);
    af->addRow(tr("Status"), m_status);
    tabs->addTab(audio, tr("Audio"));
    connect(test, &QPushButton::clicked, this, [this] {
        save();   // apply the chosen device first, then give it a moment to open
        QTimer::singleShot(600, m_e, [this] { m_e->playTestTone(); });
    });
    connect(m_e, &Engine::audioStatusChanged, this, [this](const QString& st) { m_status->setText(st); });

    // ---- MIDI ----
    auto* midi = new QWidget;
    auto* mf = new QFormLayout(midi);
    m_midi = new QComboBox;
    m_midi->addItem(tr("None"));
    m_midi->addItems(MidiInput::deviceNames());
    m_midi->setCurrentIndex(std::max(0, m_midi->findText(s.value("midi/device").toString())));
    mf->addRow(tr("MIDI input"), m_midi);
    mf->addRow(QString(), new QLabel(tr("Notes from the selected keyboard play the selected Software Instrument track.")));
    tabs->addTab(midi, tr("MIDI"));

    // ---- General ----
    auto* gen = new QWidget;
    auto* gf = new QFormLayout(gen);
    m_language = new QComboBox;
    m_language->addItem(tr("Automatic (system language)"), "auto");
    m_language->addItem("English", "en");
    for (const i18n::Language& l : i18n::available()) m_language->addItem(l.name, l.code);
    m_initialLanguage = s.value("ui/language", "auto").toString();
    m_language->setCurrentIndex(std::max(0, m_language->findData(m_initialLanguage)));
    gf->addRow(tr("Language"), m_language);
    gf->addRow(QString(), new QLabel(tr("Restart MiniDAW to apply a new language.")));
    auto* loopRow = new QHBoxLayout;
    m_loops = new QLineEdit(s.value("loops/folder", LoopBrowser::defaultFolder()).toString());
    auto* browse = new QPushButton(tr("Browse…"));
    loopRow->addWidget(m_loops);
    loopRow->addWidget(browse);
    gf->addRow(tr("Loops folder"), loopRow);
    tabs->addTab(gen, tr("General"));
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Choose Loops Folder"), m_loops->text());
        if (!d.isEmpty()) m_loops->setText(d);
    });

    // ---- Plug-ins ----
    auto* plug = new QWidget;
    auto* pl = new QVBoxLayout(plug);
    pl->addWidget(new QLabel(vst3::available() ? tr("VST3 plug-in folders:") : tr("This build was made without VST3 support.")));
    m_folders = new QListWidget;
    QStringList folders = s.value("vst3/folders").toStringList();
    if (folders.isEmpty()) folders = vst3::defaultFolders();
    m_folders->addItems(folders);
    pl->addWidget(m_folders, 1);
    auto* pb = new QHBoxLayout;
    auto* add = new QPushButton(tr("Add Folder…"));
    auto* rem = new QPushButton(tr("Remove"));
    auto* scan = new QPushButton(tr("Rescan"));
    pb->addWidget(add);
    pb->addWidget(rem);
    pb->addStretch(1);
    pb->addWidget(scan);
    pl->addLayout(pb);
    m_found = new QListWidget;
    pl->addWidget(m_found, 2);
    tabs->addTab(plug, tr("Plug-ins"));
    auto fill = [this] {
        m_found->clear();
        for (const vst3::PluginInfo& p : PluginCatalog::list())
            m_found->addItem(QString("%1  —  %2  [%3]").arg(p.name, p.vendor, p.instrument ? tr("Instrument") : tr("Effect")));
    };
    fill();
    connect(add, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Add VST3 Folder"));
        if (!d.isEmpty()) m_folders->addItem(d);
    });
    connect(rem, &QPushButton::clicked, this, [this] { delete m_folders->takeItem(m_folders->currentRow()); });
    connect(scan, &QPushButton::clicked, this, [this, fill] {
        save();
        PluginCatalog::rescan();
        fill();
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] { save(); accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void PreferencesDialog::save() {
    QSettings s;
    s.setValue("audio/device", QString::fromLatin1(m_device->currentData().toByteArray().toBase64()));
    s.setValue("audio/buffer", m_buffer->currentData().toInt());
    s.setValue("audio/input", QString::fromLatin1(m_input->currentData().toByteArray().toBase64()));
    s.setValue("audio/recOffset", m_recOffset->value());
    s.setValue("midi/device", m_midi->currentIndex() > 0 ? m_midi->currentText() : QString());
    s.setValue("ui/language", m_language->currentData().toString());
    s.setValue("loops/folder", m_loops->text());
    QStringList folders;
    for (int i = 0; i < m_folders->count(); ++i) folders << m_folders->item(i)->text();
    s.setValue("vst3/folders", folders);
    m_languageChanged = m_language->currentData().toString() != m_initialLanguage;
    m_e->setAudioOutput(m_device->currentData().toByteArray(), m_buffer->currentData().toInt());
    m_e->setAudioInput(m_input->currentData().toByteArray(), m_recOffset->value());
}
