#include "api/Json.h"

#include <cmath>
#include <cstdio>

namespace padmirror::api::json {

namespace {

// Bounded recursive-descent parser. `depth` and `nodes` are checked against the
// caller-supplied limits before any recursion or allocation, so a deeply nested
// or huge document is rejected rather than parsed.
class Parser {
public:
    Parser(const std::string& text, std::size_t maxDepth, std::size_t maxNodes)
        : text_(text), maxDepth_(maxDepth), maxNodes_(maxNodes) {}

    ParseResult run() {
        skipWs();
        Value value;
        if (!parseValue(value, 0)) {
            return {false, {}, error_.empty() ? "invalid json" : error_};
        }
        skipWs();
        if (pos_ != text_.size()) {
            return {false, {}, "trailing content"};
        }
        return {true, std::move(value), {}};
    }

private:
    bool fail(const char* message) {
        if (error_.empty()) error_ = message;
        return false;
    }

    bool countNode() {
        if (++nodes_ > maxNodes_) return fail("too many nodes");
        return true;
    }

    void skipWs() {
        while (pos_ < text_.size()) {
            char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos_;
            else break;
        }
    }

    [[nodiscard]] char peek() const { return pos_ < text_.size() ? text_[pos_] : '\0'; }

    bool parseValue(Value& out, std::size_t depth) {
        if (depth > maxDepth_) return fail("max depth exceeded");
        if (!countNode()) return false;
        skipWs();
        if (pos_ >= text_.size()) return fail("unexpected end");
        char c = text_[pos_];
        switch (c) {
            case '{': return parseObject(out, depth);
            case '[': return parseArray(out, depth);
            case '"': {
                std::string s;
                if (!parseString(s)) return false;
                out = Value(std::move(s));
                return true;
            }
            case 't': case 'f': return parseBool(out);
            case 'n': return parseNull(out);
            default:
                if (c == '-' || (c >= '0' && c <= '9')) return parseNumber(out);
                return fail("unexpected token");
        }
    }

    bool parseObject(Value& out, std::size_t depth) {
        out = Value::makeObject();
        ++pos_; // consume '{'
        skipWs();
        if (peek() == '}') { ++pos_; return true; }
        while (true) {
            skipWs();
            if (peek() != '"') return fail("expected key");
            std::string key;
            if (!parseString(key)) return false;
            skipWs();
            if (peek() != ':') return fail("expected ':'");
            ++pos_;
            Value value;
            if (!parseValue(value, depth + 1)) return false;
            out.set(key, std::move(value));
            skipWs();
            char c = peek();
            if (c == ',') { ++pos_; continue; }
            if (c == '}') { ++pos_; return true; }
            return fail("expected ',' or '}'");
        }
    }

    bool parseArray(Value& out, std::size_t depth) {
        out = Value::makeArray();
        ++pos_; // consume '['
        skipWs();
        if (peek() == ']') { ++pos_; return true; }
        while (true) {
            Value value;
            if (!parseValue(value, depth + 1)) return false;
            out.push(std::move(value));
            skipWs();
            char c = peek();
            if (c == ',') { ++pos_; continue; }
            if (c == ']') { ++pos_; return true; }
            return fail("expected ',' or ']'");
        }
    }

    bool parseString(std::string& out) {
        if (peek() != '"') return fail("expected string");
        ++pos_;
        out.clear();
        while (pos_ < text_.size()) {
            char c = text_[pos_++];
            if (c == '"') return true;
            if (c == '\\') {
                if (pos_ >= text_.size()) return fail("bad escape");
                char e = text_[pos_++];
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u': {
                        if (pos_ + 4 > text_.size()) return fail("bad \\u escape");
                        unsigned code = 0;
                        for (int i = 0; i < 4; ++i) {
                            char h = text_[pos_++];
                            code <<= 4;
                            if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
                            else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
                            else return fail("bad hex");
                        }
                        appendUtf8(code, out);
                        break;
                    }
                    default: return fail("bad escape");
                }
            } else if (static_cast<unsigned char>(c) < 0x20) {
                return fail("control char in string");
            } else {
                out += c;
            }
        }
        return fail("unterminated string");
    }

    static void appendUtf8(unsigned code, std::string& out) {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    bool parseNumber(Value& out) {
        std::size_t start = pos_;
        if (peek() == '-') ++pos_;
        while (pos_ < text_.size()) {
            char c = text_[pos_];
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' ||
                c == '+' || c == '-') {
                ++pos_;
            } else {
                break;
            }
        }
        const std::string token = text_.substr(start, pos_ - start);
        if (token.empty()) return fail("bad number");
        try {
            std::size_t consumed = 0;
            double value = std::stod(token, &consumed);
            if (consumed != token.size() || !std::isfinite(value)) return fail("bad number");
            out = Value(value);
            return true;
        } catch (...) {
            return fail("bad number");
        }
    }

    bool parseBool(Value& out) {
        if (text_.compare(pos_, 4, "true") == 0) { pos_ += 4; out = Value(true); return true; }
        if (text_.compare(pos_, 5, "false") == 0) { pos_ += 5; out = Value(false); return true; }
        return fail("bad literal");
    }

    bool parseNull(Value& out) {
        if (text_.compare(pos_, 4, "null") == 0) { pos_ += 4; out = Value(); return true; }
        return fail("bad literal");
    }

    const std::string& text_;
    std::size_t maxDepth_;
    std::size_t maxNodes_;
    std::size_t pos_ = 0;
    std::size_t nodes_ = 0;
    std::string error_;
};

} // namespace

ParseResult parse(const std::string& text, std::size_t maxDepth, std::size_t maxNodes) {
    Parser parser(text, maxDepth, maxNodes);
    return parser.run();
}

void Writer::writeString(const std::string& s) {
    out_ += '"';
    for (char c : s) {
        switch (c) {
            case '"': out_ += "\\\""; break;
            case '\\': out_ += "\\\\"; break;
            case '\b': out_ += "\\b"; break;
            case '\f': out_ += "\\f"; break;
            case '\n': out_ += "\\n"; break;
            case '\r': out_ += "\\r"; break;
            case '\t': out_ += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out_ += buf;
                } else {
                    out_ += c;
                }
        }
    }
    out_ += '"';
}

void Writer::writeNumber(double n) {
    if (!std::isfinite(n)) { out_ += "0"; return; }
    // Emit integers without a decimal point; otherwise use a compact form.
    if (n == std::floor(n) && std::fabs(n) < 1e15) {
        out_ += std::to_string(static_cast<long long>(n));
        return;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6g", n);
    out_ += buf;
}

} // namespace padmirror::api::json
