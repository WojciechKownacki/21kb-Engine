#pragma once

#include "engine/visual/VisualGraphTypes.hpp"

#include <cstdint>
#include <string>
#include <variant>

namespace kb::visual {

class VisualGraphRuntimeValue final {
public:
    using Storage = std::variant<std::monostate, bool, int, float, std::string, std::uint64_t>;

    VisualGraphRuntimeValue() = default;
    explicit VisualGraphRuntimeValue(bool value);
    explicit VisualGraphRuntimeValue(int value);
    explicit VisualGraphRuntimeValue(float value);
    explicit VisualGraphRuntimeValue(std::string value);
    explicit VisualGraphRuntimeValue(std::uint64_t value, VisualGraphValueType type);

    [[nodiscard]] VisualGraphValueType Type() const noexcept;
    [[nodiscard]] bool AsBool(bool fallback = false) const noexcept;
    [[nodiscard]] int AsInt(int fallback = 0) const noexcept;
    [[nodiscard]] float AsFloat(float fallback = 0.0F) const noexcept;
    [[nodiscard]] const std::string& AsString() const noexcept;
    [[nodiscard]] std::uint64_t AsUInt64(std::uint64_t fallback = 0U) const noexcept;

private:
    Storage value_{};
    VisualGraphValueType type_ = VisualGraphValueType::Void;
};

} // namespace kb::visual
