#pragma once

#include "engine/network/ReplicationSchema.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <span>

namespace kb::net {

struct FieldEncoding {
    enum class Kind { Float, Int, UInt, Bool };
    Kind kind = Kind::Float;
    float minimum = 0.0F, maximum = 0.0F;
    std::uint8_t bits = 0U;
    std::uint32_t maxUInt = 0U;

    [[nodiscard]] bool Valid() const {
        if (kind == Kind::Bool || kind == Kind::UInt) return true;
        if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum >= maximum || !std::isfinite(maximum - minimum)) return false;
        if (kind == Kind::Int) return minimum == std::floor(minimum) && maximum == std::floor(maximum) && minimum >= -16'777'216.0F && maximum <= 16'777'216.0F;
        return bits >= 1U && bits <= 24U;
    }
    [[nodiscard]] std::uint32_t MaximumCode() const {
        if (kind == Kind::Float) return (1U << bits) - 1U;
        if (kind == Kind::Int) return static_cast<std::uint32_t>(static_cast<double>(maximum) - minimum);
        return kind == Kind::Bool ? 1U : maxUInt;
    }
    [[nodiscard]] std::size_t Size() const {
        const auto maximumCode = MaximumCode();
        return maximumCode <= 0xFFU ? 1U : maximumCode <= 0xFFFFU ? 2U : maximumCode <= 0xFFFFFFU ? 3U : 4U;
    }
    [[nodiscard]] kb::network::ReplicatedField Specification() const {
        kb::network::ReplicatedField field;
        field.id = 1U;
        field.name = "value";
        field.type = kb::network::ReplicatedFieldType::QuantizedFloat;
        field.minimum = minimum; field.maximum = maximum; field.quantizationBits = bits;
        return field;
    }
    void Encode(double value, std::span<std::uint8_t> out) const {
        if (!std::isfinite(value)) value = kind == Kind::Float || kind == Kind::Int ? minimum : 0.0;
        std::uint32_t encoded;
        if (kind == Kind::Float) encoded = *kb::network::QuantizeFloat(Specification(), static_cast<float>(std::clamp(value, static_cast<double>(minimum), static_cast<double>(maximum))));
        else if (kind == Kind::Int) encoded = static_cast<std::uint32_t>(std::llround(std::clamp(value, static_cast<double>(minimum), static_cast<double>(maximum)) - minimum));
        else encoded = static_cast<std::uint32_t>(std::llround(std::clamp(value, 0.0, static_cast<double>(MaximumCode()))));
        for (std::size_t i = 0; i < out.size(); ++i) out[i] = static_cast<std::uint8_t>(encoded >> (8U * i));
    }
    [[nodiscard]] bool Decode(std::span<const std::uint8_t> bytes, double& out) const {
        if (bytes.size() != Size()) return false;
        std::uint32_t code = 0U;
        for (std::size_t i = 0; i < bytes.size(); ++i) code |= static_cast<std::uint32_t>(bytes[i]) << (8U * i);
        if (code > MaximumCode()) return false;
        if (kind == Kind::Float) { const auto decoded = kb::network::DequantizeFloat(Specification(), code); if (!decoded) return false; out = *decoded; }
        else out = kind == Kind::Int ? static_cast<double>(minimum) + code : code;
        return true;
    }
    [[nodiscard]] std::string Describe() const {
        return std::to_string(static_cast<int>(kind)) + ":" + std::to_string(std::bit_cast<std::uint32_t>(minimum)) + ":" +
            std::to_string(std::bit_cast<std::uint32_t>(maximum)) + ":" + std::to_string(bits) + ":" + std::to_string(maxUInt);
    }
};

} // namespace kb::net
