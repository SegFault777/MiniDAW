#include <QApplication>
#include <QPalette>
#include <QStyleFactory>
#include "MainWindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("LiteDAW");
    app.setStyle(QStyleFactory::create("Fusion"));

    QPalette p;
    p.setColor(QPalette::Window, QColor(37, 37, 40));
    p.setColor(QPalette::WindowText, QColor(230, 230, 230));
    p.setColor(QPalette::Base, QColor(30, 30, 33));
    p.setColor(QPalette::AlternateBase, QColor(42, 42, 46));
    p.setColor(QPalette::Text, QColor(230, 230, 230));
    p.setColor(QPalette::Button, QColor(56, 56, 61));
    p.setColor(QPalette::ButtonText, QColor(230, 230, 230));
    p.setColor(QPalette::Highlight, QColor(255, 159, 28));
    p.setColor(QPalette::HighlightedText, Qt::black);
    p.setColor(QPalette::ToolTipBase, QColor(50, 50, 55));
    p.setColor(QPalette::ToolTipText, QColor(230, 230, 230));
    app.setPalette(p);

    MainWindow w;
    w.resize(1280, 760);
    w.show();
    return app.exec();
}
