#include "api/HttpParser.h"

#include <algorithm>
#include <cctype>

namespace padmirror::api {

namespace {

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim(const std::string& s) {
    std::size_t begin = 0;
    std::size_t end = s.size();
    while (begin < end && (s[begin] == ' ' || s[begin] == '\t')) ++begin;
    while (end > begin && (s[end - 1] == ' ' || s[end - 1] == '\t')) --end;
    return s.substr(begin, end - begin);
}

HttpMethod methodFromToken(const std::string& token) {
    if (token == "GET") return HttpMethod::Get;
    if (token == "POST") return HttpMethod::Post;
    if (token == "PATCH") return HttpMethod::Patch;
    if (token == "DELETE") return HttpMethod::Delete;
    if (token == "PUT") return HttpMethod::Put;
    if (token == "HEAD") return HttpMethod::Head;
    if (token == "OPTIONS") return HttpMethod::Options;
    return HttpMethod::Unknown;
}

HttpParseOutcome error(int status, const char* message) {
    HttpParseOutcome outcome;
    outcome.status = ParseStatus::Error;
    outcome.errorStatus = status;
    outcome.errorMessage = message;
    return outcome;
}

} // namespace

HttpParseOutcome parseHttpRequest(const std::string& buffer, HttpRequest& out) {
    // Guard the header section size before we even find its end, so a client
    // that never sends the terminator cannot grow the buffer without bound.
    const std::size_t headerEnd = buffer.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        if (buffer.size() > ApiLimits::kMaxHeaderBytes) {
            return error(431, "header section too large");
        }
        return {}; // NeedMore
    }
    if (headerEnd > ApiLimits::kMaxHeaderBytes) {
        return error(431, "header section too large");
    }

    const std::string head = buffer.substr(0, headerEnd);

    // Split the head into lines on CRLF.
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= head.size()) {
        std::size_t nl = head.find("\r\n", start);
        if (nl == std::string::npos) {
            lines.push_back(head.substr(start));
            break;
        }
        lines.push_back(head.substr(start, nl - start));
        start = nl + 2;
    }
    if (lines.empty() || lines.front().empty()) {
        return error(400, "empty request line");
    }
    if (lines.front().size() > ApiLimits::kMaxRequestLineBytes) {
        return error(431, "request line too long");
    }

    // Parse "METHOD SP request-target SP HTTP/1.x".
    const std::string& requestLine = lines.front();
    const std::size_t sp1 = requestLine.find(' ');
    if (sp1 == std::string::npos) return error(400, "malformed request line");
    const std::size_t sp2 = requestLine.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) return error(400, "malformed request line");

    out = HttpRequest{};
    out.rawMethod = requestLine.substr(0, sp1);
    out.method = methodFromToken(out.rawMethod);
    const std::string version = requestLine.substr(sp2 + 1);
    if (version != "HTTP/1.1" && version != "HTTP/1.0") {
        return error(400, "unsupported http version");
    }
    out.keepAlive = (version == "HTTP/1.1");

    std::string target = requestLine.substr(sp1 + 1, sp2 - sp1 - 1);
    if (target.empty()) return error(400, "empty target");
    const std::size_t q = target.find('?');
    if (q == std::string::npos) {
        out.path = target;
    } else {
        out.path = target.substr(0, q);
        out.query = target.substr(q + 1);
    }

    // Headers.
    std::size_t contentLength = 0;
    bool hasContentLength = false;
    const std::size_t headerCount = lines.size() - 1;
    if (headerCount > ApiLimits::kMaxHeaderCount) {
        return error(431, "too many headers");
    }
    for (std::size_t i = 1; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        if (line.empty()) continue;
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) return error(400, "malformed header");
        const std::string name = toLower(trim(line.substr(0, colon)));
        const std::string value = trim(line.substr(colon + 1));
        if (name == "content-length") {
            hasContentLength = true;
            if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos) {
                return error(400, "invalid content-length");
            }
            try {
                contentLength = static_cast<std::size_t>(std::stoull(value));
            } catch (...) {
                return error(400, "invalid content-length");
            }
            if (contentLength > ApiLimits::kMaxBodyBytes) {
                return error(413, "body too large");
            }
        } else if (name == "content-type") {
            std::string ct = toLower(value);
            const std::size_t semi = ct.find(';');
            if (semi != std::string::npos) ct = trim(ct.substr(0, semi));
            out.contentType = ct;
        } else if (name == "transfer-encoding") {
            return error(501, "transfer-encoding unsupported");
        } else if (name == "connection") {
            const std::string v = toLower(value);
            if (v.find("close") != std::string::npos) out.keepAlive = false;
            else if (v.find("keep-alive") != std::string::npos) out.keepAlive = true;
        }
    }

    const std::size_t bodyStart = headerEnd + 4;
    if (!hasContentLength) contentLength = 0;
    if (buffer.size() - bodyStart < contentLength) {
        // Bound the wait: never let a partial body grow past the cap.
        if (contentLength > ApiLimits::kMaxBodyBytes) return error(413, "body too large");
        return {}; // NeedMore
    }
    out.body = buffer.substr(bodyStart, contentLength);

    HttpParseOutcome outcome;
    outcome.status = ParseStatus::Complete;
    outcome.consumed = bodyStart + contentLength;
    return outcome;
}

} // namespace padmirror::api
