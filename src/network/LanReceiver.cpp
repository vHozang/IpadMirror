#include "network/LanReceiver.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QNetworkAddressEntry>
#include <QNetworkInterface>
#include <QProcessEnvironment>
#include <QStandardPaths>

#include <algorithm>

namespace padmirror::network {
namespace {

struct LanInterfaceInfo {
    QString address;
    QString hardwareAddress;
    QString name;
    int score = 0;
};

bool isPrivateIpv4(const QHostAddress& address) {
    const auto value = address.toIPv4Address();
    return (value & 0xff000000U) == 0x0a000000U ||
        (value & 0xfff00000U) == 0xac100000U ||
        (value & 0xffff0000U) == 0xc0a80000U;
}

bool looksVirtual(const QNetworkInterface& interface) {
    const auto label = (interface.name() + QLatin1Char(' ') + interface.humanReadableName()).toLower();
    static const QStringList virtualMarkers = {
        QStringLiteral("vethernet"),
        QStringLiteral("hyper-v"),
        QStringLiteral("wsl"),
        QStringLiteral("tailscale"),
        QStringLiteral("vmware"),
        QStringLiteral("virtualbox"),
        QStringLiteral("loopback"),
        QStringLiteral("bluetooth"),
    };
    for (const auto& marker : virtualMarkers) {
        if (label.contains(marker)) return true;
    }
    return interface.type() == QNetworkInterface::Virtual ||
        interface.type() == QNetworkInterface::Loopback ||
        interface.type() == QNetworkInterface::Ppp;
}

QList<LanInterfaceInfo> physicalLanInterfaces() {
    QList<LanInterfaceInfo> result;
    for (const auto& interface : QNetworkInterface::allInterfaces()) {
        const auto flags = interface.flags();
        if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning) ||
            !(flags & QNetworkInterface::CanMulticast) ||
            (flags & QNetworkInterface::IsLoopBack) || (flags & QNetworkInterface::IsPointToPoint) ||
            looksVirtual(interface)) {
            continue;
        }

        for (const auto& entry : interface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol || entry.ip().isLoopback() ||
                !isPrivateIpv4(entry.ip())) {
                continue;
            }
            int score = 100;
            if (interface.type() == QNetworkInterface::Wifi) score += 300;
            if (interface.type() == QNetworkInterface::Ethernet) score += 250;
            if (!interface.hardwareAddress().isEmpty()) score += 20;
            if (entry.prefixLength() >= 16 && entry.prefixLength() <= 24) score += 10;
            result.push_back({
                entry.ip().toString(),
                interface.hardwareAddress().replace(QLatin1Char('-'), QLatin1Char(':')),
                interface.humanReadableName(),
                score,
            });
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.score > right.score;
    });
    return result;
}

} // namespace

LanReceiver::LanReceiver(QObject* parent)
    : QObject(parent) {
    process_.setProcessChannelMode(QProcess::MergedChannels);
    firewallProcess_.setProcessChannelMode(QProcess::MergedChannels);
    connect(&process_, &QProcess::started, this, &LanReceiver::started);
    connect(&process_, &QProcess::readyReadStandardOutput, this, [this] {
        const auto output = QString::fromLocal8Bit(process_.readAllStandardOutput());
        for (const auto& line : output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            emit logLine(line.trimmed());
        }
    });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (!stopping_ && error != QProcess::Crashed) {
            emit errorOccurred(QStringLiteral("UxPlay cannot start: %1").arg(process_.errorString()));
            if (error == QProcess::FailedToStart) emit stopped();
        }
    });
    connect(&process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int exitCode, QProcess::ExitStatus status) {
            const bool expected = stopping_;
            stopping_ = false;
            if (!expected && (status == QProcess::CrashExit || exitCode != 0)) {
                emit errorOccurred(QStringLiteral("UxPlay stopped unexpectedly (code %1)").arg(exitCode));
            }
            emit stopped();
        });
    connect(&firewallProcess_, &QProcess::readyReadStandardOutput, this, [this] {
        const auto output = QString::fromLocal8Bit(firewallProcess_.readAllStandardOutput());
        for (const auto& line : output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            emit logLine(line.trimmed());
        }
    });
    connect(&firewallProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (!pendingStart_ || error != QProcess::FailedToStart) return;
        pendingStart_ = false;
        emit errorOccurred(QStringLiteral("Windows Firewall setup cannot start: %1")
            .arg(firewallProcess_.errorString()));
        emit stopped();
    });
    connect(&firewallProcess_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int exitCode, QProcess::ExitStatus status) {
            if (!pendingStart_) return;
            if (status != QProcess::NormalExit || exitCode != 0) {
                pendingStart_ = false;
                const auto details = QString::fromLocal8Bit(firewallProcess_.readAll()).trimmed();
                emit errorOccurred(details.isEmpty()
                    ? QStringLiteral("Windows Firewall permission was not granted for AirPlay.")
                    : QStringLiteral("Windows Firewall setup failed: %1").arg(details));
                emit stopped();
                return;
            }
            startPreparedReceiver();
        });
}

