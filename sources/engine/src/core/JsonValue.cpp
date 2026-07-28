#include "engine/core/JsonValue.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace kb::core {

namespace {

constexpr std::size_t kMaxParseDepth = 128U;

struct Parser {
    std::string_view text;
    std::size_t position = 0U;
    std::string* error = nullptr;

    [[nodiscard]] bool Fail(std::string message) {
        if (error != nullptr && error->empty()) {
            *error = std::move(message) + " (offset " + std::to_string(position) + ")";
        }
        return false;
    }

    void SkipWhitespace() noexcept {
        while (position < text.size()) {
            const char character = text[position];
            if (character != ' ' && character != '\t' && character != '\n' && character != '\r') {
                break;
            }
            ++position;
        }
    }

    [[nodiscard]] bool AtEnd() const noexcept {
        return position >= text.size();
    }

    [[nodiscard]] char Peek() const noexcept {
        return text[position];
    }

    [[nodiscard]] bool ConsumeLiteral(std::string_view literal) {
        if (text.substr(position, literal.size()) != literal) {
            return Fail("invalid literal");
        }
        position += literal.size();
        return true;
    }

    void AppendUtf8(std::string& out, std::uint32_t codepoint) {
        if (codepoint <= 0x7FU) {
            out += static_cast<char>(codepoint);
        } else if (codepoint <= 0x7FFU) {
            out += static_cast<char>(0xC0U | (codepoint >> 6U));
            out += static_cast<char>(0x80U | (codepoint & 0x3FU));
        } else if (codepoint <= 0xFFFFU) {
            out += static_cast<char>(0xE0U | (codepoint >> 12U));
            out += static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU));
            out += static_cast<char>(0x80U | (codepoint & 0x3FU));
        } else {
            out += static_cast<char>(0xF0U | (codepoint >> 18U));
            out += static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU));
            out += static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU));
            out += static_cast<char>(0x80U | (codepoint & 0x3FU));
        }
    }

    [[nodiscard]] bool ParseHex4(std::uint32_t& out) {
        if (position + 4U > text.size()) {
            return Fail("truncated unicode escape");
        }
        out = 0U;
        for (std::size_t index = 0U; index < 4U; ++index) {
            const char character = text[position + index];
            out <<= 4U;
            if (character >= '0' && character <= '9') {
                out |= static_cast<std::uint32_t>(character - '0');
            } else if (character >= 'a' && character <= 'f') {
                out |= static_cast<std::uint32_t>(character - 'a' + 10);
            } else if (character >= 'A' && character <= 'F') {
                out |= static_cast<std::uint32_t>(character - 'A' + 10);
            } else {
                return Fail("invalid unicode escape");
            }
        }
        position += 4U;
        return true;
    }

    [[nodiscard]] bool ParseString(std::string& out) {
        if (AtEnd() || Peek() != '"') {
            return Fail("expected string");
        }
        ++position;
        while (true) {
            if (AtEnd()) {
                return Fail("unterminated string");
            }
            const char character = text[position];
            if (character == '"') {
                ++position;
                return true;
            }
            if (static_cast<unsigned char>(character) < 0x20U) {
                return Fail("control character in string");
            }
            if (character != '\\') {
                out += character;
                ++position;
                continue;
            }
            ++position;
            if (AtEnd()) {
                return Fail("unterminated escape");
            }
            const char escape = text[position];
            ++position;
            switch (escape) {
            case '"':
                out += '"';
                break;
            case '\\':
                out += '\\';
                break;
            case '/':
                out += '/';
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u': {
                std::uint32_t codepoint = 0U;
                if (!ParseHex4(codepoint)) {
                    return false;
                }
                if (codepoint >= 0xD800U && codepoint <= 0xDBFFU) {
                    if (position + 2U > text.size() || text[position] != '\\' || text[position + 1U] != 'u') {
                        return Fail("missing low surrogate");
                    }
                    position += 2U;
                    std::uint32_t low = 0U;
                    if (!ParseHex4(low)) {
                        return false;
                    }
                    if (low < 0xDC00U || low > 0xDFFFU) {
                        return Fail("invalid low surrogate");
                    }
                    codepoint = 0x10000U + ((codepoint - 0xD800U) << 10U) + (low - 0xDC00U);
                } else if (codepoint >= 0xDC00U && codepoint <= 0xDFFFU) {
                    return Fail("unexpected low surrogate");
                }
                AppendUtf8(out, codepoint);
                break;
            }
            default:
                return Fail("invalid escape");
            }
        }
    }

    [[nodiscard]] bool ParseNumber(JsonValue& out) {
        const std::size_t start = position;
        if (!AtEnd() && Peek() == '-') {
            ++position;
        }
        while (!AtEnd()) {
            const char character = Peek();
            if ((character >= '0' && character <= '9') || character == '.' || character == 'e' || character == 'E'
                || character == '+' || character == '-') {
                ++position;
            } else {
                break;
            }
        }
        if (position == start) {
            return Fail("expected number");
        }
        const std::string token{ text.substr(start, position - start) };
        char* end = nullptr;
        const double value = std::strtod(token.c_str(), &end);
        if (end == nullptr || *end != '\0') {
            return Fail("invalid number");
        }
        out = JsonValue::MakeNumber(value);
        return true;
    }

    [[nodiscard]] bool ParseValue(JsonValue& out, std::size_t depth) {
        if (depth > kMaxParseDepth) {
            return Fail("document is nested too deeply");
        }
        SkipWhitespace();
        if (AtEnd()) {
            return Fail("unexpected end of document");
        }
        const char character = Peek();
        if (character == '{') {
            ++position;
            out = JsonValue::MakeObject();
            SkipWhitespace();
            if (!AtEnd() && Peek() == '}') {
                ++position;
                return true;
            }
            while (true) {
                SkipWhitespace();
                std::string key;
                if (!ParseString(key)) {
                    return false;
                }
                SkipWhitespace();
                if (AtEnd() || Peek() != ':') {
                    return Fail("expected ':' in object");
                }
                ++position;
                JsonValue member;
                if (!ParseValue(member, depth + 1U)) {
                    return false;
                }
                out.Set(std::move(key), std::move(member));
                SkipWhitespace();
                if (AtEnd()) {
                    return Fail("unterminated object");
                }
                if (Peek() == ',') {
                    ++position;
                    continue;
                }
                if (Peek() == '}') {
                    ++position;
                    return true;
                }
                return Fail("expected ',' or '}' in object");
            }
        }
        if (character == '[') {
            ++position;
            out = JsonValue::MakeArray();
            SkipWhitespace();
            if (!AtEnd() && Peek() == ']') {
                ++position;
                return true;
            }
            while (true) {
                JsonValue element;
                if (!ParseValue(element, depth + 1U)) {
                    return false;
                }
                out.Append(std::move(element));
                SkipWhitespace();
                if (AtEnd()) {
                    return Fail("unterminated array");
                }
                if (Peek() == ',') {
                    ++position;
                    continue;
                }
                if (Peek() == ']') {
                    ++position;
                    return true;
                }
                return Fail("expected ',' or ']' in array");
            }
        }
        if (character == '"') {
            std::string value;
            if (!ParseString(value)) {
                return false;
            }
            out = JsonValue::MakeString(std::move(value));
            return true;
        }
        if (character == 't') {
            if (!ConsumeLiteral("true")) {
                return false;
            }
            out = JsonValue::MakeBool(true);
            return true;
        }
        if (character == 'f') {
            if (!ConsumeLiteral("false")) {
                return false;
            }
            out = JsonValue::MakeBool(false);
            return true;
        }
        if (character == 'n') {
            if (!ConsumeLiteral("null")) {
                return false;
            }
            out = JsonValue::MakeNull();
            return true;
        }
        return ParseNumber(out);
    }
};

