#include "app/MainWindow.h"
#include "app/RuntimeDependencies.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QIcon>
#include <QMessageBox>
#include <QTextStream>
#include <QTimer>

#ifdef Q_OS_WIN
#include <shobjidl_core.h>
#endif

int main(int argc, char* argv[]) {
#ifdef Q_OS_WIN
    SetCurrentProcessExplicitAppUserModelID(L"PadMirror.Desktop");
#endif

    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("PadMirror"));
    QCoreApplication::setApplicationName(QStringLiteral("PadMirror"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    application.setWindowIcon(QIcon(QStringLiteral(":/assets/icons/PadMirror-256.png")));

    padmirror::app::RuntimeDependencies::configureEnvironment();
    const auto arguments = QCoreApplication::arguments();
    const auto missing = padmirror::app::RuntimeDependencies::missingComponents();
    const auto features = padmirror::app::RuntimeDependencies::featureReport();

    if (arguments.contains(QStringLiteral("--check-runtime"))) {
        const auto report = missing.isEmpty()
            ? QStringLiteral("OK\n%1\n").arg(features.join(QLatin1Char('\n')))
            : QStringLiteral("MISSING\n%1\n%2\n")
                  .arg(missing.join(QLatin1Char('\n')), features.join(QLatin1Char('\n')));
        for (const auto& argument : arguments) {
            const auto prefix = QStringLiteral("--check-runtime-file=");
            if (!argument.startsWith(prefix)) {
                continue;
            }
            QFile file(argument.mid(prefix.size()));
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream(&file) << report;
            }
        }
        return missing.isEmpty() ? 0 : 2;
    }

    if (!missing.isEmpty()) {
        QMessageBox::critical(
            nullptr,
            QStringLiteral("PadMirror runtime is incomplete"),
            QStringLiteral("Required AirPlay media components are unavailable:\n\n%1\n\n"
                           "Run PadMirrorSetup.exe again to repair the installation.")
                .arg(missing.join(QLatin1Char('\n'))));
        return 2;
    }

    padmirror::app::MainWindow window;
    window.show();
    QTimer::singleShot(0, &window, &padmirror::app::MainWindow::startReceiver);
    return application.exec();
}
