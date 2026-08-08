#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>

#include <atomic>
#ifndef _WIN32
#include "capture/usb/QuickTimeProtocol.h"
#include "capture/usb/UsbTransport.h"
#include <thread>
#endif

namespace padmirror::media {
class MediaSession;
}

namespace padmirror::capture {

class CaptureSession final : public QObject {
    Q_OBJECT

public:
    enum class State {
        Disconnected,
        DeviceFound,
        Pairing,
        StartingCapture,
        Streaming,
        Recovering,
    };
    Q_ENUM(State)

    explicit CaptureSession(media::MediaSession* mediaSession, QObject* parent = nullptr);
    ~CaptureSession() override;

    void start(const QString& serial);
    void stop();
    [[nodiscard]] bool running() const;

signals:
    void stateChanged(padmirror::capture::CaptureSession::State state);
    void errorOccurred(const QString& message);
    void statusOccurred(const QString& message);
    void wifiFallbackRequested(const QString& message);

private:
    void publishState(State state);
    void publishError(const std::string& message);
#ifdef _WIN32
    void consumeBridgeOutput();
    void consumeBridgeErrors();
    void handleBridgeLine(const QByteArray& line);
    [[nodiscard]] QString bridgeProgram() const;
#else
    bool waitBackoff() const;
#endif

    media::MediaSession* mediaSession_ = nullptr;
#ifdef _WIN32
    QProcess bridgeProcess_;
    QByteArray bridgeOutput_;
    QByteArray bridgeErrors_;
    QString selectedSerial_;
    bool bridgeFatalError_ = false;
#else
    usb::UsbTransport transport_;
    usb::QuickTimeProtocol protocol_;
    std::thread worker_;
#endif
    std::atomic_bool stopRequested_{false};
    std::atomic_bool running_{false};
};

} // namespace padmirror::capture
