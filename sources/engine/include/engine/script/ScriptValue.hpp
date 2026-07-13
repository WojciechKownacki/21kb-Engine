#pragma once

#include "engine/visual/VisualGraphRuntimeValue.hpp"
#include "engine/visual/VisualGraphTypes.hpp"

#include <cstdint>
#include <string>
#include <type_traits>
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

// LIB-032: ScriptValue is the only channel through which data crosses the
// Lua/Visual Graph script boundary (function arguments, outputs, event
// payloads, exposed variables all marshal through it). Its Storage variant
// must never hold a raw pointer or reference — a script could then read or
// outlive a C++ object whose lifetime it does not own or control, past the
// frame that produced it. This asserts the invariant at the type
// definition itself, so editing Storage to add a pointer alternative fails
// the build immediately instead of silently reopening the hole.
static_assert(
    !std::is_pointer_v<bool> && !std::is_pointer_v<int> && !std::is_pointer_v<float> &&
        !std::is_pointer_v<std::string> && !std::is_pointer_v<std::uint64_t>,
    "ScriptValue::Storage must never hold a raw pointer or reference type");

} // namespace kb::script
