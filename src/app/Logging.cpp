#include "app/Logging.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

#include <cstdio>
#include <memory>
#include <mutex>

namespace padmirror::app {
namespace {

std::mutex logMutex;
std::unique_ptr<QFile> logFile;

const char* levelName(QtMsgType type) {
    switch (type) {
    case QtDebugMsg: return "DEBUG";
    case QtInfoMsg: return "INFO";
    case QtWarningMsg: return "WARN";
    case QtCriticalMsg: return "ERROR";
    case QtFatalMsg: return "FATAL";
    }
    return "LOG";
}

void messageHandler(QtMsgType type, const QMessageLogContext&, const QString& message) {
    std::lock_guard lock(logMutex);
    const auto line = QStringLiteral("%1 [%2] %3\n")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
             QString::fromLatin1(levelName(type)),
             message);
    if (logFile && logFile->isOpen()) {
        logFile->write(line.toUtf8());
        logFile->flush();
    }
    std::fputs(line.toLocal8Bit().constData(), stderr);
    if (type == QtFatalMsg) std::abort();
}

} // namespace

void installLogging() {
    const auto directoryPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/logs");
    QDir().mkpath(directoryPath);
    logFile = std::make_unique<QFile>(directoryPath + QStringLiteral("/padmirror.log"));
    logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    qInstallMessageHandler(messageHandler);
}

} // namespace padmirror::app
