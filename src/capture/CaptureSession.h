#pragma once

#include "capture/usb/QuickTimeProtocol.h"
#include "capture/usb/UsbTransport.h"

#include <QObject>
#include <QString>

#include <atomic>
#include <thread>

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

private:
    void publishState(State state);
    void publishError(const std::string& message);
    bool waitBackoff() const;

    media::MediaSession* mediaSession_ = nullptr;
    usb::UsbTransport transport_;
    usb::QuickTimeProtocol protocol_;
    std::thread worker_;
    std::atomic_bool stopRequested_{false};
    std::atomic_bool running_{false};
};

} // namespace padmirror::capture
