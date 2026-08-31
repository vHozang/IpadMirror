#pragma once

// RaopReceiver: owns the vendored UxPlay RAOP/AirPlay receiver (raop_t) and its
// service-discovery registration (dnssd_t), and bridges UxPlay's C callbacks
// into PadMirror's shared media ingress. This is the only file that touches the
// UxPlay C API; the rest of PadMirror stays transport-neutral.
//
// Lifecycle mirrors UxPlay's start_raop_server ordering:
//   raop_init -> raop_init2 -> set ports -> raop_start_httpd (bind) ->
//   dnssd_init -> raop_set_dnssd -> dnssd_register_raop/airplay (LAST).
// Service records are only published after the listener has bound, so the iPad
// never sees an advertised-but-dead receiver.

#include "core/MirrorTypes.h"
#include "media/AirPlayClockSynchronizer.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace padmirror::core {
class IMediaIngress;
}

namespace padmirror::airplay {

// Callback context shared with the UxPlay C callbacks. Defined in the .cpp;
// held by shared_ptr so its lifetime is independent of RaopReceiver teardown
// ordering and survives any in-flight callback. Namespace-scoped (not nested)
// so the extern "C" trampolines in the .cpp can name it.
struct RaopReceiverContext;

class RaopReceiver final {
public:
    struct StartResult {
        bool started = false;
        std::string detail;
        std::uint16_t port = 0;
    };

    explicit RaopReceiver(core::IMediaIngress& mediaIngress);
    ~RaopReceiver();

    RaopReceiver(const RaopReceiver&) = delete;
    RaopReceiver& operator=(const RaopReceiver&) = delete;

    // Binds the RAOP/RTSP listener and publishes the _raop._tcp / _airplay._tcp
    // records. receiverName is the display name shown on the iPad. Returns
    // started=false with a human-readable detail on any failure; on failure all
    // native resources are released and nothing is advertised.
    StartResult start(const std::string& receiverName);

    // Unregisters the service records and tears down the native receiver. Safe
    // to call when not started. Blocks until UxPlay's worker threads have
    // stopped so no callback can fire against a destroyed context.
    void stop();

    [[nodiscard]] bool running() const;

private:
    void releaseLocked();

    core::IMediaIngress& mediaIngress_;
    std::shared_ptr<RaopReceiverContext> context_;

    mutable std::mutex mutex_;
    void* raop_ = nullptr;   // raop_t*
    void* dnssd_ = nullptr;  // dnssd_t*
    bool raopRegistered_ = false;
    bool airplayRegistered_ = false;
    bool running_ = false;
    std::uint16_t port_ = 0;
};

} // namespace padmirror::airplay
