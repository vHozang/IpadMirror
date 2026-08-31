#pragma once

#include "core/IMediaIngress.h"
#include "core/IMirrorBackend.h"
#include "core/MirrorTypes.h"

#include <functional>
#include <mutex>

namespace padmirror::app {

class MirrorCoordinator final {
public:
    // Prepares the shared media pipeline for a new transport. Returns true when
    // media is armed and ready to accept packets. Supplied by the application so
    // the coordinator stays free of Qt/GStreamer.
    using MediaArmHandler = std::function<bool()>;
    using MediaTeardownHandler = std::function<void()>;

    MirrorCoordinator(
        core::IMediaIngress& mediaIngress,
        core::IMirrorBackend& usbBackend,
        core::IMirrorBackend& airPlayBackend);
    ~MirrorCoordinator();

    void setMediaHandlers(MediaArmHandler arm, MediaTeardownHandler teardown);

    bool startUsb();
    bool enableAirPlay();
    void stop();

    [[nodiscard]] core::MirrorTransport transport() const;
    [[nodiscard]] core::MirrorState state() const;
    [[nodiscard]] core::MirrorHealth health() const;

private:
    bool start(core::MirrorTransport transport, core::IMirrorBackend& backend);
    void stopLocked();
    [[nodiscard]] core::IMirrorBackend* activeBackendLocked();

    core::IMediaIngress& mediaIngress_;
    core::IMirrorBackend& usbBackend_;
    core::IMirrorBackend& airPlayBackend_;
    MediaArmHandler mediaArm_;
    MediaTeardownHandler mediaTeardown_;
    mutable std::mutex mutex_;
    core::MirrorTransport transport_ = core::MirrorTransport::None;
    core::MirrorState state_ = core::MirrorState::Stopped;
    core::MirrorHealth health_;
    bool mediaArmed_ = false;
};

} // namespace padmirror::app
