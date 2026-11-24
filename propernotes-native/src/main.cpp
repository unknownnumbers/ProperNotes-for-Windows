// SPDX-License-Identifier: GPL-3.0-only
#include "MainWindow.h"

#include <QApplication>
#include <QGuiApplication>
#include <QMessageBox>
#include <exception>

int main(int argc, char* argv[])
{
    // Process-local settings only. The portable package supplies its own Qt
    // plugins; a machine-wide Qt development environment is not required.
    qunsetenv("QT_PLUGIN_PATH");
    qunsetenv("QT_QPA_PLATFORM_PLUGIN_PATH");
#ifdef Q_OS_WIN
    qputenv("QT_QPA_PLATFORM", "windows");
#endif
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ProperNotes"));
    QCoreApplication::setApplicationName(QStringLiteral("ProperNotes for Windows"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.3.16-native-preview.1"));
    try {
        propernotes::MainWindow window;
        window.show();
        const QStringList arguments = QCoreApplication::arguments();
        if (arguments.size() > 1)
            window.openFile(arguments.at(1));
        return app.exec();
    } catch (const std::exception& error) {
        QMessageBox::critical(nullptr, QStringLiteral("ProperNotes for Windows"),
            QStringLiteral("无法初始化原生窗口: %1").arg(QString::fromUtf8(error.what())));
        return 1;
    }
}
