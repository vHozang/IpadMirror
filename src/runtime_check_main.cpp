#include "app/RuntimeDependencies.h"

#include <QCoreApplication>
#include <QFile>
#include <QTextStream>

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("PadMirror"));
    QCoreApplication::setApplicationName(QStringLiteral("PadMirror"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.10"));
    padmirror::app::RuntimeDependencies::configureEnvironment();

    const auto missing = padmirror::app::RuntimeDependencies::missingComponents();
    const auto report = missing.isEmpty()
        ? QStringLiteral("OK\n")
        : QStringLiteral("MISSING\n%1\n").arg(missing.join(QLatin1Char('\n')));
    QTextStream(stdout) << report;

    for (const auto& argument : QCoreApplication::arguments()) {
        const auto prefix = QStringLiteral("--report=");
        if (!argument.startsWith(prefix)) continue;
        QFile file(argument.mid(prefix.size()));
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) QTextStream(&file) << report;
    }
    return missing.isEmpty() ? 0 : 2;
}
