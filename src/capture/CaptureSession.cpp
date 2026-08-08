#include "capture/CaptureSession.h"

#include "media/MediaSession.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QtEndian>

#ifndef _WIN32
#include <chrono>
#include <thread>
#endif

namespace padmirror::capture {

CaptureSession::CaptureSession(media::MediaSession* mediaSession, QObject* parent)
    : QObject(parent),
      mediaSession_(mediaSession) {
#ifdef _WIN32
    bridgeProcess_.setProcessChannelMode(QProcess::SeparateChannels);
    connect(&bridgeProcess_, &QProcess::readyReadStandardOutput, this, &CaptureSession::consumeBridgeOutput);
    connect(&bridgeProcess_, &QProcess::readyReadStandardError, this, &CaptureSession::consumeBridgeErrors);
    connect(&bridgeProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (stopRequested_.load() || error == QProcess::Crashed) return;
        bridgeFatalError_ = true;
        running_.store(false);
        publishError("Cannot start the safe Apple USB bridge");
        publishState(State::Disconnected);
    });
    connect(
        &bridgeProcess_,
        qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
        this,
        [this](int exitCode, QProcess::ExitStatus exitStatus) {
            if (stopRequested_.load()) return;
            running_.store(false);
            if (!bridgeFatalError_ && (exitStatus != QProcess::NormalExit || exitCode != 0)) {
                publishError("The safe Apple USB bridge stopped unexpectedly");
            }
            publishState(State::Disconnected);
        });
#else
    protocol_.setWriter([this](std::span<const std::uint8_t> packet) {
        return transport_.write(packet);
    });
    protocol_.setVideoHandler([this](VideoPacket packet) {
        if (mediaSession_) mediaSession_->pushVideo(std::move(packet));
    });
    protocol_.setAudioHandler([this](AudioPacket packet) {
        if (mediaSession_) mediaSession_->pushAudio(std::move(packet));
    });
    protocol_.setErrorHandler([this](const std::string& message) {
        publishError(message);
    });
    protocol_.setReadyHandler([this] {
        publishState(State::Streaming);
    });
#endif
}

CaptureSession::~CaptureSession() {
    stop();
}

void CaptureSession::start(const QString& serial) {
    stop();
    stopRequested_.store(false);
    running_.store(true);
#ifdef _WIN32
    selectedSerial_ = serial;
    bridgeFatalError_ = false;
    bridgeOutput_.clear();
    bridgeErrors_.clear();
    const auto program = bridgeProgram();
    if (program.isEmpty()) {
        bridgeFatalError_ = true;
        running_.store(false);
        publishError("The safe Apple USB bridge is missing. Reinstall PadMirror.");
        publishState(State::Disconnected);
        return;
    }
    bridgeProcess_.setProgram(program);
    bridgeProcess_.setArguments({});
    bridgeProcess_.setWorkingDirectory(QFileInfo(program).absolutePath());
    publishState(State::StartingCapture);
    bridgeProcess_.start(QIODevice::ReadOnly);
#else
    const auto selectedSerial = serial.toStdString();
    worker_ = std::thread([this, selectedSerial] {
        while (!stopRequested_.load()) {
            protocol_.reset();
            publishState(State::StartingCapture);
            const auto result = transport_.run(
                selectedSerial,
                [this](std::span<const std::uint8_t> frame) {
                    return protocol_.processFrame(frame);
                },
                [this](const std::string& message) {
                    publishError(message);
                },
                [this] { return stopRequested_.load(); });

            if (stopRequested_.load() || result == usb::UsbTransport::RunResult::Stopped) {
                break;
            }
            publishState(result == usb::UsbTransport::RunResult::Disconnected
                ? State::Disconnected
                : State::Recovering);
            if (!waitBackoff()) {
                break;
            }
            publishState(State::Recovering);
        }
        running_.store(false);
        publishState(State::Disconnected);
    });
#endif
}

void CaptureSession::stop() {
#ifdef _WIN32
    stopRequested_.store(true);
    if (bridgeProcess_.state() != QProcess::NotRunning) {
        bridgeProcess_.terminate();
        if (!bridgeProcess_.waitForFinished(1500)) {
            bridgeProcess_.kill();
            bridgeProcess_.waitForFinished(1500);
        }
    }
    bridgeOutput_.clear();
    bridgeErrors_.clear();
    running_.store(false);
#else
    if (!worker_.joinable()) {
        running_.store(false);
        return;
    }
    if (transport_.connected()) {
        protocol_.beginClose();
        protocol_.waitForRelease(std::chrono::milliseconds(400));
        protocol_.finishClose();
    }
    stopRequested_.store(true);
    transport_.requestStop();
    worker_.join();
    running_.store(false);
#endif
}

bool CaptureSession::running() const {
    return running_.load();
}

void CaptureSession::publishState(State state) {
    QPointer<CaptureSession> guard(this);
    QMetaObject::invokeMethod(this, [guard, state] {
        if (guard) emit guard->stateChanged(state);
    }, Qt::QueuedConnection);
}

