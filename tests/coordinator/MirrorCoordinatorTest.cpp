#include "app/MirrorCoordinator.h"

#include "core/IMediaIngress.h"
#include "core/IMirrorBackend.h"
#include "core/MirrorTypes.h"

#include <cassert>
#include <string>
#include <vector>

using namespace padmirror;

namespace {

// Records the ordering of every lifecycle event so tests can assert the exact
// arm-before-start and stop-before-teardown sequence the plan requires.
struct EventLog {
    std::vector<std::string> events;
    void add(std::string event) { events.push_back(std::move(event)); }
    [[nodiscard]] bool contains(const std::string& event) const {
        for (const auto& e : events)
            if (e == event) return true;
        return false;
    }
    [[nodiscard]] int indexOf(const std::string& event) const {
        for (std::size_t i = 0; i < events.size(); ++i)
            if (events[i] == event) return static_cast<int>(i);
        return -1;
    }
};

class FakeIngress final : public core::IMediaIngress {
public:
    explicit FakeIngress(EventLog& log) : log_(log) {}
    bool pushVideo(core::EncodedVideoPacket) override { return true; }
    bool pushAudio(core::EncodedAudioPacket) override { return true; }
    std::uint64_t signalDiscontinuity(core::DiscontinuityReason) override {
        log_.add("ingress.flush");
        return ++epoch_;
    }
    void endOfStream() override { log_.add("ingress.eos"); }

private:
    EventLog& log_;
    std::uint64_t epoch_ = 0;
};

class FakeBackend final : public core::IMirrorBackend {
public:
    FakeBackend(EventLog& log, std::string name) : log_(log), name_(std::move(name)) {}

    bool initialize() override {
        log_.add(name_ + ".initialize");
        health_.available = initOk_;
        health_.detail = initOk_ ? "ok" : "unavailable";
        state_ = initOk_ ? core::MirrorState::Waiting : core::MirrorState::Error;
        return initOk_;
    }
    void shutdown() override { log_.add(name_ + ".shutdown"); }
    bool start() override {
        log_.add(name_ + ".start");
        if (!startOk_) {
            state_ = core::MirrorState::Error;
            health_.available = false;
            health_.detail = "start failed";
            return false;
        }
        state_ = core::MirrorState::Streaming;
        return true;
    }
    void stop() override {
        log_.add(name_ + ".stop");
        state_ = core::MirrorState::Stopped;
    }
    [[nodiscard]] core::MirrorState state() const override { return state_; }
    [[nodiscard]] core::MirrorHealth health() const override { return health_; }

    bool initOk_ = true;
    bool startOk_ = true;

private:
    EventLog& log_;
    std::string name_;
    core::MirrorState state_ = core::MirrorState::Stopped;
    core::MirrorHealth health_;
};

} // namespace

int main() {
    // 1. Happy path: arm media before backend start, flush on transport switch.
    {
        EventLog log;
        FakeIngress ingress(log);
        FakeBackend usb(log, "usb");
        FakeBackend airplay(log, "airplay");
        app::MirrorCoordinator coordinator(ingress, usb, airplay);
        int armCount = 0;
        int teardownCount = 0;
        coordinator.setMediaHandlers(
            [&] { log.add("media.arm"); ++armCount; return true; },
            [&] { log.add("media.teardown"); ++teardownCount; });

        assert(coordinator.startUsb());
        assert(coordinator.transport() == core::MirrorTransport::Usb);
        assert(coordinator.state() == core::MirrorState::Streaming);
        // initialize -> flush -> arm -> start ordering.
        assert(log.indexOf("usb.initialize") < log.indexOf("ingress.flush"));
        assert(log.indexOf("ingress.flush") < log.indexOf("media.arm"));
        assert(log.indexOf("media.arm") < log.indexOf("usb.start"));
    }

    // 2. USB XOR AirPlay: switching stops the prior transport first.
    {
        EventLog log;
        FakeIngress ingress(log);
        FakeBackend usb(log, "usb");
        FakeBackend airplay(log, "airplay");
        app::MirrorCoordinator coordinator(ingress, usb, airplay);
        coordinator.setMediaHandlers([&] { return true; }, [&] { log.add("media.teardown"); });

        assert(coordinator.startUsb());
        assert(coordinator.enableAirPlay());
        assert(coordinator.transport() == core::MirrorTransport::AirPlay);
        // The USB backend must be stopped and shut down before AirPlay starts.
        assert(log.indexOf("usb.stop") < log.indexOf("airplay.start"));
        assert(log.indexOf("usb.shutdown") < log.indexOf("airplay.start"));
    }

    // 3. Preflight failure: media is never armed, no transport remains.
    {
        EventLog log;
        FakeIngress ingress(log);
        FakeBackend usb(log, "usb");
        FakeBackend airplay(log, "airplay");
        usb.initOk_ = false;
        app::MirrorCoordinator coordinator(ingress, usb, airplay);
        bool armed = false;
        coordinator.setMediaHandlers([&] { armed = true; return true; }, [&] {});

        assert(!coordinator.startUsb());
        assert(!armed);
        assert(!log.contains("usb.start"));
        assert(coordinator.transport() == core::MirrorTransport::None);
        assert(coordinator.state() == core::MirrorState::Error);
    }

    // 4. Backend start failure after arming: media torn down, EOS emitted.
    {
        EventLog log;
        FakeIngress ingress(log);
        FakeBackend usb(log, "usb");
        FakeBackend airplay(log, "airplay");
        usb.startOk_ = false;
        app::MirrorCoordinator coordinator(ingress, usb, airplay);
        coordinator.setMediaHandlers([&] { log.add("media.arm"); return true; },
                                     [&] { log.add("media.teardown"); });

        assert(!coordinator.startUsb());
        assert(log.contains("media.arm"));
        assert(log.contains("media.teardown"));
        assert(log.indexOf("usb.start") < log.indexOf("media.teardown"));
        assert(coordinator.transport() == core::MirrorTransport::None);
        assert(coordinator.state() == core::MirrorState::Error);
    }

    // 5. Media arm failure: backend is shut down, no active transport.
    {
        EventLog log;
        FakeIngress ingress(log);
        FakeBackend usb(log, "usb");
        FakeBackend airplay(log, "airplay");
        app::MirrorCoordinator coordinator(ingress, usb, airplay);
        coordinator.setMediaHandlers([&] { return false; }, [&] {});

        assert(!coordinator.startUsb());
        assert(!log.contains("usb.start"));
        assert(log.contains("usb.shutdown"));
        assert(coordinator.transport() == core::MirrorTransport::None);
    }

    // 6. Repeated stop is idempotent and leaves a clean stopped state.
    {
        EventLog log;
        FakeIngress ingress(log);
        FakeBackend usb(log, "usb");
        FakeBackend airplay(log, "airplay");
        app::MirrorCoordinator coordinator(ingress, usb, airplay);
        coordinator.setMediaHandlers([&] { return true; }, [&] {});

        assert(coordinator.startUsb());
        coordinator.stop();
        coordinator.stop();
        assert(coordinator.transport() == core::MirrorTransport::None);
        assert(coordinator.state() == core::MirrorState::Stopped);
        assert(coordinator.health().available == false);
    }

    return 0;
}
