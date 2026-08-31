#pragma once

// Loopback-only automation API (milestone M8).
//
// This header defines the transport-neutral contract between the pure request
// router (padmirror_core, Qt/GStreamer-free and unit-testable) and the Qt
// application adapter that fulfils the requests. Nothing here depends on Qt,
// GStreamer or sockets, so the whole routing/validation surface can be tested
// with the portable core test harness.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace padmirror::api {

// Hard, fixed bounds. The loopback server refuses anything larger before it
// dispatches, so a local misbehaving client cannot exhaust memory.
struct ApiLimits {
    static constexpr std::uint16_t kPort = 17890;
    static constexpr std::size_t kMaxRequestLineBytes = 8 * 1024;
    static constexpr std::size_t kMaxHeaderBytes = 16 * 1024;
    static constexpr std::size_t kMaxHeaderCount = 64;
    static constexpr std::size_t kMaxBodyBytes = 64 * 1024;
    static constexpr std::size_t kMaxJsonDepth = 16;
    static constexpr std::size_t kMaxJsonNodes = 4096;
    static constexpr std::size_t kMaxConnections = 8;
    static constexpr int kIdleTimeoutMs = 15000;
    static constexpr std::size_t kMaxWebSocketMessageBytes = 8 * 1024;
};

enum class HttpMethod {
    Unknown,
    Get,
    Post,
    Patch,
    Delete,
    Put,
    Head,
    Options,
};

// A fully parsed, size-bounded HTTP/1.1 request. `path` excludes any query
// string; `query` holds the raw query (unused by current routes but preserved
// for validation). `contentType` is lower-cased without parameters.
struct HttpRequest {
    HttpMethod method = HttpMethod::Unknown;
    std::string rawMethod;
    std::string path;
    std::string query;
    std::string contentType;
    std::string body;
    bool keepAlive = false;
};

struct HttpResponse {
    int statusCode = 200;
    std::string contentType = "application/json";
    std::string body;
    // Populated for 405 responses so the server can emit an Allow header.
    std::string allow;
};

struct ApiCommandResult {
    bool ok = false;
    std::string error;

    static ApiCommandResult success() { return {true, {}}; }
    static ApiCommandResult failure(std::string message) { return {false, std::move(message)}; }
};

// ---- Read-model snapshots (pure data; the adapter fills them) ---------------

struct ApiStatus {
    std::string transport = "none";   // none | usb | airplay
    std::string state = "stopped";    // stopped | waiting | connecting | streaming | recovering | error
    bool available = false;
    std::string healthDetail;
    double sourceFps = 0.0;
    double decodeFps = 0.0;
    double outputFps = 0.0;
    std::uint64_t droppedFrames = 0;
    double audioBufferMs = 0.0;
    bool audioBufferValid = false;
    std::uint64_t activeEpoch = 0;
    std::string lastDiscontinuity;
    std::string videoCodec;
    std::string sourceResolution;
    std::string requestedQuality;
};

struct ApiDevice {
    std::string name;
    std::string serial;
    std::string osVersion;
    bool trusted = false;
    bool trustKnown = false;
};

struct ApiDevices {
    std::vector<ApiDevice> devices;
    std::string scanError;
};

struct ApiSettings {
    std::string connectionMode = "usb";     // usb | airplay
    std::string airPlayQuality = "auto";    // auto | p1080_60
    std::string hardwareDecoder = "Auto";
    int audioBufferMs = 10;
    std::string audioMode = "lowLatency";   // lowLatency | exclusive | safe
    std::string audioDevice;
    bool dropStaleFrames = true;
    bool gamingMode = true;
    bool maintainAspectRatio = true;
    bool fullscreenOnLaunch = false;
    bool alwaysOnTop = false;
    bool diagnosticsVisible = false;
};

// ---- Command payloads (only present fields are applied) ----------------------

struct AudioPatch {
    std::optional<int> audioBufferMs;
    std::optional<std::string> audioMode;
    std::optional<std::string> audioDevice;
};

struct VideoPatch {
    std::optional<std::string> hardwareDecoder;
    std::optional<bool> dropStaleFrames;
    std::optional<bool> gamingMode;
    std::optional<bool> maintainAspectRatio;
};

struct WindowPatch {
    std::optional<std::string> action;   // fullscreen | toggleFullscreen | minimize | maximize | close
    std::optional<bool> alwaysOnTop;
    std::optional<bool> diagnosticsVisible;
};

// The application-side surface the router drives. Command methods run on the
// caller's thread; the Qt adapter marshals them onto the UI thread. Keeping the
// interface pure lets the router be tested against fakes.
class IApiService {
public:
    virtual ~IApiService() = default;

    virtual ApiStatus status() = 0;
    virtual ApiDevices devices() = 0;
    virtual ApiSettings settings() = 0;

    virtual ApiCommandResult startUsb() = 0;
    virtual ApiCommandResult startAirPlay() = 0;
    virtual ApiCommandResult stop() = 0;

    virtual ApiCommandResult applyAudioPatch(const AudioPatch& patch) = 0;
    virtual ApiCommandResult applyVideoPatch(const VideoPatch& patch) = 0;
    virtual ApiCommandResult applyWindowPatch(const WindowPatch& patch) = 0;
};

} // namespace padmirror::api
