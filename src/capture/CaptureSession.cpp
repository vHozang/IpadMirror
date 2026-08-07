#include "capture/CaptureSession.h"

#include "media/MediaSession.h"

#include <QMetaObject>
#include <QPointer>

#include <chrono>
#include <thread>

namespace padmirror::capture {

CaptureSession::CaptureSession(media::MediaSession* mediaSession, QObject* parent)
    : QObject(parent),
      mediaSession_(mediaSession) {
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
}

CaptureSession::~CaptureSession() {
    stop();
}

void CaptureSession::start(const QString& serial) {
    stop();
    stopRequested_.store(false);
    running_.store(true);
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
}

void CaptureSession::stop() {
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

bool CaptureSession::waitBackoff() const {
    for (int tick = 0; tick < 10 && !stopRequested_.load(); ++tick) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return !stopRequested_.load();
}

} // namespace padmirror::capture
