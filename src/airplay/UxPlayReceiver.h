#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace padmirror::airplay {

enum class ReceiverState {
    Starting,
    Waiting,
    Streaming,
    Error,
};

class UxPlayReceiver final {
public:
    using StateCallback = std::function<void(ReceiverState, const std::string&)>;

    struct StartResult {
        bool started = false;
        std::string detail;
        std::uint16_t port = 0;
    };

    explicit UxPlayReceiver(StateCallback stateCallback);
    ~UxPlayReceiver();

    UxPlayReceiver(const UxPlayReceiver&) = delete;
    UxPlayReceiver& operator=(const UxPlayReceiver&) = delete;

    StartResult start(const std::string& receiverName, std::uintptr_t windowHandle);
    void setWindowHandle(std::uintptr_t windowHandle);
    void stop();

    [[nodiscard]] bool running() const;

private:
    struct Context;

    void releaseLocked();

    StateCallback stateCallback_;
    std::shared_ptr<Context> context_;
    mutable std::mutex mutex_;
    void* raop_ = nullptr;
    void* dnssd_ = nullptr;
    void* renderLogger_ = nullptr;
    bool renderersReady_ = false;
    bool raopRegistered_ = false;
    bool airplayRegistered_ = false;
    bool running_ = false;
    std::uint16_t port_ = 0;
};

} // namespace padmirror::airplay