void AppendEscaped(std::string& out, std::string_view text) {
    for (const char character : text) {
        switch (character) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(character) < 0x20U) {
                constexpr std::string_view kHexDigits = "0123456789abcdef";
                out += "\\u00";
                out += kHexDigits[static_cast<std::size_t>(static_cast<unsigned char>(character) >> 4U)];
                out += kHexDigits[static_cast<std::size_t>(static_cast<unsigned char>(character) & 0x0FU)];
            } else {
                out += character;
            }
            break;
        }
    }
}

void DumpValue(const JsonValue& value, std::string& out) {
    switch (value.GetKind()) {
    case JsonValue::Kind::Null:
        out += "null";
        break;
    case JsonValue::Kind::Bool:
        out += value.AsBool() ? "true" : "false";
        break;
    case JsonValue::Kind::Number: {
        const double number = value.AsNumber();
        if (std::isfinite(number) && number == std::floor(number) && std::fabs(number) < 9.007199254740992e15) {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(number));
            out += buffer;
        } else if (std::isfinite(number)) {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%.17g", number);
            out += buffer;
        } else {
            out += "null";
        }
        break;
    }
    case JsonValue::Kind::String:
        out += '"';
        AppendEscaped(out, value.AsString());
        out += '"';
        break;
    case JsonValue::Kind::Array:
        out += '[';
        for (std::size_t index = 0U; index < value.Size(); ++index) {
            if (index != 0U) {
                out += ',';
            }
            DumpValue(*value.At(index), out);
        }
        out += ']';
        break;
    case JsonValue::Kind::Object:
        out += '{';
        for (std::size_t index = 0U; index < value.Size(); ++index) {
            if (index != 0U) {
                out += ',';
            }
            out += '"';
            AppendEscaped(out, value.MemberName(index));
            out += "\":";
            DumpValue(*value.At(index), out);
        }
        out += '}';
        break;
    }
}