LanReceiver::~LanReceiver() {
    stop();
}

bool LanReceiver::start(const QString& configuredPath, std::uint16_t videoPort, std::uint16_t audioPort) {
    stop();
    const auto executable = findUxPlay(configuredPath);
    if (executable.isEmpty()) {
        emit errorOccurred(QStringLiteral("UxPlay was not found. Configure its executable in Wi-Fi settings."));
        return false;
    }

    const auto lanInterfaces = physicalLanInterfaces();
    if (lanInterfaces.isEmpty()) {
        emit errorOccurred(QStringLiteral(
            "No physical private IPv4 network was found. Connect this PC and iPad to the same LAN."));
        return false;
    }
    const auto lan = lanInterfaces.front();

    const auto videoForward = QStringLiteral(
        "config-interval=-1 ! udpsink host=127.0.0.1 port=%1 "
        "buffer-size=4194304 sync=false async=false")
        .arg(videoPort);
    const auto audioForward = QStringLiteral(
        "pt=96 ! udpsink host=127.0.0.1 port=%1 sync=false async=false")
        .arg(audioPort);
    QStringList arguments = {
        QStringLiteral("-n"), QStringLiteral("PadMirror"),
        QStringLiteral("-nh"),
        QStringLiteral("-fps"), QStringLiteral("60"),
        QStringLiteral("-s"), QStringLiteral("1920x1080@60"),
        QStringLiteral("-vsync"), QStringLiteral("no"),
        QStringLiteral("-p"), QStringLiteral("7100,7101,7102"),
        QStringLiteral("-vs"), QStringLiteral("fakesink sync=false async=false"),
        QStringLiteral("-as"), QStringLiteral("fakesink sync=false async=false"),
        QStringLiteral("-vrtp"), videoForward,
        QStringLiteral("-artp"), audioForward,
    };
    if (!lan.hardwareAddress.isEmpty()) {
        arguments.push_back(QStringLiteral("-m"));
        arguments.push_back(lan.hardwareAddress);
    }

    auto environment = QProcessEnvironment::systemEnvironment();
#ifdef Q_OS_WIN
    const QDir executableDir(QFileInfo(executable).absolutePath());
    process_.setWorkingDirectory(executableDir.absolutePath());
    const auto bundledPlugins = executableDir.filePath(QStringLiteral("gstreamer-1.0"));
    if (QFileInfo::exists(bundledPlugins)) {
        environment.insert(QStringLiteral("GST_PLUGIN_PATH_1_0"), bundledPlugins);
        environment.insert(QStringLiteral("GST_PLUGIN_SYSTEM_PATH_1_0"), bundledPlugins);
        const auto scanner = executableDir.filePath(
            QStringLiteral("libexec/gstreamer-1.0/gst-plugin-scanner.exe"));
        if (QFileInfo::exists(scanner)) {
            environment.insert(QStringLiteral("GST_PLUGIN_SCANNER_1_0"), QDir::toNativeSeparators(scanner));
        }
        const auto cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        if (!cacheDir.isEmpty() && QDir().mkpath(cacheDir)) {
            const auto executableKey = QCryptographicHash::hash(
                QDir::toNativeSeparators(executable).toLower().toUtf8(),
                QCryptographicHash::Sha256).toHex().left(12);
            environment.insert(
                QStringLiteral("GST_REGISTRY_1_0"),
                QDir(cacheDir).filePath(QStringLiteral("uxplay-gstreamer-registry-%1.bin")
                    .arg(QString::fromLatin1(executableKey))));
        }
        environment.insert(QStringLiteral("UXPLAY_MDNS_IPV4"), lan.address);
        const auto systemRoot = qEnvironmentVariable("SystemRoot", QStringLiteral("C:/Windows"));
        environment.insert(
            QStringLiteral("PATH"),
            QStringList{
                QDir::toNativeSeparators(executableDir.absolutePath()),
                QDir::toNativeSeparators(QDir(systemRoot).filePath(QStringLiteral("System32"))),
                QDir::toNativeSeparators(systemRoot),
            }.join(QDir::listSeparator()));
    }
#endif
    pendingProgram_ = executable;
    pendingArguments_ = arguments;
    pendingEnvironment_ = environment;
    pendingStart_ = true;

#ifdef Q_OS_WIN
    if (!qEnvironmentVariableIsSet("PADMIRROR_SKIP_FIREWALL_SETUP")) {
        const auto script = QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("dependencies/configure-wifi-firewall.ps1"));
        if (!QFileInfo::exists(script)) {
            pendingStart_ = false;
            emit errorOccurred(QStringLiteral("Wi-Fi firewall setup is missing. Reinstall PadMirror."));
            return false;
        }
        firewallProcess_.setProgram(QStringLiteral("powershell.exe"));
        firewallProcess_.setArguments({
            QStringLiteral("-NoProfile"),
            QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
            QStringLiteral("-File"), QDir::toNativeSeparators(script),
            QStringLiteral("-UxPlayPath"), QDir::toNativeSeparators(executable),
        });
        firewallProcess_.start();
        return true;
    }
