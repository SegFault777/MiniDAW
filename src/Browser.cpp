// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Browser.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>
#include <QVBoxLayout>

namespace {
/// A list whose items can be dragged out as file URLs (the timeline accepts those).
class LoopList : public QListWidget {
public:
    using QListWidget::QListWidget;
protected:
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override {
        auto* m = new QMimeData;
        QList<QUrl> urls;
        for (const QListWidgetItem* it : items) urls << QUrl::fromLocalFile(it->data(Qt::UserRole).toString());
        m->setUrls(urls);
        return m;
    }
};
}  // namespace

QString LoopBrowser::defaultFolder() {
    const QString app = QCoreApplication::applicationDirPath();
    for (const QString& rel : {QStringLiteral("/loops"), QStringLiteral("/../loops")})
        if (QDir(app + rel).exists()) return QDir::cleanPath(app + rel);
    return app + "/loops";
}

LoopBrowser::LoopBrowser(Engine* e, QWidget* p) : QWidget(p), m_e(e) {
    setMinimumWidth(230);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(8, 8, 8, 8);
    auto* title = new QLabel(tr("LOOP BROWSER"));
    title->setStyleSheet("color:#8c8c94;font-weight:bold;letter-spacing:1px");
    lay->addWidget(title);
    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Search"));
    lay->addWidget(m_search);
    auto* list = new LoopList;
    list->setDragEnabled(true);
    list->setDragDropMode(QAbstractItemView::DragOnly);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list = list;
    lay->addWidget(m_list, 1);
    m_auto = new QCheckBox(tr("Preview on click"));
    m_auto->setChecked(true);
    lay->addWidget(m_auto);
    auto* row = new QHBoxLayout;
    auto* stop = new QPushButton(tr("Stop"));
    auto* folder = new QPushButton(tr("Folder…"));
    row->addWidget(stop);
    row->addWidget(folder);
    lay->addLayout(row);
    m_info = new QLabel;
    m_info->setStyleSheet("color:#8c8c94;font-size:10px");
    m_info->setWordWrap(true);
    lay->addWidget(m_info);

    connect(m_search, &QLineEdit::textChanged, this, [this] { reload(); });
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
        if (m_auto->isChecked()) m_e->audition(it->data(Qt::UserRole).toString());
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) { emit addRequested(it->data(Qt::UserRole).toString()); });
    connect(stop, &QPushButton::clicked, this, [this] { m_e->stopAudition(); });
    connect(folder, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Choose Loops Folder"), m_folder);
        if (!d.isEmpty()) {
            QSettings().setValue("loops/folder", d);
            setFolder(d);
        }
    });
    setFolder(QSettings().value("loops/folder", defaultFolder()).toString());
}

void LoopBrowser::setFolder(const QString& f) {
    m_folder = f;
    reload();
}

void LoopBrowser::reload() {
    static const QRegularExpression bpmRe("(\\d{2,3})\\s*[-_ ]?\\s*bpm", QRegularExpression::CaseInsensitiveOption);
    m_list->clear();
    const QString filter = m_search->text().trimmed();
    QDirIterator it(m_folder, {"*.wav", "*.mp3", "*.flac", "*.ogg", "*.m4a", "*.aif", "*.aiff"}, QDir::Files, QDirIterator::Subdirectories);
    QStringList files;
    while (it.hasNext()) files << it.next();
    files.sort(Qt::CaseInsensitive);
    for (const QString& path : files) {
        const QString base = QFileInfo(path).completeBaseName();
        if (!filter.isEmpty() && !base.contains(filter, Qt::CaseInsensitive)) continue;
        const auto m = bpmRe.match(base);
        auto* item = new QListWidgetItem(m.hasMatch() ? QString("%1   ·   %2 bpm").arg(base.left(m.capturedStart()).trimmed(), m.captured(1)) : base, m_list);
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
    }
    m_info->setText(m_list->count() ? tr("%1 loops — double-click to add, or drag onto a track.").arg(m_list->count())
                                    : tr("No audio files found in %1").arg(m_folder));
}
