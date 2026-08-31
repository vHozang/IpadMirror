#include "api/ApiRouter.h"

#include "api/Json.h"

#include <optional>
#include <string>

namespace padmirror::api {

namespace {

HttpResponse jsonResponse(int status, std::string body) {
    HttpResponse response;
    response.statusCode = status;
    response.contentType = "application/json";
    response.body = std::move(body);
    return response;
}

HttpResponse errorResponse(int status, const std::string& message) {
    json::Writer writer;
    writer.beginObject();
    writer.member("error", message);
    writer.endObject();
    return jsonResponse(status, writer.str());
}

HttpResponse methodNotAllowed(const std::string& allow) {
    HttpResponse response = errorResponse(405, "method not allowed");
    response.allow = allow;
    return response;
}

HttpResponse commandResponse(const ApiCommandResult& result) {
    if (result.ok) {
        json::Writer writer;
        writer.beginObject();
        writer.member("ok", true);
        writer.endObject();
        return jsonResponse(200, writer.str());
    }
    return errorResponse(409, result.error.empty() ? "command rejected" : result.error);
}

// Parses a JSON object request body, enforcing content-type and the bounded
// parser limits. Returns nullopt and fills `response` on any failure.
std::optional<json::Value> parseObjectBody(const HttpRequest& request, HttpResponse& response) {
    if (request.contentType != "application/json") {
        response = errorResponse(415, "expected application/json");
        return std::nullopt;
    }
    if (request.body.empty()) {
        response = errorResponse(400, "empty body");
        return std::nullopt;
    }
    json::ParseResult parsed =
        json::parse(request.body, ApiLimits::kMaxJsonDepth, ApiLimits::kMaxJsonNodes);
    if (!parsed.ok) {
        response = errorResponse(400, "invalid json: " + parsed.error);
        return std::nullopt;
    }
    if (!parsed.value.isObject()) {
        response = errorResponse(400, "expected json object");
        return std::nullopt;
    }
    return parsed.value;
}

// Reads an optional string member, rejecting a present-but-wrong type.
bool readOptionalString(const json::Value& obj, const std::string& key,
                        std::optional<std::string>& out, std::string& error) {
    const json::Value* value = obj.find(key);
    if (!value) return true;
    if (!value->isString()) { error = key + " must be a string"; return false; }
    out = value->asString();
    return true;
}

bool readOptionalBool(const json::Value& obj, const std::string& key,
                      std::optional<bool>& out, std::string& error) {
    const json::Value* value = obj.find(key);
    if (!value) return true;
    if (!value->isBool()) { error = key + " must be a boolean"; return false; }
    out = value->asBool();
    return true;
}

bool readOptionalInt(const json::Value& obj, const std::string& key,
                     std::optional<int>& out, std::string& error) {
    const json::Value* value = obj.find(key);
    if (!value) return true;
    if (!value->isNumber()) { error = key + " must be a number"; return false; }
    const double n = value->asNumber();
    if (n < -100000.0 || n > 100000.0) { error = key + " out of range"; return false; }
    out = static_cast<int>(n);
    return true;
}

void writeStatus(json::Writer& writer, const ApiStatus& status) {
    writer.beginObject();
    writer.member("transport", status.transport);
    writer.member("state", status.state);
    writer.member("available", status.available);
    writer.member("healthDetail", status.healthDetail);
    writer.member("sourceFps", status.sourceFps);
    writer.member("decodeFps", status.decodeFps);
    writer.member("outputFps", status.outputFps);
    writer.member("droppedFrames", static_cast<unsigned long long>(status.droppedFrames));
    writer.member("audioBufferMs", status.audioBufferMs);
    writer.member("audioBufferValid", status.audioBufferValid);
    writer.member("activeEpoch", static_cast<unsigned long long>(status.activeEpoch));
    writer.member("lastDiscontinuity", status.lastDiscontinuity);
    writer.member("videoCodec", status.videoCodec);
    writer.member("sourceResolution", status.sourceResolution);
    writer.member("requestedQuality", status.requestedQuality);
    writer.endObject();
}

void writeSettings(json::Writer& writer, const ApiSettings& settings) {
    writer.beginObject();
    writer.member("connectionMode", settings.connectionMode);
    writer.member("airPlayQuality", settings.airPlayQuality);
    writer.member("hardwareDecoder", settings.hardwareDecoder);
    writer.member("audioBufferMs", settings.audioBufferMs);
    writer.member("audioMode", settings.audioMode);
    writer.member("audioDevice", settings.audioDevice);
    writer.member("dropStaleFrames", settings.dropStaleFrames);
    writer.member("gamingMode", settings.gamingMode);
    writer.member("maintainAspectRatio", settings.maintainAspectRatio);
    writer.member("fullscreenOnLaunch", settings.fullscreenOnLaunch);
    writer.member("alwaysOnTop", settings.alwaysOnTop);
    writer.member("diagnosticsVisible", settings.diagnosticsVisible);
    writer.endObject();
}

} // namespace

HttpResponse ApiRouter::handle(const HttpRequest& request) {
    // Reject anything but the known method tokens outright.
    if (request.method == HttpMethod::Unknown) {
        return errorResponse(501, "unsupported method");
    }
    if (request.path == "/status") return handleStatus(request);
    if (request.path == "/devices") return handleDevices(request);
    if (request.path == "/settings") return handleSettings(request);
    if (request.path == "/session") return handleSession(request);
    if (request.path == "/settings/audio") return handleAudio(request);
    if (request.path == "/settings/video") return handleVideo(request);
    if (request.path == "/window") return handleWindow(request);
    return errorResponse(404, "not found");
}

HttpResponse ApiRouter::handleStatus(const HttpRequest& request) {
    if (request.method != HttpMethod::Get) return methodNotAllowed("GET");
    json::Writer writer;
    writeStatus(writer, service_.status());
    return jsonResponse(200, writer.str());
}

HttpResponse ApiRouter::handleDevices(const HttpRequest& request) {
    if (request.method != HttpMethod::Get) return methodNotAllowed("GET");
    const ApiDevices devices = service_.devices();
    json::Writer writer;
    writer.beginObject();
    writer.member("scanError", devices.scanError);
    writer.key("devices");
    writer.beginArray();
    for (const ApiDevice& device : devices.devices) {
        writer.beginObject();
        writer.member("name", device.name);
        writer.member("serial", device.serial);
        writer.member("osVersion", device.osVersion);
        writer.member("trusted", device.trusted);
        writer.member("trustKnown", device.trustKnown);
        writer.endObject();
    }
    writer.endArray();
    writer.endObject();
    return jsonResponse(200, writer.str());
}

HttpResponse ApiRouter::handleSettings(const HttpRequest& request) {
    if (request.method != HttpMethod::Get) return methodNotAllowed("GET");
    json::Writer writer;
    writeSettings(writer, service_.settings());
    return jsonResponse(200, writer.str());
}

HttpResponse ApiRouter::handleSession(const HttpRequest& request) {
    // POST /session {"transport":"usb|airplay"} starts a transport;
    // DELETE /session stops the active one.
    if (request.method == HttpMethod::Delete) {
        return commandResponse(service_.stop());
    }
    if (request.method != HttpMethod::Post) return methodNotAllowed("POST, DELETE");

    HttpResponse response;
    auto body = parseObjectBody(request, response);
    if (!body) return response;

    const json::Value* transport = body->find("transport");
    if (!transport || !transport->isString()) {
        return errorResponse(400, "transport must be a string");
    }
    const std::string& value = transport->asString();
    if (value == "usb") return commandResponse(service_.startUsb());
    if (value == "airplay") return commandResponse(service_.startAirPlay());
    return errorResponse(400, "transport must be 'usb' or 'airplay'");
}

HttpResponse ApiRouter::handleAudio(const HttpRequest& request) {
    if (request.method != HttpMethod::Patch) return methodNotAllowed("PATCH");
    HttpResponse response;
    auto body = parseObjectBody(request, response);
    if (!body) return response;

    AudioPatch patch;
    std::string error;
    if (!readOptionalInt(*body, "audioBufferMs", patch.audioBufferMs, error) ||
        !readOptionalString(*body, "audioMode", patch.audioMode, error) ||
        !readOptionalString(*body, "audioDevice", patch.audioDevice, error)) {
        return errorResponse(400, error);
    }
    if (!patch.audioBufferMs && !patch.audioMode && !patch.audioDevice) {
        return errorResponse(400, "no recognized fields");
    }
    return commandResponse(service_.applyAudioPatch(patch));
}

HttpResponse ApiRouter::handleVideo(const HttpRequest& request) {
    if (request.method != HttpMethod::Patch) return methodNotAllowed("PATCH");
    HttpResponse response;
    auto body = parseObjectBody(request, response);
    if (!body) return response;

    VideoPatch patch;
    std::string error;
    if (!readOptionalString(*body, "hardwareDecoder", patch.hardwareDecoder, error) ||
        !readOptionalBool(*body, "dropStaleFrames", patch.dropStaleFrames, error) ||
        !readOptionalBool(*body, "gamingMode", patch.gamingMode, error) ||
        !readOptionalBool(*body, "maintainAspectRatio", patch.maintainAspectRatio, error)) {
        return errorResponse(400, error);
    }
    if (!patch.hardwareDecoder && !patch.dropStaleFrames && !patch.gamingMode &&
        !patch.maintainAspectRatio) {
        return errorResponse(400, "no recognized fields");
    }
    return commandResponse(service_.applyVideoPatch(patch));
}

HttpResponse ApiRouter::handleWindow(const HttpRequest& request) {
    if (request.method != HttpMethod::Patch) return methodNotAllowed("PATCH");
    HttpResponse response;
    auto body = parseObjectBody(request, response);
    if (!body) return response;

    WindowPatch patch;
    std::string error;
    if (!readOptionalString(*body, "action", patch.action, error) ||
        !readOptionalBool(*body, "alwaysOnTop", patch.alwaysOnTop, error) ||
        !readOptionalBool(*body, "diagnosticsVisible", patch.diagnosticsVisible, error)) {
        return errorResponse(400, error);
    }
    if (patch.action) {
        const std::string& action = *patch.action;
        if (action != "fullscreen" && action != "toggleFullscreen" &&
            action != "minimize" && action != "maximize" && action != "close") {
            return errorResponse(400, "unknown window action");
        }
    }
    if (!patch.action && !patch.alwaysOnTop && !patch.diagnosticsVisible) {
        return errorResponse(400, "no recognized fields");
    }
    return commandResponse(service_.applyWindowPatch(patch));
}

} // namespace padmirror::api