#endif

    startPreparedReceiver();
    return true;
}

void LanReceiver::startPreparedReceiver() {
    if (!pendingStart_) return;
    pendingStart_ = false;
    stopping_ = false;
    process_.setProcessEnvironment(pendingEnvironment_);
    process_.setProgram(pendingProgram_);
    process_.setArguments(pendingArguments_);
    process_.start();
}

void LanReceiver::stop() {
    pendingStart_ = false;
    if (firewallProcess_.state() != QProcess::NotRunning) {
        firewallProcess_.terminate();
        if (!firewallProcess_.waitForFinished(1200)) {
            firewallProcess_.kill();
            firewallProcess_.waitForFinished(1200);
        }
    }
    if (process_.state() == QProcess::NotRunning) return;
    stopping_ = true;
    process_.terminate();
    if (!process_.waitForFinished(1200)) {
        process_.kill();
        process_.waitForFinished(1200);
    }
}

bool LanReceiver::running() const {
    return process_.state() != QProcess::NotRunning;
}

QString LanReceiver::findUxPlay(const QString& configuredPath) {
    if (!configuredPath.isEmpty() && QFileInfo::exists(configuredPath)) {
        return QFileInfo(configuredPath).absoluteFilePath();
    }
#ifdef Q_OS_WIN
    const auto bundled = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("uxplay/uxplay.exe"));
    if (QFileInfo::exists(bundled)) return bundled;
#endif
    if (const auto executable = QStandardPaths::findExecutable(QStringLiteral("uxplay")); !executable.isEmpty()) {
        return executable;
    }
#ifdef Q_OS_WIN
    const QStringList commonPaths = {
        QStringLiteral("C:/msys64/ucrt64/bin/uxplay.exe"),
        QStringLiteral("C:/msys64/mingw64/bin/uxplay.exe"),
    };
    for (const auto& path : commonPaths) {
        if (QFileInfo::exists(path)) return path;
    }
#endif
    return {};
}

QStringList LanReceiver::localIpv4Addresses() {
    QStringList addresses;
    for (const auto& interface : physicalLanInterfaces()) {
        addresses.push_back(interface.address);
    }
    addresses.removeDuplicates();
    return addresses;
}

} // namespace padmirror::network
