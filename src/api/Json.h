#pragma once

// Minimal, bounded JSON reader/writer for the loopback API.
//
// The standard library gives us no JSON, and padmirror_core must stay free of
// Qt, so this is a small hand-rolled parser. It is deliberately strict and
// bounded: it enforces a maximum nesting depth and a maximum node count so a
// hostile-but-local client cannot drive unbounded recursion or allocation. It
// supports exactly the subset the API needs (objects, arrays, strings, numbers,
// booleans, null) and rejects everything else.

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace padmirror::api::json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() = default;
    explicit Value(bool b) : type_(Type::Bool), bool_(b) {}
    explicit Value(double n) : type_(Type::Number), number_(n) {}
    explicit Value(std::string s) : type_(Type::String), string_(std::move(s)) {}

    static Value makeArray() { Value v; v.type_ = Type::Array; return v; }
    static Value makeObject() { Value v; v.type_ = Type::Object; return v; }

    [[nodiscard]] Type type() const { return type_; }
    [[nodiscard]] bool isObject() const { return type_ == Type::Object; }
    [[nodiscard]] bool isArray() const { return type_ == Type::Array; }
    [[nodiscard]] bool isString() const { return type_ == Type::String; }
    [[nodiscard]] bool isNumber() const { return type_ == Type::Number; }
    [[nodiscard]] bool isBool() const { return type_ == Type::Bool; }

    [[nodiscard]] bool asBool() const { return bool_; }
    [[nodiscard]] double asNumber() const { return number_; }
    [[nodiscard]] const std::string& asString() const { return string_; }
    [[nodiscard]] const std::vector<Value>& items() const { return array_; }
    [[nodiscard]] const std::map<std::string, Value>& members() const { return object_; }

    // Object access. Returns nullptr when absent or when this is not an object.
    [[nodiscard]] const Value* find(const std::string& key) const {
        if (type_ != Type::Object) return nullptr;
        auto it = object_.find(key);
        return it == object_.end() ? nullptr : &it->second;
    }

    void set(const std::string& key, Value value) { object_.emplace(key, std::move(value)); }
    void push(Value value) { array_.push_back(std::move(value)); }

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<Value> array_;
    std::map<std::string, Value> object_;
};

struct ParseResult {
    bool ok = false;
    Value value;
    std::string error;
};

// Parses `text` with hard depth and node limits. Any violation yields ok=false.
ParseResult parse(const std::string& text, std::size_t maxDepth, std::size_t maxNodes);

// ---- Writer -----------------------------------------------------------------

class Writer {
public:
    void beginObject() { pending(); out_ += '{'; stack_.push_back(false); }
    void endObject() { out_ += '}'; stack_.pop_back(); }
    void beginArray() { pending(); out_ += '['; stack_.push_back(false); }
    void endArray() { out_ += ']'; stack_.pop_back(); }

    void key(const std::string& k) {
        separator();
        writeString(k);
        out_ += ':';
        needValue_ = true;
    }

    void value(const std::string& s) { pending(); writeString(s); }
    void value(const char* s) { pending(); writeString(s); }
    void value(bool b) { pending(); out_ += b ? "true" : "false"; }
    void value(double n) { pending(); writeNumber(n); }
    void value(long long n) { pending(); out_ += std::to_string(n); }
    void value(unsigned long long n) { pending(); out_ += std::to_string(n); }
    void value(int n) { pending(); out_ += std::to_string(n); }
    void nullValue() { pending(); out_ += "null"; }

    void member(const std::string& k, const std::string& v) { key(k); value(v); }
    void member(const std::string& k, const char* v) { key(k); value(v); }
    void member(const std::string& k, bool v) { key(k); value(v); }
    void member(const std::string& k, double v) { key(k); value(v); }
    void member(const std::string& k, int v) { key(k); value(v); }
    void member(const std::string& k, unsigned long long v) { key(k); value(v); }

    [[nodiscard]] std::string str() const { return out_; }

private:
    void separator() {
        if (!stack_.empty() && stack_.back()) out_ += ',';
        if (!stack_.empty()) stack_.back() = true;
    }
    // For array elements / top-level values not preceded by an explicit key.
    void pending() {
        if (needValue_) { needValue_ = false; return; }
        separator();
    }
    void writeString(const std::string& s);
    void writeNumber(double n);

    std::string out_;
    std::vector<bool> stack_;
    bool needValue_ = false;
};

} // namespace padmirror::api::json
