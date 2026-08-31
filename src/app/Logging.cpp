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
    const auto directoryPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/logs");
    QDir().mkpath(directoryPath);
    logFile = std::make_unique<QFile>(directoryPath + QStringLiteral("/padmirror.log"));
    logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    qInstallMessageHandler(messageHandler);

    // This is a WIN32 GUI subsystem app, so anything written to stderr (the
    // GStreamer default log sink, plus PadMirror's own pipeline bus diagnostics
    // such as the [audio-eld] burst-governor lines) is otherwise discarded --
    // there is no attached console. Always redirect stderr to a dedicated file
    // so those native-layer diagnostics can be inspected after a session. This
    // is safe by default: GStreamer only emits to stderr at WARNING/ERROR unless
    // GST_DEBUG is set, so the file does not grow unboundedly in normal use.
    {
        const auto gstLogPath = directoryPath + QStringLiteral("/gstreamer.log");
        std::freopen(gstLogPath.toLocal8Bit().constData(), "a", stderr);
    }
}

} // namespace padmirror::app
