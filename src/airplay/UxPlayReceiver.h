#pragma once

#include "support/ReceiverConfig.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace padmirror::airplay {

struct ReceiverContext;

enum class ReceiverState {
    Starting,
    Waiting,
    Connecting,
    Streaming,
    Stopping,
    Error,
};

struct ReceiverDiagnostics {
    ReceiverState state = ReceiverState::Starting;
    std::string detail;
    std::string receiverName;
    std::uint16_t port = 0;
    int connectionCount = 0;
    std::string clientName;
    std::string clientModel;
    std::string videoCodec;
    int videoWidth = 0;
    int videoHeight = 0;
    double videoInputFps = 0.0;
    double videoSinkFps = 0.0;
    std::uint32_t videoQueueBuffers = 0;
    std::uint64_t videoQueueDropped = 0;
    std::uint64_t videoQosDropped = 0;
    std::uint64_t videoAppsrcFailures = 0;
    double videoInputPtsDeltaMs = 0.0;
    double videoSinkPtsDeltaMs = 0.0;
    double videoSinkPtsLeadMs = 0.0;
    std::string audioCodec;
    int audioSampleRate = 0;
    std::uint64_t audioAppsrcQueuedNs = 0;
    std::uint64_t audioQueueQueuedNs = 0;
    std::int64_t audioPtsLeadNs = 0;
    std::uint64_t audioAppsrcDropped = 0;
    std::uint64_t audioQueueDropped = 0;
    std::uint64_t audioAppsrcFailures = 0;
    std::uint64_t audioReanchors = 0;
    std::uint64_t audioConcealedFrames = 0;
    std::uint64_t audioDecoderResets = 0;
    std::uint64_t audioLatencyRecoveries = 0;
    int maxFps = 60;
};

class UxPlayReceiver final {
public:
    using StateCallback = std::function<void(ReceiverState, const std::string&)>;
    using DiagnosticsCallback = std::function<void(const ReceiverDiagnostics&)>;

    struct StartResult {
        bool started = false;
        std::string detail;
        std::uint16_t port = 0;
    };

    UxPlayReceiver(StateCallback stateCallback,
                   DiagnosticsCallback diagnosticsCallback = {});
    ~UxPlayReceiver();

    UxPlayReceiver(const UxPlayReceiver&) = delete;
    UxPlayReceiver& operator=(const UxPlayReceiver&) = delete;

    StartResult start(const support::ReceiverConfig& config,
                      std::uintptr_t windowHandle);
    void setWindowHandle(std::uintptr_t windowHandle);
    void setAudioOutput(int volumePercent, bool muted);
    void stop();

    [[nodiscard]] bool running() const;

private:
    void releaseLocked();

    StateCallback stateCallback_;
    DiagnosticsCallback diagnosticsCallback_;
    std::shared_ptr<ReceiverContext> context_;
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
