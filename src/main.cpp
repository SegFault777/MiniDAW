// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QSettings>
#include "MainWindow.h"
#include "Theme.h"
#include "Translator.h"

#ifndef MINIDAW_VERSION
#define MINIDAW_VERSION "0.5"
#endif

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("MiniDAW");
    app.setApplicationVersion(MINIDAW_VERSION);
    app.setOrganizationName("MiniDAW");
    theme::apply(app);
    // The language must be installed before any widget is created so that every tr() call sees it.
    i18n::install(QSettings().value("ui/language", "auto").toString());

    MainWindow w;
    w.resize(1400, 840);
    w.show();
    // A project path passed on the command line (for example by the file association) is opened directly.
    if (argc > 1) w.openProject(QString::fromLocal8Bit(argv[1]));
    return app.exec();
}
