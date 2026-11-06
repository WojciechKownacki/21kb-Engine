#include "engine/script/ScriptValue.hpp"

#include <charconv>
#include <cerrno>
#include <cstdlib>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

namespace kb::script {
namespace {

const std::string kEmptyString;

} // namespace

ScriptValue::ScriptValue(bool value)
    : value_(value)
    , type_(ScriptValueType::Bool) {}

ScriptValue::ScriptValue(int value)
    : value_(value)
    , type_(ScriptValueType::Int) {}

ScriptValue::ScriptValue(float value)
    : value_(value)
    , type_(ScriptValueType::Float) {}

ScriptValue::ScriptValue(double value)
    : value_(value)
    , type_(ScriptValueType::Double) {}

ScriptValue::ScriptValue(std::int64_t value)
    : value_(value)
    , type_(ScriptValueType::Int64) {}

ScriptValue::ScriptValue(std::uint32_t value)
    : value_(static_cast<std::uint64_t>(value))
    , type_(ScriptValueType::UInt32) {}

ScriptValue::ScriptValue(std::string value)
    : value_(std::move(value))
    , type_(ScriptValueType::String) {}

ScriptValue::ScriptValue(std::string value, ScriptValueType type)
    : value_(std::move(value))
    , type_(type == ScriptValueType::Name || type == ScriptValueType::Guid ? type : ScriptValueType::String) {}

ScriptValue::ScriptValue(std::uint64_t value, ScriptValueType type)
    : value_(value)
    , type_(
          type == ScriptValueType::Entity || type == ScriptValueType::Component || type == ScriptValueType::Hash
              ? type
              : ScriptValueType::Void) {
    if (type_ == ScriptValueType::Void) {
        value_ = std::monostate{};
    }
}

ScriptValueType ScriptValue::Type() const noexcept {
    return type_;
}

bool ScriptValue::AsBool(bool fallback) const noexcept {
    const auto* value = std::get_if<bool>(&value_);
    return value == nullptr ? fallback : *value;
}

int ScriptValue::AsInt(int fallback) const noexcept {
    const auto* value = std::get_if<int>(&value_);
    return value == nullptr ? fallback : *value;
}

float ScriptValue::AsFloat(float fallback) const noexcept {
    const auto* value = std::get_if<float>(&value_);
    return value == nullptr ? fallback : *value;
}

double ScriptValue::AsDouble(double fallback) const noexcept {
    const auto* value = std::get_if<double>(&value_);
    return value == nullptr ? fallback : *value;
}

std::int64_t ScriptValue::AsInt64(std::int64_t fallback) const noexcept {
    const auto* value = std::get_if<std::int64_t>(&value_);
    return value == nullptr ? fallback : *value;
}

std::uint32_t ScriptValue::AsUInt32(std::uint32_t fallback) const noexcept {
    const auto* value = std::get_if<std::uint64_t>(&value_);
    return value == nullptr ? fallback : static_cast<std::uint32_t>(*value);
}

const std::string& ScriptValue::AsString() const noexcept {
    const auto* value = std::get_if<std::string>(&value_);
    return value == nullptr ? kEmptyString : *value;
}

std::uint64_t ScriptValue::AsUInt64(std::uint64_t fallback) const noexcept {
    const auto* value = std::get_if<std::uint64_t>(&value_);
    return value == nullptr ? fallback : *value;
}

kb::visual::VisualGraphRuntimeValue ScriptValue::ToVisualGraphValue() const {
    switch (type_) {
    case ScriptValueType::Bool:
        return kb::visual::VisualGraphRuntimeValue{AsBool()};
    case ScriptValueType::Int:
        return kb::visual::VisualGraphRuntimeValue{AsInt()};
    case ScriptValueType::Float:
        return kb::visual::VisualGraphRuntimeValue{AsFloat()};
    case ScriptValueType::String:
        return kb::visual::VisualGraphRuntimeValue{AsString()};
    case ScriptValueType::Entity:
    case ScriptValueType::Component:
    case ScriptValueType::UInt32:
    case ScriptValueType::Hash:
        return kb::visual::VisualGraphRuntimeValue{AsUInt64(), ToVisualGraphValueType(type_)};
    case ScriptValueType::Int64:
        return kb::visual::VisualGraphRuntimeValue{AsInt64()};
    case ScriptValueType::Double:
        return kb::visual::VisualGraphRuntimeValue{AsDouble()};
    case ScriptValueType::Name:
    case ScriptValueType::Guid:
        return kb::visual::VisualGraphRuntimeValue{AsString(), ToVisualGraphValueType(type_)};
    case ScriptValueType::Void:
        break;
    }
    return kb::visual::VisualGraphRuntimeValue{};
}

const char* ToString(ScriptValueType type) noexcept {
    switch (type) {
    case ScriptValueType::Void:
        return "Void";
    case ScriptValueType::Bool:
        return "Bool";
    case ScriptValueType::Int:
        return "Int";
    case ScriptValueType::Float:
        return "Float";
    case ScriptValueType::String:
        return "String";
    case ScriptValueType::Entity:
        return "Entity";
    case ScriptValueType::Component:
        return "Component";
    case ScriptValueType::Int64:
        return "Int64";
    case ScriptValueType::UInt32:
        return "UInt32";
    case ScriptValueType::Double:
        return "Double";
    case ScriptValueType::Name:
        return "Name";
    case ScriptValueType::Guid:
        return "Guid";
    case ScriptValueType::Hash:
        return "Hash";
    }
    return "Void";
}

bool TryParse(std::string_view text, ScriptValueType& out) noexcept {
    // The full enumerator set, contiguous from Void=0. Iterating and
    // comparing against ToString(candidate) keeps this the exact inverse of
    // ToString with no independent string table to drift — adding a new
    // ScriptValueType and its ToString case automatically makes it parseable
    // here too.
    constexpr ScriptValueType kAllTypes[]{
        ScriptValueType::Void, ScriptValueType::Bool, ScriptValueType::Int, ScriptValueType::Float,
        ScriptValueType::String, ScriptValueType::Entity, ScriptValueType::Component,
        ScriptValueType::Int64, ScriptValueType::UInt32, ScriptValueType::Double,
        ScriptValueType::Name, ScriptValueType::Guid, ScriptValueType::Hash,
    };
    for (const ScriptValueType candidate : kAllTypes) {
        if (text == ToString(candidate)) {
            out = candidate;
            return true;
        }
    }
    return false;
}

kb::visual::VisualGraphValueType ToVisualGraphValueType(ScriptValueType type) noexcept {
    switch (type) {
    case ScriptValueType::Bool:
        return kb::visual::VisualGraphValueType::Bool;
    case ScriptValueType::Int:
        return kb::visual::VisualGraphValueType::Int;
    case ScriptValueType::Float:
        return kb::visual::VisualGraphValueType::Float;
    case ScriptValueType::String:
        return kb::visual::VisualGraphValueType::String;
    case ScriptValueType::Entity:
        return kb::visual::VisualGraphValueType::Entity;
    case ScriptValueType::Component:
        return kb::visual::VisualGraphValueType::Component;
    case ScriptValueType::Int64:
        return kb::visual::VisualGraphValueType::Int64;
    case ScriptValueType::UInt32:
        return kb::visual::VisualGraphValueType::UInt32;
    case ScriptValueType::Double:
        return kb::visual::VisualGraphValueType::Double;
    case ScriptValueType::Name:
        return kb::visual::VisualGraphValueType::Name;
    case ScriptValueType::Guid:
        return kb::visual::VisualGraphValueType::Guid;
    case ScriptValueType::Hash:
        return kb::visual::VisualGraphValueType::Hash;
    case ScriptValueType::Void:
        break;
    }
    return kb::visual::VisualGraphValueType::Void;
}

namespace {

template <typename Number>
[[nodiscard]] std::string NumberText(Number value) {
    char buffer[40];
    const std::to_chars_result written = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return std::string{ buffer, written.ptr };
}

template <typename Integer>
[[nodiscard]] bool ParseWhole(std::string_view text, Integer& output) noexcept {
    const std::from_chars_result parsed = std::from_chars(text.data(), text.data() + text.size(), output);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

template <typename Real>
[[nodiscard]] bool ParseReal(std::string_view text, Real& output) {
    const std::string copy{ text };
    char* end = nullptr;
    errno = 0;
    if constexpr (std::is_same_v<Real, float>) {
        output = std::strtof(copy.c_str(), &end);
    } else {
        output = std::strtod(copy.c_str(), &end);
    }
    return errno == 0 && end == copy.c_str() + copy.size();
}

} // namespace

std::string FormatValueText(const ScriptValue& value) {
    switch (value.Type()) {
    case ScriptValueType::Bool:
        return value.AsBool() ? "true" : "false";
    case ScriptValueType::Int:
        return NumberText(value.AsInt());
    case ScriptValueType::Float:
        return NumberText(value.AsFloat());
    case ScriptValueType::Int64:
        return NumberText(value.AsInt64());
    case ScriptValueType::Double:
        return NumberText(value.AsDouble());
    case ScriptValueType::UInt32:
        return NumberText(value.AsUInt32());
    case ScriptValueType::Entity:
    case ScriptValueType::Component:
    case ScriptValueType::Hash:
        return NumberText(value.AsUInt64());
    case ScriptValueType::String:
    case ScriptValueType::Name:
    case ScriptValueType::Guid:
        return value.AsString();
    case ScriptValueType::Void:
        break;
    }
    return {};
}

std::optional<ScriptValue> TryParseValueText(std::string_view text, ScriptValueType type) {
    switch (type) {
    case ScriptValueType::String:
        return ScriptValue{ std::string{ text } };
    case ScriptValueType::Name:
    case ScriptValueType::Guid:
        return ScriptValue{ std::string{ text }, type };
    case ScriptValueType::Void:
        return std::nullopt;
    default:
        break;
    }
    if (text.empty()) {
        return std::nullopt;
    }
    switch (type) {
    case ScriptValueType::Bool:
        if (text == "true" || text == "1") {
            return ScriptValue{ true };
        }
        if (text == "false" || text == "0") {
            return ScriptValue{ false };
        }
        return std::nullopt;
    case ScriptValueType::Int: {
        int parsed = 0;
        return ParseWhole(text, parsed) ? std::optional<ScriptValue>{ ScriptValue{ parsed } } : std::nullopt;
    }
    case ScriptValueType::Float: {
        float parsed = 0.0F;
        return ParseReal(text, parsed) ? std::optional<ScriptValue>{ ScriptValue{ parsed } } : std::nullopt;
    }
    case ScriptValueType::Double: {
        double parsed = 0.0;
        return ParseReal(text, parsed) ? std::optional<ScriptValue>{ ScriptValue{ parsed } } : std::nullopt;
    }
    case ScriptValueType::Int64: {
        std::int64_t parsed = 0;
        return ParseWhole(text, parsed) ? std::optional<ScriptValue>{ ScriptValue{ parsed } } : std::nullopt;
    }
    case ScriptValueType::UInt32: {
        std::uint32_t parsed = 0U;
        return ParseWhole(text, parsed) ? std::optional<ScriptValue>{ ScriptValue{ parsed } } : std::nullopt;
    }
    case ScriptValueType::Entity:
    case ScriptValueType::Component:
    case ScriptValueType::Hash: {
        std::uint64_t parsed = 0U;
        return ParseWhole(text, parsed) ? std::optional<ScriptValue>{ ScriptValue{ parsed, type } } : std::nullopt;
    }
    case ScriptValueType::String:
    case ScriptValueType::Name:
    case ScriptValueType::Guid:
    case ScriptValueType::Void:
        break;
    }
    return std::nullopt;
}

std::string FormatTypedValueText(const ScriptValue& value) {
    return std::string{ ToString(value.Type()) } + ':' + FormatValueText(value);
}

std::optional<ScriptValue> TryParseTypedValueText(std::string_view text) {
    const std::size_t separator = text.find(':');
    ScriptValueType type = ScriptValueType::Void;
    if (separator == std::string_view::npos || !TryParse(text.substr(0U, separator), type)) {
        return std::nullopt;
    }
    return TryParseValueText(text.substr(separator + 1U), type);
}

} // namespace kb::script
