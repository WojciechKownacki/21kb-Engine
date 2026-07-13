#pragma once

#include "engine/visual/VisualGraphRuntimeValue.hpp"
#include "engine/visual/VisualGraphTypes.hpp"

#include <cstdint>
#include <string>
#include <variant>

namespace kb::script {

enum class ScriptValueType : std::uint8_t {
    Void,
    Bool,
    Int,
    Float,
    String,
    Entity,
    Component,
};

class ScriptValue final {
public:
    using Storage = std::variant<std::monostate, bool, int, float, std::string, std::uint64_t>;

    ScriptValue() = default;
    explicit ScriptValue(bool value);
    explicit ScriptValue(int value);
    explicit ScriptValue(float value);
    explicit ScriptValue(std::string value);
    ScriptValue(std::uint64_t value, ScriptValueType type);

    [[nodiscard]] ScriptValueType Type() const noexcept;
    [[nodiscard]] bool AsBool(bool fallback = false) const noexcept;
    [[nodiscard]] int AsInt(int fallback = 0) const noexcept;
    [[nodiscard]] float AsFloat(float fallback = 0.0F) const noexcept;
    [[nodiscard]] const std::string& AsString() const noexcept;
    [[nodiscard]] std::uint64_t AsUInt64(std::uint64_t fallback = 0U) const noexcept;
    [[nodiscard]] kb::visual::VisualGraphRuntimeValue ToVisualGraphValue() const;

    // Structural equality: same declared ScriptValueType and same stored
    // value. Two values built from the same underlying storage alternative
    // but different declared types (e.g. an Entity and a Component both
    // wrapping the raw id 5) compare unequal, since they mean different
    // things even though std::variant's payload matches.
    [[nodiscard]] bool operator==(const ScriptValue& other) const noexcept {
        return type_ == other.type_ && value_ == other.value_;
    }
    [[nodiscard]] bool operator!=(const ScriptValue& other) const noexcept {
        return !(*this == other);
    }

private:
    Storage value_{};
    ScriptValueType type_ = ScriptValueType::Void;
};

[[nodiscard]] const char* ToString(ScriptValueType type) noexcept;
[[nodiscard]] kb::visual::VisualGraphValueType ToVisualGraphValueType(ScriptValueType type) noexcept;

} // namespace kb::script
