#pragma once

// Route dispatch for the loopback API. Pure and Qt-free: it takes a parsed
// HttpRequest, validates method/content-type/body against the fixed bounds,
// invokes the IApiService, and renders a bounded JSON HttpResponse. The socket
// server (Qt) owns transport concerns (binding to loopback, connection limits,
// idle timeouts); this class owns everything that can be unit-tested.

#include "api/ApiContracts.h"

namespace padmirror::api {

class ApiRouter {
public:
    explicit ApiRouter(IApiService& service) : service_(service) {}

    // Dispatches a fully-parsed request to a route and returns the response.
    // Never throws; every failure path maps to a bounded JSON error body.
    HttpResponse handle(const HttpRequest& request);

private:
    HttpResponse handleStatus(const HttpRequest& request);
    HttpResponse handleDevices(const HttpRequest& request);
    HttpResponse handleSettings(const HttpRequest& request);
    HttpResponse handleSession(const HttpRequest& request);
    HttpResponse handleAudio(const HttpRequest& request);
    HttpResponse handleVideo(const HttpRequest& request);
    HttpResponse handleWindow(const HttpRequest& request);

    IApiService& service_;
};

} // namespace padmirror::api
