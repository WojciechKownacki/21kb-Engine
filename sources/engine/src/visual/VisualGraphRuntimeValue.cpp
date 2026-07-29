#include "engine/visual/VisualGraphRuntimeValue.hpp"

#include <limits>
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

VisualGraphRuntimeValue::VisualGraphRuntimeValue(double value)
    : value_(value)
    , type_(VisualGraphValueType::Double) {}

VisualGraphRuntimeValue::VisualGraphRuntimeValue(std::int64_t value)
    : value_(value)
    , type_(VisualGraphValueType::Int64) {}

VisualGraphRuntimeValue::VisualGraphRuntimeValue(std::string value)
    : value_(std::move(value))
    , type_(VisualGraphValueType::String) {}

VisualGraphRuntimeValue::VisualGraphRuntimeValue(std::string value, VisualGraphValueType type)
    : value_(std::move(value))
    , type_(type == VisualGraphValueType::Name || type == VisualGraphValueType::Guid ? type : VisualGraphValueType::String) {}

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

double VisualGraphRuntimeValue::AsDouble(double fallback) const noexcept {
    const double* value = std::get_if<double>(&value_);
    return value == nullptr ? fallback : *value;
}

std::int64_t VisualGraphRuntimeValue::AsInt64(std::int64_t fallback) const noexcept {
    const std::int64_t* value = std::get_if<std::int64_t>(&value_);
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

std::optional<VisualGraphRuntimeValue> VisualGraphRuntimeValue::ConvertLosslessly(VisualGraphValueType target) const noexcept {
    if (Type() == target) {
        return *this;
    }
    if (!IsImplicitVisualGraphValueConversion(Type(), target)) {
        return std::nullopt;
    }

    switch (target) {
    case VisualGraphValueType::Int64:
        if (Type() == VisualGraphValueType::Int) {
            return VisualGraphRuntimeValue{ static_cast<std::int64_t>(AsInt()) };
        }
        if (Type() == VisualGraphValueType::UInt32) {
            const std::uint64_t value = AsUInt64();
            if (value <= static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
                return VisualGraphRuntimeValue{ static_cast<std::int64_t>(value) };
            }
        }
        break;
    case VisualGraphValueType::Double:
        if (Type() == VisualGraphValueType::Int) {
            return VisualGraphRuntimeValue{ static_cast<double>(AsInt()) };
        }
        if (Type() == VisualGraphValueType::UInt32) {
            const std::uint64_t value = AsUInt64();
            if (value <= static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
                return VisualGraphRuntimeValue{ static_cast<double>(value) };
            }
        }
        if (Type() == VisualGraphValueType::Float) {
            return VisualGraphRuntimeValue{ static_cast<double>(AsFloat()) };
        }
        break;
    default:
        break;
    }
    return std::nullopt;
}

} // namespace kb::visual
