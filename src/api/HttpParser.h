#pragma once

// Bounded HTTP/1.1 request parser for the loopback API.
//
// The server accumulates bytes from a connection and feeds them here. The
// parser enforces every ApiLimits bound (request-line length, header count and
// size, declared body size) and refuses malformed input before the router ever
// sees it. It handles only what the local API needs: a single request with an
// optional Content-Length body. Chunked transfer-encoding is rejected.

#include "api/ApiContracts.h"

#include <string>

namespace padmirror::api {

enum class ParseStatus {
    NeedMore,   // Not enough bytes yet; keep reading (still within limits).
    Complete,   // A full request was parsed into `out`.
    Error,      // Malformed or over-limit; respond with `errorStatus` and close.
};

struct HttpParseOutcome {
    ParseStatus status = ParseStatus::NeedMore;
    int errorStatus = 0;         // HTTP status to send on Error (400/405/413/431/501).
    std::string errorMessage;    // Short machine-readable reason.
    std::size_t consumed = 0;    // Bytes consumed from the buffer on Complete.
};

// Attempts to parse one request from `buffer`. Pure and stateless: the caller
// owns the buffer and re-invokes as more bytes arrive.
HttpParseOutcome parseHttpRequest(const std::string& buffer, HttpRequest& out);

} // namespace padmirror::api