void CaptureSession::publishError(const std::string& message) {
    const auto text = QString::fromStdString(message);
    QPointer<CaptureSession> guard(this);
    QMetaObject::invokeMethod(this, [guard, text] {
        if (guard) emit guard->errorOccurred(text);
    }, Qt::QueuedConnection);
}

#ifdef _WIN32
QString CaptureSession::bridgeProgram() const {
    const QDir applicationDir(QCoreApplication::applicationDirPath());
    const QStringList candidates = {
        applicationDir.filePath(QStringLiteral("usb-bridge/PadMirrorUsbBridge.exe")),
        applicationDir.filePath(QStringLiteral("../stage/usb-bridge/PadMirrorUsbBridge.exe")),
        applicationDir.filePath(QStringLiteral("../portable-package/usb-bridge/PadMirrorUsbBridge.exe")),
    };
    for (const auto& candidate : candidates) {
        if (QFileInfo::exists(candidate)) return QDir::cleanPath(candidate);
    }
    return {};
}

void CaptureSession::consumeBridgeOutput() {
    bridgeOutput_.append(bridgeProcess_.readAllStandardOutput());
    constexpr qsizetype headerSize = 13;
    constexpr quint32 maximumPayload = 32U * 1024U * 1024U;
    while (bridgeOutput_.size() >= headerSize) {
        const auto* bytes = reinterpret_cast<const uchar*>(bridgeOutput_.constData());
        const auto type = bytes[0];
        const auto ptsNs = qFromBigEndian<quint64>(bytes + 1);
        const auto payloadSize = qFromBigEndian<quint32>(bytes + 9);
        if (payloadSize == 0 || payloadSize > maximumPayload) {
            bridgeFatalError_ = true;
            bridgeProcess_.kill();
            publishError("The Apple USB bridge returned an invalid media frame");
            return;
        }
        const auto frameSize = headerSize + static_cast<qsizetype>(payloadSize);
        if (bridgeOutput_.size() < frameSize) return;
        const auto payload = bridgeOutput_.mid(headerSize, payloadSize);
        bridgeOutput_.remove(0, frameSize);

        if (!mediaSession_) continue;
        if (type == 1 || type == 2) {
            VideoPacket packet;
            packet.ptsNs = ptsNs;
            packet.keyFrame = type == 1;
            packet.format.codec = VideoCodec::Hevc;
            packet.data.assign(
                reinterpret_cast<const std::uint8_t*>(payload.constData()),
                reinterpret_cast<const std::uint8_t*>(payload.constData()) + payload.size());
            mediaSession_->pushVideo(std::move(packet));
        } else if (type == 3) {
            AudioPacket packet;
            packet.ptsNs = ptsNs;
            packet.format.codec = AudioCodec::AacEld;
            packet.data.assign(
                reinterpret_cast<const std::uint8_t*>(payload.constData()),
                reinterpret_cast<const std::uint8_t*>(payload.constData()) + payload.size());
            mediaSession_->pushAudio(std::move(packet));
        }
    }
}

void CaptureSession::consumeBridgeErrors() {
    bridgeErrors_.append(bridgeProcess_.readAllStandardError());
    while (true) {
        const auto newline = bridgeErrors_.indexOf('\n');
        if (newline < 0) return;
        auto line = bridgeErrors_.left(newline);
        bridgeErrors_.remove(0, newline + 1);
        if (line.endsWith('\r')) line.chop(1);
        handleBridgeLine(line);
    }
}

void CaptureSession::handleBridgeLine(const QByteArray& line) {
    static const QByteArray prefix("PADMIRROR ");
    if (!line.startsWith(prefix)) return;
    QJsonParseError parseError{};
    const auto document = QJsonDocument::fromJson(line.mid(prefix.size()), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) return;
    const auto object = document.object();
    const auto event = object.value(QStringLiteral("event")).toString();
    const auto message = object.value(QStringLiteral("message")).toString();
    const auto code = object.value(QStringLiteral("code")).toString();
    if (event == QStringLiteral("device")) {
        emit stateChanged(State::DeviceFound);
    } else if (event == QStringLiteral("mounting") || event == QStringLiteral("tunnel")) {
        emit stateChanged(State::Pairing);
    } else if (event == QStringLiteral("streaming")) {
        emit stateChanged(State::Streaming);
    } else if (event == QStringLiteral("error")) {
        bridgeFatalError_ = true;
        if (code == QStringLiteral("usb_video_requires_ios27")) {
            emit wifiFallbackRequested(message);
            return;
        }
        if (!message.isEmpty()) publishError(message.toStdString());
        return;
    }
    if (!message.isEmpty()) emit statusOccurred(message);
}
#else
bool CaptureSession::waitBackoff() const {
    for (int tick = 0; tick < 10 && !stopRequested_.load(); ++tick) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return !stopRequested_.load();
}
#endif

} // namespace padmirror::capture
