#include "app/AppController.h"
#include "app/Logging.h"
#include "app/MainWindow.h"
#include "app/RuntimeDependencies.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QMessageBox>
#include <QPixmap>
#include <QProcess>
#include <QQuickStyle>
#include <QTextStream>
#include <QTimer>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("PadMirror"));
    QCoreApplication::setApplicationName(QStringLiteral("PadMirror"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.6"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    padmirror::app::installLogging();
    padmirror::app::RuntimeDependencies::configureEnvironment();

    const auto arguments = QCoreApplication::arguments();
    const auto missingRuntime = padmirror::app::RuntimeDependencies::missingComponents();
    if (arguments.contains(QStringLiteral("--check-runtime"))) {
        QString report = missingRuntime.isEmpty()
            ? QStringLiteral("OK\n")
            : QStringLiteral("MISSING\n%1\n").arg(missingRuntime.join(QLatin1Char('\n')));
        for (const auto& argument : arguments) {
            const auto prefix = QStringLiteral("--check-runtime-file=");
            if (!argument.startsWith(prefix)) continue;
            QFile file(argument.mid(prefix.size()));
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream(&file) << report;
            }
        }
        return missingRuntime.isEmpty() ? 0 : 2;
    }

    if (!missingRuntime.isEmpty()) {
        const bool repairAttempted = arguments.contains(QStringLiteral("--runtime-repair-attempted"));
        if (!repairAttempted && padmirror::app::RuntimeDependencies::runBundledRepair()) {
            padmirror::app::RuntimeDependencies::configureEnvironment();
            auto restartArguments = arguments.mid(1);
            restartArguments.push_back(QStringLiteral("--runtime-repair-attempted"));
            QProcess::startDetached(QCoreApplication::applicationFilePath(), restartArguments);
            return 0;
        }

        QMessageBox::critical(
            nullptr,
            QStringLiteral("PadMirror runtime is incomplete"),
            QStringLiteral("PadMirror could not load the required media components:\n\n%1\n\n"
                           "Run PadMirrorSetup.exe again to repair the installation.")
                .arg(missingRuntime.join(QLatin1Char('\n'))));
        return 2;
    }

    padmirror::app::AppController controller;
    padmirror::app::MainWindow window(&controller);
    if (controller.settings()->fullscreenOnLaunch()) window.showFullScreen();
    else window.show();

    QString uiCapturePath;
    for (const auto& argument : arguments) {
        const auto prefix = QStringLiteral("--capture-ui=");
        if (argument.startsWith(prefix)) uiCapturePath = argument.mid(prefix.size());
    }
    if (!uiCapturePath.isEmpty()) {
        QTimer::singleShot(1500, &window, [&application, &window, uiCapturePath] {
            application.exit(window.grab().save(uiCapturePath) ? 0 : 3);
        });
    } else {
        QTimer::singleShot(0, &controller, &padmirror::app::AppController::start);
    }
    return application.exec();
}
