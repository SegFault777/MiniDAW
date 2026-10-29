// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Translator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QTextStream>

namespace {
QString unescape(const QString& s) {
    QString out;
    for (int i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const QChar c = s[++i];
            out += c == 'n' ? QChar('\n') : c == 't' ? QChar('\t') : c;
        } else {
            out += s[i];
        }
    }
    return out;
}
}  // namespace

bool FileTranslator::loadFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    m_map.clear();
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.isEmpty() || line.startsWith('#')) continue;
        const int tab = line.indexOf('\t');
        if (tab <= 0) continue;
        const QString tr = unescape(line.mid(tab + 1));
        if (!tr.isEmpty()) m_map.insert(unescape(line.left(tab)), tr);
    }
    return !m_map.isEmpty();
}

QString FileTranslator::translate(const char*, const char* sourceText, const char*, int) const {
    // The context (class name) is ignored on purpose: one text is translated the same way everywhere.
    const auto it = m_map.constFind(QString::fromUtf8(sourceText));
    return it == m_map.constEnd() ? QString() : it.value();   // an empty result means "not translated"
}

namespace i18n {

QString translationsDir() {
    const QString app = QCoreApplication::applicationDirPath();
    for (const QString& rel : {QStringLiteral("/translations"), QStringLiteral("/../translations")})
        if (QDir(app + rel).exists()) return QDir::cleanPath(app + rel);
    return app + "/translations";
}

QList<Language> available() {
    QList<Language> langs;
    const QDir dir(translationsDir());
    for (const QFileInfo& fi : dir.entryInfoList({"*.tsv"}, QDir::Files, QDir::Name)) {
        Language l{fi.completeBaseName(), fi.completeBaseName(), fi.absoluteFilePath()};
        QFile f(l.path);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString first = QString::fromUtf8(f.readLine()).trimmed();
            if (first.startsWith("#name\t")) l.name = first.mid(6);
        }
        langs << l;
    }
    return langs;
}

QString install(const QString& code) {
    const QList<Language> langs = available();
    QString want = code;
    if (want == "auto") {
        const QString sys = QLocale::system().name();   // for example "ko_KR"
        want.clear();
        for (const Language& l : langs)
            if (l.code == sys) want = l.code;
        for (const Language& l : langs)
            if (want.isEmpty() && l.code == sys.section('_', 0, 0)) want = l.code;
        for (const Language& l : langs)   // any Chinese locale falls back to Simplified Chinese
            if (want.isEmpty() && sys.startsWith("zh") && l.code.startsWith("zh")) want = l.code;
    }
    for (const Language& l : langs) {
        if (l.code != want) continue;
        auto* t = new FileTranslator;   // owned by the application for its whole lifetime
        if (t->loadFile(l.path)) {
            QCoreApplication::installTranslator(t);
            return l.code;
        }
        delete t;
    }
    return "en";
}

}  // namespace i18n
