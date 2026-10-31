// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Translator.h"

#include <QCoreApplication>
#include <QDir>
#include <QLocale>

bool FileTranslator::loadFile(const QString& path) { return m_table.load(path.toStdString()); }

QString FileTranslator::translate(const char*, const char* sourceText, const char*, int) const {
    // The context (class name) is ignored on purpose: one text is translated the same way everywhere.
    const std::string* t = m_table.find(sourceText);
    return t ? QString::fromStdString(*t) : QString();   // an empty result means "not translated"
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
    for (const core::LanguageInfo& l : core::findLanguages(translationsDir().toStdString()))
        langs << Language{QString::fromStdString(l.code), QString::fromStdString(l.name), QString::fromStdString(l.path)};
    return langs;
}

QString install(const QString& code) {
    const QList<Language> langs = available();
    QString want = code;
    if (want == "auto") {
        std::vector<core::LanguageInfo> infos;
        for (const Language& l : langs) infos.push_back({l.code.toStdString(), l.name.toStdString(), l.path.toStdString()});
        want = QString::fromStdString(core::pickLanguage(infos, QLocale::system().name().toStdString()));   // for example "ko_KR"
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
