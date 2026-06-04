#include "engine/visual/VisualGraphRuntimeValue.hpp"

#include <utility>

namespace kb::visual {
namespace {

const std::string kEmptyString;

} // namespace

VisualGraphRuntimeValue::VisualGraphRuntimeValue(bool value)
    : value_(value)
    , type_(VisualGraphValueType::Bool) {}

VisualGraphRuntimeValue::VisualGraphRuntimeValue(int value)
    : value_(value)
    , type_(VisualGraphValueType::Int) {}

VisualGraphRuntimeValue::VisualGraphRuntimeValue(float value)
    : value_(value)
    , type_(VisualGraphValueType::Float) {}

VisualGraphRuntimeValue::VisualGraphRuntimeValue(std::string value)
    : value_(std::move(value))
    , type_(VisualGraphValueType::String) {}

VisualGraphRuntimeValue::VisualGraphRuntimeValue(std::uint64_t value, VisualGraphValueType type)
    : value_(value)
    , type_(type) {}

VisualGraphValueType VisualGraphRuntimeValue::Type() const noexcept {
    return type_;
}

bool VisualGraphRuntimeValue::AsBool(bool fallback) const noexcept {
    const bool* value = std::get_if<bool>(&value_);
    return value == nullptr ? fallback : *value;
}

int VisualGraphRuntimeValue::AsInt(int fallback) const noexcept {
    const int* value = std::get_if<int>(&value_);
    return value == nullptr ? fallback : *value;
}

float VisualGraphRuntimeValue::AsFloat(float fallback) const noexcept {
    const float* value = std::get_if<float>(&value_);
    return value == nullptr ? fallback : *value;
}

const std::string& VisualGraphRuntimeValue::AsString() const noexcept {
    const std::string* value = std::get_if<std::string>(&value_);
    return value == nullptr ? kEmptyString : *value;
}

std::uint64_t VisualGraphRuntimeValue::AsUInt64(std::uint64_t fallback) const noexcept {
    const std::uint64_t* value = std::get_if<std::uint64_t>(&value_);
    return value == nullptr ? fallback : *value;
}

} // namespace kb::visual
