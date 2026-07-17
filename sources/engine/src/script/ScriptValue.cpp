#include "engine/script/ScriptValue.hpp"

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

} // namespace kb::script
