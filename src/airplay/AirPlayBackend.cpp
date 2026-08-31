#include "airplay/AirPlayBackend.h"

#include "airplay/RaopReceiver.h"
#include "core/IMediaIngress.h"
#include "core/MirrorTypes.h"

namespace padmirror::airplay {

AirPlayBackend::AirPlayBackend(core::IMediaIngress& mediaIngress)
    : mediaIngress_(mediaIngress) {}

AirPlayBackend::~AirPlayBackend() {
    shutdown();
}

bool AirPlayBackend::initialize() {
    std::lock_guard lock(mutex_);
    if (!receiver_) {
        receiver_ = std::make_unique<RaopReceiver>(mediaIngress_);
    }
    // The receiver's real availability is only known once the listener binds
    // and the mDNS records register (in start()). initialize() just confirms
    // the backend is constructible and ready to attempt a start.
    health_ = {
        .available = true,
        .trusted = true,
        .detail = "AirPlay receiver ready to start",
    };
    state_ = core::MirrorState::Stopped;
    return true;
}

void AirPlayBackend::shutdown() {
    stop();
    std::lock_guard lock(mutex_);
    receiver_.reset();
}

bool AirPlayBackend::start() {
    std::lock_guard lock(mutex_);
    if (!receiver_) {
        receiver_ = std::make_unique<RaopReceiver>(mediaIngress_);
    }

    const auto result = receiver_->start("PadMirror");
    if (!result.started) {
        health_ = {
            .available = false,
            .trusted = false,
            .detail = "AirPlay unavailable: " + result.detail,
        };
        state_ = core::MirrorState::Error;
        return false;
    }

    health_ = {
        .available = true,
        .trusted = true,
        .detail = result.detail,
    };
    // Records are published; wait for an iPad to connect from the AirPlay picker.
    state_ = core::MirrorState::Waiting;
    return true;
}

void AirPlayBackend::stop() {
    std::lock_guard lock(mutex_);
    if (receiver_) {
        receiver_->stop();
    }
    if (state_ != core::MirrorState::Error) {
        state_ = core::MirrorState::Stopped;
    }
}

core::MirrorState AirPlayBackend::state() const {
    std::lock_guard lock(mutex_);
    return state_;
}

core::MirrorHealth AirPlayBackend::health() const {
    std::lock_guard lock(mutex_);
    return health_;
}

} // namespace padmirror::airplay
