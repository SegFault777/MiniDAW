// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QHash>
#include <QList>
#include <QString>
#include <QTranslator>

/// Looks strings up in a plain-text translation file: one "English<TAB>Translation" pair per line.
/// Lines starting with '#' are comments; the first line "#name<TAB>Display name" names the language.
/// \n, \t and \\ may be used inside the text. No Qt Linguist tools are needed to add a language.
class FileTranslator : public QTranslator {
public:
    bool loadFile(const QString& path);
    QString translate(const char* context, const char* sourceText, const char* disambiguation = nullptr, int n = -1) const override;
    bool isEmpty() const override { return m_map.isEmpty(); }
private:
    QHash<QString, QString> m_map;
};

namespace i18n {

struct Language {
    QString code;   // file name without extension, for example "ko" or "zh_CN"
    QString name;   // native name shown in the settings
    QString path;
};

QString translationsDir();            ///< Folder that contains the *.tsv files (next to the executable).
QList<Language> available();
/// Installs the translator for 'code' ("auto" follows the system language). Returns the code that was used.
QString install(const QString& code);

}  // namespace i18n
