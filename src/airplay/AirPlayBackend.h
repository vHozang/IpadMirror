#pragma once

#include "core/IMirrorBackend.h"

#include <memory>
#include <mutex>

namespace padmirror::core {
class IMediaIngress;
}

namespace padmirror::airplay {

class RaopReceiver;

// AirPlayBackend adapts the in-process UxPlay RAOP receiver (RaopReceiver) to
// the IMirrorBackend contract the coordinator drives. initialize() reports
// whether the receiver can be brought up; start() binds the listener and
// publishes the AirPlay/RAOP service records so an iPad can connect.
class AirPlayBackend final : public core::IMirrorBackend {
public:
    explicit AirPlayBackend(core::IMediaIngress& mediaIngress);
    ~AirPlayBackend() override;

    bool initialize() override;
    void shutdown() override;
    bool start() override;
    void stop() override;
    [[nodiscard]] core::MirrorState state() const override;
    [[nodiscard]] core::MirrorHealth health() const override;

private:
    core::IMediaIngress& mediaIngress_;
    std::unique_ptr<RaopReceiver> receiver_;
    mutable std::mutex mutex_;
    core::MirrorState state_ = core::MirrorState::Stopped;
    core::MirrorHealth health_;
};

} // namespace padmirror::airplay
