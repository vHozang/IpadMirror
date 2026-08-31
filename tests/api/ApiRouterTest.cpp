#include "api/ApiContracts.h"
#include "api/ApiRouter.h"
#include "api/HttpParser.h"
#include "api/Json.h"

#include <cassert>
#include <string>

using namespace padmirror;
using namespace padmirror::api;

namespace {

// Records every command so tests can assert routing and payload mapping without
// any Qt/socket dependency.
struct FakeService final : IApiService {
    ApiStatus statusValue;
    ApiDevices devicesValue;
    ApiSettings settingsValue;

    std::string lastCommand;
    AudioPatch lastAudio;
    VideoPatch lastVideo;
    WindowPatch lastWindow;
    bool failNext = false;
    std::string failMessage = "rejected";

    ApiStatus status() override { lastCommand = "status"; return statusValue; }
    ApiDevices devices() override { lastCommand = "devices"; return devicesValue; }
    ApiSettings settings() override { lastCommand = "settings"; return settingsValue; }

    ApiCommandResult finish(std::string name) {
        lastCommand = std::move(name);
        if (failNext) { failNext = false; return ApiCommandResult::failure(failMessage); }
        return ApiCommandResult::success();
    }

    ApiCommandResult startUsb() override { return finish("startUsb"); }
    ApiCommandResult startAirPlay() override { return finish("startAirPlay"); }
    ApiCommandResult stop() override { return finish("stop"); }
    ApiCommandResult applyAudioPatch(const AudioPatch& p) override { lastAudio = p; return finish("audio"); }
    ApiCommandResult applyVideoPatch(const VideoPatch& p) override { lastVideo = p; return finish("video"); }
    ApiCommandResult applyWindowPatch(const WindowPatch& p) override { lastWindow = p; return finish("window"); }
};

HttpRequest makeRequest(HttpMethod method, std::string path, std::string body = {},
                        std::string contentType = "application/json") {
    HttpRequest request;
    request.method = method;
    request.rawMethod = "X";
    request.path = std::move(path);
    request.body = std::move(body);
    if (!request.body.empty()) request.contentType = std::move(contentType);
    return request;
}

bool bodyContains(const HttpResponse& response, const std::string& needle) {
    return response.body.find(needle) != std::string::npos;
}

void testHttpParser() {
    // Happy path GET, no body.
    {
        HttpRequest request;
        const std::string raw = "GET /status HTTP/1.1\r\nHost: localhost\r\n\r\n";
        auto outcome = parseHttpRequest(raw, request);
        assert(outcome.status == ParseStatus::Complete);
        assert(request.method == HttpMethod::Get);
        assert(request.path == "/status");
        assert(outcome.consumed == raw.size());
    }

    // Path with query string is split.
    {
        HttpRequest request;
        auto outcome = parseHttpRequest("GET /status?x=1 HTTP/1.1\r\n\r\n", request);
        assert(outcome.status == ParseStatus::Complete);
        assert(request.path == "/status");
        assert(request.query == "x=1");
    }

    // POST with a JSON body and Content-Length.
    {
        HttpRequest request;
        const std::string body = "{\"transport\":\"usb\"}";
        const std::string raw = "POST /session HTTP/1.1\r\nContent-Type: application/json\r\n"
                                "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
        auto outcome = parseHttpRequest(raw, request);
        assert(outcome.status == ParseStatus::Complete);
        assert(request.method == HttpMethod::Post);
        assert(request.contentType == "application/json");
        assert(request.body == body);
    }

    // Partial request → NeedMore (headers incomplete).
    {
        HttpRequest request;
        auto outcome = parseHttpRequest("GET /status HTTP/1.1\r\nHost: local", request);
        assert(outcome.status == ParseStatus::NeedMore);
    }

    // Partial body → NeedMore (declared length not yet received).
    {
        HttpRequest request;
        const std::string raw = "POST /session HTTP/1.1\r\nContent-Length: 20\r\n\r\n{\"a\":1}";
        auto outcome = parseHttpRequest(raw, request);
        assert(outcome.status == ParseStatus::NeedMore);
    }

    // Oversize body via Content-Length → 413 before waiting.
    {
        HttpRequest request;
        const std::string raw = "POST /session HTTP/1.1\r\nContent-Length: 999999\r\n\r\n";
        auto outcome = parseHttpRequest(raw, request);
        assert(outcome.status == ParseStatus::Error);
        assert(outcome.errorStatus == 413);
    }

    // Chunked transfer-encoding rejected (501).
    {
        HttpRequest request;
        const std::string raw = "POST /session HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n";
        auto outcome = parseHttpRequest(raw, request);
        assert(outcome.status == ParseStatus::Error);
        assert(outcome.errorStatus == 501);
    }

    // Malformed request line → 400.
    {
        HttpRequest request;
        auto outcome = parseHttpRequest("GARBAGE\r\n\r\n", request);
        assert(outcome.status == ParseStatus::Error);
        assert(outcome.errorStatus == 400);
    }

    // Too many headers → 431.
    {
        std::string raw = "GET /status HTTP/1.1\r\n";
        for (int i = 0; i < 70; ++i) raw += "X-H" + std::to_string(i) + ": v\r\n";
        raw += "\r\n";
        HttpRequest request;
        auto outcome = parseHttpRequest(raw, request);
        assert(outcome.status == ParseStatus::Error);
        assert(outcome.errorStatus == 431);
    }
}

void testJson() {
    // Depth limit is enforced.
    {
        std::string deep;
        for (int i = 0; i < 40; ++i) deep += "[";
        auto result = json::parse(deep, 16, 4096);
        assert(!result.ok);
    }
    // Node limit is enforced.
    {
        std::string many = "[";
        for (int i = 0; i < 100; ++i) many += "1,";
        many += "1]";
        auto result = json::parse(many, 16, 8);
        assert(!result.ok);
    }
    // Valid object round-trips its members.
    {
        auto result = json::parse("{\"a\":true,\"b\":\"x\",\"c\":12}", 16, 4096);
        assert(result.ok);
        assert(result.value.isObject());
        const json::Value* a = result.value.find("a");
        assert(a && a->isBool() && a->asBool());
        const json::Value* c = result.value.find("c");
        assert(c && c->isNumber() && c->asNumber() == 12);
    }
    // Trailing garbage rejected.
    {
        auto result = json::parse("{}   junk", 16, 4096);
        assert(!result.ok);
    }
    // Writer escapes strings.
    {
        json::Writer writer;
        writer.beginObject();
        writer.member("k", std::string("a\"b\n"));
        writer.endObject();
        const std::string out = writer.str();
        assert(out.find("\\\"") != std::string::npos);
        assert(out.find("\\n") != std::string::npos);
    }
}

void testRouterReads() {
    FakeService service;
    service.statusValue.transport = "usb";
    service.statusValue.state = "streaming";
    service.statusValue.audioBufferValid = true;
    service.statusValue.audioBufferMs = 22.5;
    ApiRouter router(service);

    // GET /status returns 200 with fields.
    {
        auto response = router.handle(makeRequest(HttpMethod::Get, "/status"));
        assert(response.statusCode == 200);
        assert(bodyContains(response, "\"transport\":\"usb\""));
        assert(bodyContains(response, "\"state\":\"streaming\""));
        assert(bodyContains(response, "\"audioBufferValid\":true"));
    }

    // GET /devices renders an array.
    {
        service.devicesValue.scanError = "USB_CAPTURE_UNSUPPORTED";
        ApiDevice device; device.name = "iPad"; device.trusted = true;
        service.devicesValue.devices.push_back(device);
        auto response = router.handle(makeRequest(HttpMethod::Get, "/devices"));
        assert(response.statusCode == 200);
        assert(bodyContains(response, "USB_CAPTURE_UNSUPPORTED"));
        assert(bodyContains(response, "\"name\":\"iPad\""));
    }

    // GET /settings.
    {
        auto response = router.handle(makeRequest(HttpMethod::Get, "/settings"));
        assert(response.statusCode == 200);
        assert(bodyContains(response, "connectionMode"));
    }

    // Unknown path → 404.
    {
        auto response = router.handle(makeRequest(HttpMethod::Get, "/nope"));
        assert(response.statusCode == 404);
    }

    // Wrong method on a read route → 405 with Allow.
    {
        auto response = router.handle(makeRequest(HttpMethod::Post, "/status",
                                                  "{\"a\":1}"));
        assert(response.statusCode == 405);
        assert(response.allow == "GET");
    }
}

void testRouterCommands() {
    FakeService service;
    ApiRouter router(service);

    // POST /session {"transport":"usb"} → startUsb.
    {
        auto response = router.handle(makeRequest(HttpMethod::Post, "/session",
                                                  "{\"transport\":\"usb\"}"));
        assert(response.statusCode == 200);
        assert(service.lastCommand == "startUsb");
    }
    // POST /session {"transport":"airplay"} → startAirPlay.
    {
        auto response = router.handle(makeRequest(HttpMethod::Post, "/session",
                                                  "{\"transport\":\"airplay\"}"));
        assert(response.statusCode == 200);
        assert(service.lastCommand == "startAirPlay");
    }
    // Bad transport value → 400, no command.
    {
        service.lastCommand.clear();
        auto response = router.handle(makeRequest(HttpMethod::Post, "/session",
                                                  "{\"transport\":\"lan\"}"));
        assert(response.statusCode == 400);
        assert(service.lastCommand.empty());
    }
    // DELETE /session → stop.
    {
        auto response = router.handle(makeRequest(HttpMethod::Delete, "/session"));
        assert(response.statusCode == 200);
        assert(service.lastCommand == "stop");
    }
    // Command failure maps to 409 with the message.
    {
        service.failNext = true;
        service.failMessage = "USB unavailable";
        auto response = router.handle(makeRequest(HttpMethod::Post, "/session",
                                                  "{\"transport\":\"usb\"}"));
        assert(response.statusCode == 409);
        assert(bodyContains(response, "USB unavailable"));
    }

    // PATCH /settings/audio applies only present fields.
    {
        auto response = router.handle(makeRequest(HttpMethod::Patch, "/settings/audio",
                                                  "{\"audioBufferMs\":25,\"audioMode\":\"safe\"}"));
        assert(response.statusCode == 200);
        assert(service.lastAudio.audioBufferMs.has_value() && *service.lastAudio.audioBufferMs == 25);
        assert(service.lastAudio.audioMode.has_value() && *service.lastAudio.audioMode == "safe");
        assert(!service.lastAudio.audioDevice.has_value());
    }
    // Wrong type for a field → 400.
    {
        auto response = router.handle(makeRequest(HttpMethod::Patch, "/settings/audio",
                                                  "{\"audioBufferMs\":\"loud\"}"));
        assert(response.statusCode == 400);
    }
    // Empty patch → 400.
    {
        auto response = router.handle(makeRequest(HttpMethod::Patch, "/settings/audio", "{}"));
        assert(response.statusCode == 400);
    }
    // PATCH /settings/video.
    {
        auto response = router.handle(makeRequest(HttpMethod::Patch, "/settings/video",
                                                  "{\"gamingMode\":false}"));
        assert(response.statusCode == 200);
        assert(service.lastVideo.gamingMode.has_value() && *service.lastVideo.gamingMode == false);
    }
    // PATCH /window with valid action.
    {
        auto response = router.handle(makeRequest(HttpMethod::Patch, "/window",
                                                  "{\"action\":\"toggleFullscreen\"}"));
        assert(response.statusCode == 200);
        assert(service.lastWindow.action.has_value() && *service.lastWindow.action == "toggleFullscreen");
    }
    // PATCH /window with unknown action → 400.
    {
        auto response = router.handle(makeRequest(HttpMethod::Patch, "/window",
                                                  "{\"action\":\"selfdestruct\"}"));
        assert(response.statusCode == 400);
    }
}

void testRouterValidation() {
    FakeService service;
    ApiRouter router(service);

    // Non-JSON content type on a command body → 415.
    {
        auto response = router.handle(makeRequest(HttpMethod::Post, "/session",
                                                  "transport=usb", "text/plain"));
        assert(response.statusCode == 415);
    }
    // Missing body on a POST command → 400.
    {
        HttpRequest request = makeRequest(HttpMethod::Post, "/session");
        request.contentType = "application/json";
        auto response = router.handle(request);
        assert(response.statusCode == 400);
    }
    // Invalid JSON → 400.
    {
        auto response = router.handle(makeRequest(HttpMethod::Post, "/session", "{bad"));
        assert(response.statusCode == 400);
    }
    // JSON array where object expected → 400.
    {
        auto response = router.handle(makeRequest(HttpMethod::Post, "/session", "[1,2]"));
        assert(response.statusCode == 400);
    }
    // Unknown HTTP method token → 501.
    {
        HttpRequest request = makeRequest(HttpMethod::Unknown, "/status");
        auto response = router.handle(request);
        assert(response.statusCode == 501);
    }
}

} // namespace

int main() {
    testHttpParser();
    testJson();
    testRouterReads();
    testRouterCommands();
    testRouterValidation();
    return 0;
}
