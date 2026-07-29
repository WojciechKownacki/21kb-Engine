#pragma once

#include "engine/visual/VisualGraphTypes.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace kb::visual {

class VisualGraphRuntimeValue final {
public:
    // LIB-041: UInt32/Hash reuse the std::uint64_t alternative (tagged by
    // `type_`, same pattern already used for Entity/Component) and
    // Name/Guid reuse the std::string alternative (tagged by `type_`) —
    // only Int64 and Double need genuinely new alternatives.
    using Storage = std::variant<std::monostate, bool, int, float, std::string, std::uint64_t, std::int64_t, double>;

    VisualGraphRuntimeValue() = default;
    explicit VisualGraphRuntimeValue(bool value);
    explicit VisualGraphRuntimeValue(int value);
    explicit VisualGraphRuntimeValue(float value);
    explicit VisualGraphRuntimeValue(double value);
    explicit VisualGraphRuntimeValue(std::int64_t value);
    explicit VisualGraphRuntimeValue(std::string value);
    explicit VisualGraphRuntimeValue(std::string value, VisualGraphValueType type);
    explicit VisualGraphRuntimeValue(std::uint64_t value, VisualGraphValueType type);

    [[nodiscard]] VisualGraphValueType Type() const noexcept;
    [[nodiscard]] bool AsBool(bool fallback = false) const noexcept;
    [[nodiscard]] int AsInt(int fallback = 0) const noexcept;
    [[nodiscard]] float AsFloat(float fallback = 0.0F) const noexcept;
    [[nodiscard]] double AsDouble(double fallback = 0.0) const noexcept;
    [[nodiscard]] std::int64_t AsInt64(std::int64_t fallback = 0) const noexcept;
    [[nodiscard]] const std::string& AsString() const noexcept;
    [[nodiscard]] std::uint64_t AsUInt64(std::uint64_t fallback = 0U) const noexcept;
    [[nodiscard]] std::optional<VisualGraphRuntimeValue> ConvertLosslessly(VisualGraphValueType target) const noexcept;

private:
    Storage value_{};
    VisualGraphValueType type_ = VisualGraphValueType::Void;
};

} // namespace kb::visual
