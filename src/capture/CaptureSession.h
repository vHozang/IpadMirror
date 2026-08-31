#pragma once

#include <QObject>
#include <QString>

#include "capture/usb/QuickTimeProtocol.h"
#include "capture/usb/UsbTransport.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

namespace padmirror::core {
class IMediaIngress;
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
        ReceiverReady,
        Streaming,
        Recovering,
    };
    Q_ENUM(State)

    explicit CaptureSession(core::IMediaIngress* mediaIngress, QObject* parent = nullptr);
    ~CaptureSession() override;

    void start(const QString& serial);
    void stop();
    [[nodiscard]] bool running() const;

signals:
    void stateChanged(padmirror::capture::CaptureSession::State state);
    void errorOccurred(const QString& message);
    void statusOccurred(const QString& message);

private:
    void publishState(State state);
    void publishError(const std::string& message);
    void publishErrorRateLimited(const std::string& message);
    bool waitBackoff(std::chrono::milliseconds delay) const;

    core::IMediaIngress* mediaIngress_ = nullptr;
    usb::UsbTransport transport_;
    usb::QuickTimeProtocol protocol_;
    std::thread worker_;
    std::atomic_bool stopRequested_{false};
    std::atomic_bool running_{false};
    std::atomic_bool receiverReadyReported_{false};
    std::atomic_bool videoReported_{false};
    mutable std::mutex errorMutex_;
    std::string lastError_;
    std::chrono::steady_clock::time_point lastErrorAt_{};
};

} // namespace padmirror::capture