const std::string kEmptyString{};

} // namespace

JsonValue JsonValue::MakeNull() {
    return JsonValue{};
}

JsonValue JsonValue::MakeBool(bool value) {
    JsonValue json;
    json.kind_ = Kind::Bool;
    json.bool_ = value;
    return json;
}

JsonValue JsonValue::MakeNumber(double value) {
    JsonValue json;
    json.kind_ = Kind::Number;
    json.number_ = value;
    return json;
}

JsonValue JsonValue::MakeString(std::string value) {
    JsonValue json;
    json.kind_ = Kind::String;
    json.string_ = std::move(value);
    return json;
}

JsonValue JsonValue::MakeArray() {
    JsonValue json;
    json.kind_ = Kind::Array;
    return json;
}

JsonValue JsonValue::MakeObject() {
    JsonValue json;
    json.kind_ = Kind::Object;
    return json;
}

JsonValue::Kind JsonValue::GetKind() const noexcept {
    return kind_;
}

bool JsonValue::IsNull() const noexcept {
    return kind_ == Kind::Null;
}

bool JsonValue::AsBool(bool fallback) const noexcept {
    return kind_ == Kind::Bool ? bool_ : fallback;
}

double JsonValue::AsNumber(double fallback) const noexcept {
    return kind_ == Kind::Number ? number_ : fallback;
}

const std::string& JsonValue::AsString() const noexcept {
    return kind_ == Kind::String ? string_ : kEmptyString;
}

std::size_t JsonValue::Size() const noexcept {
    if (kind_ == Kind::Array) {
        return array_.size();
    }
    if (kind_ == Kind::Object) {
        return memberValues_.size();
    }
    return 0U;
}

const JsonValue* JsonValue::At(std::size_t index) const noexcept {
    if (kind_ == Kind::Array) {
        return index < array_.size() ? &array_[index] : nullptr;
    }
    if (kind_ == Kind::Object) {
        return index < memberValues_.size() ? &memberValues_[index] : nullptr;
    }
    return nullptr;
}

void JsonValue::Append(JsonValue value) {
    if (kind_ != Kind::Array) {
        kind_ = Kind::Array;
        array_.clear();
    }
    array_.push_back(std::move(value));
}

const JsonValue* JsonValue::Find(std::string_view key) const noexcept {
    if (kind_ != Kind::Object) {
        return nullptr;
    }
    for (std::size_t index = 0U; index < memberNames_.size(); ++index) {
        if (memberNames_[index] == key) {
            return &memberValues_[index];
        }
    }
    return nullptr;
}

const std::string& JsonValue::MemberName(std::size_t index) const noexcept {
    if (kind_ != Kind::Object || index >= memberNames_.size()) {
        return kEmptyString;
    }
    return memberNames_[index];
}

void JsonValue::Set(std::string key, JsonValue value) {
    if (kind_ != Kind::Object) {
        kind_ = Kind::Object;
        memberNames_.clear();
        memberValues_.clear();
    }
    for (std::size_t index = 0U; index < memberNames_.size(); ++index) {
        if (memberNames_[index] == key) {
            memberValues_[index] = std::move(value);
            return;
        }
    }
    memberNames_.push_back(std::move(key));
    memberValues_.push_back(std::move(value));
}

std::string JsonValue::Dump() const {
    std::string out;
    out.reserve(256U);
    DumpValue(*this, out);
    return out;
}

bool JsonValue::Parse(std::string_view text, JsonValue& out, std::string& error) {
    error.clear();
    Parser parser{ .text = text, .position = 0U, .error = &error };
    if (!parser.ParseValue(out, 0U)) {
        return false;
    }
    parser.SkipWhitespace();
    if (!parser.AtEnd()) {
        return parser.Fail("trailing characters after document");
    }
    return true;
}

} // namespace kb::core
