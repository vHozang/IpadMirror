#include "app/MirrorCoordinator.h"

#include <utility>

namespace padmirror::app {

MirrorCoordinator::MirrorCoordinator(
    core::IMediaIngress& mediaIngress,
    core::IMirrorBackend& usbBackend,
    core::IMirrorBackend& airPlayBackend)
    : mediaIngress_(mediaIngress),
      usbBackend_(usbBackend),
      airPlayBackend_(airPlayBackend) {}

MirrorCoordinator::~MirrorCoordinator() {
    stop();
}

void MirrorCoordinator::setMediaHandlers(MediaArmHandler arm, MediaTeardownHandler teardown) {
    std::lock_guard lock(mutex_);
    mediaArm_ = std::move(arm);
    mediaTeardown_ = std::move(teardown);
}

bool MirrorCoordinator::startUsb() {
    return start(core::MirrorTransport::Usb, usbBackend_);
}

bool MirrorCoordinator::enableAirPlay() {
    return start(core::MirrorTransport::AirPlay, airPlayBackend_);
}

void MirrorCoordinator::stop() {
    std::lock_guard lock(mutex_);
    stopLocked();
}

core::MirrorTransport MirrorCoordinator::transport() const {
    std::lock_guard lock(mutex_);
    return transport_;
}

core::MirrorState MirrorCoordinator::state() const {
    std::lock_guard lock(mutex_);
    return state_;
}

core::MirrorHealth MirrorCoordinator::health() const {
    std::lock_guard lock(mutex_);
    return health_;
}

core::IMirrorBackend* MirrorCoordinator::activeBackendLocked() {
    switch (transport_) {
    case core::MirrorTransport::Usb:
        return &usbBackend_;
    case core::MirrorTransport::AirPlay:
        return &airPlayBackend_;
    case core::MirrorTransport::None:
        break;
    }
    return nullptr;
}

bool MirrorCoordinator::start(core::MirrorTransport transport, core::IMirrorBackend& backend) {
    std::lock_guard lock(mutex_);
    stopLocked();

    transport_ = transport;
    state_ = core::MirrorState::Connecting;

    // 1. Preflight the backend before opening any media path. A backend that
    //    cannot become available must never cause the pipeline to start.
    if (!backend.initialize()) {
        health_ = backend.health();
        state_ = core::MirrorState::Error;
        transport_ = core::MirrorTransport::None;
        return false;
    }

    // 2. Flush so the new transport begins on a fresh epoch, then arm the shared
    //    media pipeline before the backend can emit any packet.
    mediaIngress_.signalDiscontinuity(core::DiscontinuityReason::TransportSwitch);
    if (mediaArm_) {
        if (!mediaArm_()) {
            backend.shutdown();
            health_ = backend.health();
            state_ = core::MirrorState::Error;
            transport_ = core::MirrorTransport::None;
            mediaIngress_.endOfStream();
            return false;
        }
        mediaArmed_ = true;
    }

    // 3. Activate the backend last, permitting callbacks into media ingress.
    if (!backend.start()) {
        health_ = backend.health();
        backend.shutdown();
        if (mediaArmed_ && mediaTeardown_) {
            mediaTeardown_();
            mediaArmed_ = false;
        }
        mediaIngress_.endOfStream();
        state_ = core::MirrorState::Error;
        transport_ = core::MirrorTransport::None;
        return false;
    }

    health_ = backend.health();
    state_ = backend.state();
    return true;
}

void MirrorCoordinator::stopLocked() {
    if (auto* backend = activeBackendLocked()) {
        backend->stop();
        backend->shutdown();
    }
    if (mediaArmed_ && mediaTeardown_) {
        mediaTeardown_();
        mediaArmed_ = false;
    }
    if (transport_ != core::MirrorTransport::None) {
        mediaIngress_.signalDiscontinuity(core::DiscontinuityReason::StreamRestart);
        mediaIngress_.endOfStream();
    }
    transport_ = core::MirrorTransport::None;
    state_ = core::MirrorState::Stopped;
    health_ = {};
}

} // namespace padmirror::app
