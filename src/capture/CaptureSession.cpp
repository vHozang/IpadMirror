#include "capture/CaptureSession.h"

#include "capture/usb/AppleAvChannelFactory.h"
#include "core/IMediaIngress.h"
#include "core/MirrorTypes.h"

#include <QMetaObject>
#include <QPointer>

#include <algorithm>
#include <chrono>
#include <memory>
#include <thread>
#include <utility>

namespace padmirror::capture {
namespace {

core::VideoCodec toCoreCodec(VideoCodec codec) {
    return codec == VideoCodec::Hevc ? core::VideoCodec::Hevc : core::VideoCodec::H264;
}

core::AudioCodec toCoreCodec(AudioCodec codec) {
    switch (codec) {
    case AudioCodec::PcmS16Le: return core::AudioCodec::PcmS16Le;
    case AudioCodec::AacEld: return core::AudioCodec::AacEld;
    case AudioCodec::Unknown: return core::AudioCodec::Unknown;
    }
    return core::AudioCodec::Unknown;
}

core::EncodedVideoPacket toEncodedPacket(VideoPacket packet) {
    core::EncodedVideoPacket encoded;
    encoded.codec = toCoreCodec(packet.format.codec);
    encoded.payload = std::make_shared<const std::vector<std::uint8_t>>(std::move(packet.data));
    encoded.ptsNs = packet.ptsNs;
    encoded.keyframe = packet.keyFrame;
    encoded.format.codec = encoded.codec;
    encoded.format.width = packet.format.width;
    encoded.format.height = packet.format.height;
    encoded.format.sps = std::move(packet.format.sps);
    encoded.format.pps = std::move(packet.format.pps);
    return encoded;
}

core::EncodedAudioPacket toEncodedPacket(AudioPacket packet) {
    core::EncodedAudioPacket encoded;
    encoded.codec = toCoreCodec(packet.format.codec);
    encoded.payload = std::make_shared<const std::vector<std::uint8_t>>(std::move(packet.data));
    encoded.ptsNs = packet.ptsNs;
    encoded.format.codec = encoded.codec;
    encoded.format.sampleRate = packet.format.sampleRate;
    encoded.format.channels = packet.format.channels;
    encoded.format.bitsPerChannel = packet.format.bitsPerChannel;
    return encoded;
}

} // namespace

CaptureSession::CaptureSession(core::IMediaIngress* mediaIngress, QObject* parent)
    : QObject(parent),
      mediaIngress_(mediaIngress) {
    protocol_.setWriter([this](std::span<const std::uint8_t> packet) {
        return transport_.write(packet);
    });
    protocol_.setVideoHandler([this](VideoPacket packet) {
        if (mediaIngress_) mediaIngress_->pushVideo(toEncodedPacket(std::move(packet)));
        if (!videoReported_.exchange(true)) publishState(State::Streaming);
    });
    protocol_.setAudioHandler([this](AudioPacket packet) {
        if (mediaIngress_) mediaIngress_->pushAudio(toEncodedPacket(std::move(packet)));
    });
    protocol_.setErrorHandler([this](const std::string& message) {
        publishErrorRateLimited(message);
    });
    protocol_.setReadyHandler([this] {
        if (!receiverReadyReported_.exchange(true)) publishState(State::ReceiverReady);
    });
}

CaptureSession::~CaptureSession() {
    stop();
}

void CaptureSession::start(const QString& serial) {
    stop();
    const auto channel = usb::AppleAvChannelFactory::create();
    if (channel.status != usb::AppleAvChannelFactory::Status::Available) {
        publishError(channel.detail);
        publishState(State::Disconnected);
        return;
    }
    stopRequested_.store(false);
    {
        std::lock_guard lock(errorMutex_);
        lastError_.clear();
        lastErrorAt_ = {};
    }
    running_.store(true);
    const auto selectedSerial = serial.toStdString();
    worker_ = std::thread([this, selectedSerial] {
        std::chrono::milliseconds retryDelay{1000};
        while (!stopRequested_.load()) {
            receiverReadyReported_.store(false);
            videoReported_.store(false);
            protocol_.reset();
            if (mediaIngress_) {
                mediaIngress_->signalDiscontinuity(core::DiscontinuityReason::TransportReconnect);
            }
            publishState(State::StartingCapture);
            const auto result = transport_.run(
                selectedSerial,
                [this](std::span<const std::uint8_t> frame) {
                    return protocol_.processFrame(frame);
                },
                [this](const std::string& message) {
                    publishErrorRateLimited(message);
                },
                [this] { return stopRequested_.load(); });

            if (stopRequested_.load() || result == usb::UsbTransport::RunResult::Stopped) break;
            publishState(result == usb::UsbTransport::RunResult::Disconnected
                ? State::Disconnected
                : State::Recovering);
            if (!waitBackoff(retryDelay)) break;
            retryDelay = videoReported_.load()
                ? std::chrono::milliseconds(1000)
                : std::min(retryDelay * 2, std::chrono::milliseconds(10000));
            publishState(State::Recovering);
        }
        if (mediaIngress_) mediaIngress_->endOfStream();
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

void CaptureSession::publishErrorRateLimited(const std::string& message) {
    if (message.empty()) return;
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard lock(errorMutex_);
        if (lastErrorAt_ != std::chrono::steady_clock::time_point{}) {
            const auto elapsed = now - lastErrorAt_;
            if (elapsed < std::chrono::seconds(2) ||
                (message == lastError_ && elapsed < std::chrono::seconds(10))) {
                return;
            }
        }
        lastError_ = message;
        lastErrorAt_ = now;
    }
    publishError(message);
}

bool CaptureSession::waitBackoff(std::chrono::milliseconds delay) const {
    constexpr auto step = std::chrono::milliseconds(100);
    for (auto elapsed = std::chrono::milliseconds(0);
         elapsed < delay && !stopRequested_.load(); elapsed += step) {
        std::this_thread::sleep_for(std::min(step, delay - elapsed));
    }
    return !stopRequested_.load();
}

} // namespace padmirror::capture
