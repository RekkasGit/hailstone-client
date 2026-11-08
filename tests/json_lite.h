#pragma once

// A small JSON reader for the test vectors (rof2decomp re/test/vectors/*.jsonl):
// objects, arrays, strings, numbers (kept as text, so integers up to 64 bits
// and %.17g doubles read back exactly), true/false/null. Non-finite floats
// arrive as the strings "inf", "-inf", "nan" and "nan:0x<bits>".

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace hailstone::test {

struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
    bool b = false;
    std::string text; // number text or string
    std::vector<Json> items;
    std::map<std::string, Json> fields;

    bool is_null() const { return type == Type::Null; }
    bool has(const std::string& k) const { return type == Type::Object && fields.count(k) != 0; }
    const Json& operator[](const std::string& k) const {
        static const Json null_value;
        if (type != Type::Object) return null_value;
        auto it = fields.find(k);
        return it == fields.end() ? null_value : it->second;
    }
    const Json& operator[](std::size_t i) const {
        static const Json null_value;
        return i < items.size() ? items[i] : null_value;
    }
    std::int64_t i64() const {
        if (type == Type::Bool) return b ? 1 : 0;
        if (type != Type::Number) return 0;
        if (text.find_first_of(".eE") != std::string::npos) return static_cast<std::int64_t>(std::strtod(text.c_str(), nullptr));
        return std::strtoll(text.c_str(), nullptr, 10);
    }
    std::int32_t i32() const { return static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(i64()))); }
    std::uint32_t u32() const { return static_cast<std::uint32_t>(static_cast<std::uint64_t>(i64())); }
    bool truthy() const { return type == Type::Bool ? b : (type == Type::Number ? i64() != 0 : false); }
    double f64() const {
        if (type == Type::Number) return std::strtod(text.c_str(), nullptr);
        if (type == Type::String) {
            if (text == "inf") return std::numeric_limits<double>::infinity();
            if (text == "-inf") return -std::numeric_limits<double>::infinity();
            if (text.rfind("nan:0x", 0) == 0) {
                auto bits = static_cast<std::uint32_t>(std::strtoul(text.c_str() + 6, nullptr, 16));
                float f;
                std::memcpy(&f, &bits, 4);
                return static_cast<double>(f);
            }
            if (text == "nan") return std::numeric_limits<double>::quiet_NaN();
        }
        return 0.0;
    }
    float f32() const {
        if (type == Type::String && text.rfind("nan:0x", 0) == 0) {
            auto bits = static_cast<std::uint32_t>(std::strtoul(text.c_str() + 6, nullptr, 16));
            float f;
            std::memcpy(&f, &bits, 4);
            return f;
        }
        return static_cast<float>(f64());
    }
};

class JsonParser {
public:
    explicit JsonParser(std::string_view s) : s_(s) {}
    Json parse() {
        Json v = value();
        ws();
        return v;
    }

private:
    void ws() {
        while (p_ < s_.size() && (s_[p_] == ' ' || s_[p_] == '\t' || s_[p_] == '\n' || s_[p_] == '\r')) ++p_;
    }
    char peek() {
        ws();
        if (p_ >= s_.size()) throw std::runtime_error("json: end");
        return s_[p_];
    }
    void expect(char c) {
        if (peek() != c) throw std::runtime_error(std::string("json: expected ") + c);
        ++p_;
    }
    std::string str() {
        expect('"');
        std::string out;
        while (p_ < s_.size() && s_[p_] != '"') {
            char c = s_[p_++];
            if (c == '\\' && p_ < s_.size()) {
                char e = s_[p_++];
                switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    // \u00XX: the vectors escape the bytes below 0x20 this way; larger codes become UTF-8.
                    unsigned code = static_cast<unsigned>(std::strtoul(std::string(s_.substr(p_, 4)).c_str(), nullptr, 16));
                    p_ += 4;
                    if (code < 0x80) {
                        out += static_cast<char>(code);
                    } else if (code < 0x800) {
                        out += static_cast<char>(0xc0 | (code >> 6));
                        out += static_cast<char>(0x80 | (code & 0x3f));
                    } else {
                        out += static_cast<char>(0xe0 | (code >> 12));
                        out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
                        out += static_cast<char>(0x80 | (code & 0x3f));
                    }
                    break;
                }
                default: out += e; break;
                }
            } else {
                out += c;
            }
        }
        ++p_;
        return out;
    }
    Json value() {
        Json v;
        char c = peek();
        if (c == '{') {
            ++p_;
            v.type = Json::Type::Object;
            if (peek() == '}') {
                ++p_;
                return v;
            }
            for (;;) {
                std::string k = str();
                expect(':');
                v.fields[k] = value();
                if (peek() == ',') {
                    ++p_;
                    continue;
                }
                expect('}');
                return v;
            }
        }
        if (c == '[') {
            ++p_;
            v.type = Json::Type::Array;
            if (peek() == ']') {
                ++p_;
                return v;
            }
            for (;;) {
                v.items.push_back(value());
                if (peek() == ',') {
                    ++p_;
                    continue;
                }
                expect(']');
                return v;
            }
        }
        if (c == '"') {
            v.type = Json::Type::String;
            v.text = str();
            return v;
        }
        if (s_.compare(p_, 4, "true") == 0) {
            p_ += 4;
            v.type = Json::Type::Bool;
            v.b = true;
            return v;
        }
        if (s_.compare(p_, 5, "false") == 0) {
            p_ += 5;
            v.type = Json::Type::Bool;
            return v;
        }
        if (s_.compare(p_, 4, "null") == 0) {
            p_ += 4;
            return v;
        }
        std::size_t b = p_;
        while (p_ < s_.size() && std::strchr("+-0123456789.eE", s_[p_])) ++p_;
        if (b == p_) throw std::runtime_error("json: bad value");
        v.type = Json::Type::Number;
        v.text = std::string(s_.substr(b, p_ - b));
        return v;
    }
    std::string_view s_;
    std::size_t p_ = 0;
};

inline Json parse_json(std::string_view s) { return JsonParser(s).parse(); }

} // namespace hailstone::test
