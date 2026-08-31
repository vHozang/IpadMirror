#include "app/RuntimeDependencies.h"

#include <QCoreApplication>
#include <QFile>
#include <QTextStream>

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("PadMirror"));
    QCoreApplication::setApplicationName(QStringLiteral("PadMirrorRuntimeCheck"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    padmirror::app::RuntimeDependencies::configureEnvironment();
    const auto missing = padmirror::app::RuntimeDependencies::missingComponents();
    const auto features = padmirror::app::RuntimeDependencies::featureReport();
    const auto report = missing.isEmpty()
        ? QStringLiteral("OK\n%1\n").arg(features.join(QLatin1Char('\n')))
        : QStringLiteral("MISSING\n%1\n%2\n")
              .arg(missing.join(QLatin1Char('\n')), features.join(QLatin1Char('\n')));

    QTextStream(stdout) << report;
    for (const auto& argument : QCoreApplication::arguments()) {
        const auto prefix = QStringLiteral("--report=");
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
