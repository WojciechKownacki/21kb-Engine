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

ScriptValue::ScriptValue(std::string value)
    : value_(std::move(value))
    , type_(ScriptValueType::String) {}

ScriptValue::ScriptValue(std::uint64_t value, ScriptValueType type)
    : value_(value)
    , type_(type == ScriptValueType::Entity || type == ScriptValueType::Component ? type : ScriptValueType::Void) {
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
        return kb::visual::VisualGraphRuntimeValue{AsUInt64(), ToVisualGraphValueType(type_)};
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
    }
    return "Void";
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
    case ScriptValueType::Void:
        break;
    }
    return kb::visual::VisualGraphValueType::Void;
}

} // namespace kb::script
