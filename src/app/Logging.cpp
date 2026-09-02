#include "app/Logging.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>

#include <cstdio>
#include <memory>
#include <mutex>

namespace padmirror::app {
namespace {

std::mutex logMutex;
std::unique_ptr<QFile> logFile;
constexpr qint64 maximumLogBytes = 4 * 1024 * 1024;

QString loggingDirectory() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/logs");
}

void rotateLog(const QString& path, bool everyLaunch = false) {
    const QFileInfo info(path);
    if (!info.exists() || (!everyLaunch && info.size() < maximumLogBytes)) {
        return;
    }
    QFile::remove(path + QStringLiteral(".1"));
    QFile::rename(path, path + QStringLiteral(".1"));
}

QByteArray sessionHeader() {
    return QStringLiteral(
               "=== PadMirror native session ===\n"
               "started=%1\n"
               "pid=%2\n"
               "version=%3\n"
               "executable=%4\n"
               "===============================\n")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs))
        .arg(QCoreApplication::applicationPid())
        .arg(QCoreApplication::applicationVersion(),
             QCoreApplication::applicationFilePath())
        .toUtf8();
}

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
    // Keep normal operation quiet when PadMirror is launched from a console.
    // The complete stream remains in the log file; set PADMIRROR_VERBOSE_LOG=1
    // when live informational output is needed for diagnostics.
    const bool verboseConsole = qEnvironmentVariableIntValue("PADMIRROR_VERBOSE_LOG") > 0;
    if (verboseConsole || type >= QtWarningMsg || type == QtFatalMsg) {
        std::fputs(line.toLocal8Bit().constData(), stderr);
    }
    if (type == QtFatalMsg) std::abort();
}

} // namespace

void installLogging() {
    const auto directoryPath = loggingDirectory();
    QDir().mkpath(directoryPath);
    const auto applicationLogPath =
        directoryPath + QStringLiteral("/padmirror.log");
    const auto gstreamerLogPath =
        directoryPath + QStringLiteral("/gstreamer.log");
    rotateLog(applicationLogPath);
    rotateLog(gstreamerLogPath, true);

    logFile = std::make_unique<QFile>(applicationLogPath);
    logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    qInstallMessageHandler(messageHandler);

    // The GUI subsystem has no attached console. Start a fresh native log on
    // every launch so hardware-test evidence always identifies one process.
    FILE* nativeLog = nullptr;
#if defined(_WIN32)
    const bool nativeLogOpened =
        freopen_s(
            &nativeLog,
            gstreamerLogPath.toLocal8Bit().constData(),
            "w",
            stderr) == 0 &&
        nativeLog;
#else
    nativeLog = std::freopen(
        gstreamerLogPath.toLocal8Bit().constData(),
        "w",
        stderr);
    const bool nativeLogOpened = nativeLog != nullptr;
#endif
    if (nativeLogOpened) {
        std::setvbuf(stderr, nullptr, _IOLBF, BUFSIZ);
        const auto header = sessionHeader();
        std::fwrite(header.constData(), 1, static_cast<std::size_t>(header.size()), stderr);
        std::fflush(stderr);
    }
}

QString logDirectoryPath() {
    return loggingDirectory();
}

} // namespace padmirror::app
