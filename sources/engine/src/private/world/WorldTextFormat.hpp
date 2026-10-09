#pragma once

#include "engine/core/JsonValue.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace kb::world::text {

// Deterministic, line-oriented JSON emission for the world's text files: the
// same document always produces the same bytes, one array entry per line, so
// version control diffs stay small and readable.
inline void AppendQuoted(std::string& out, std::string_view value) {
    out += '"';
    for (const char character : value) {
        switch (character) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(character) < 0x20U) {
                constexpr std::string_view kHex = "0123456789abcdef";
                out += "\\u00";
                out += kHex[static_cast<unsigned char>(character) >> 4U];
                out += kHex[static_cast<unsigned char>(character) & 0x0FU];
            } else {
                out += character;
            }
            break;
        }
    }
    out += '"';
}

[[nodiscard]] inline std::string Number(double value) {
    char buffer[40];
    if (std::isfinite(value) && value == std::floor(value) && std::fabs(value) < 9.007199254740992e15) {
        std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.17g", std::isfinite(value) ? value : 0.0);
    }
    return buffer;
}

[[nodiscard]] inline std::string Integer(std::int64_t value) {
    return std::to_string(value);
}

[[nodiscard]] inline const std::string* String(const kb::core::JsonValue& object, std::string_view key) {
    const kb::core::JsonValue* value = object.Find(key);
    return value != nullptr && value->GetKind() == kb::core::JsonValue::Kind::String ? &value->AsString() : nullptr;
}

[[nodiscard]] inline std::optional<double> Double(const kb::core::JsonValue& object, std::string_view key) {
    const kb::core::JsonValue* value = object.Find(key);
    if (value == nullptr || value->GetKind() != kb::core::JsonValue::Kind::Number || !std::isfinite(value->AsNumber())) {
        return std::nullopt;
    }
    return value->AsNumber();
}

[[nodiscard]] inline std::optional<bool> Bool(const kb::core::JsonValue& object, std::string_view key) {
    const kb::core::JsonValue* value = object.Find(key);
    if (value == nullptr || value->GetKind() != kb::core::JsonValue::Kind::Bool) {
        return std::nullopt;
    }
    return value->AsBool();
}

// An integer stored exactly in a JSON number, within [minimum, maximum].
[[nodiscard]] inline std::optional<std::int64_t> Int(const kb::core::JsonValue& object, std::string_view key, std::int64_t minimum, std::int64_t maximum) {
    const std::optional<double> value = Double(object, key);
    if (!value.has_value() || *value != std::floor(*value) || *value < static_cast<double>(minimum) || *value > static_cast<double>(maximum)) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(*value);
}

[[nodiscard]] bool ReadTextFile(const std::filesystem::path& path, std::string& text, std::string& error);
[[nodiscard]] bool WriteTextFileAtomically(const std::filesystem::path& path, std::string_view text, std::string& error);

} // namespace kb::world::text
