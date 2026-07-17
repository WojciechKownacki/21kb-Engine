#pragma once

#include "engine/visual/VisualGraphRuntimeValue.hpp"
#include "engine/visual/VisualGraphTypes.hpp"

#include <cstdint>
#include <string>
#include <string_view>
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
    // LIB-041: appended after Component (not inserted between existing
    // values) so the numeric value of every pre-existing enumerator stays
    // stable for anything that persists it by integer (e.g.
    // kb::library::DescribeType's array index). `Int` already satisfies
    // the plan's "Int32" (a native `int` is 32-bit on every platform this
    // engine targets) — no separate Int32 enumerator was added to avoid a
    // pure-rename churn across every existing `Int` call site.
    Int64,
    UInt32,
    Double,
    Name,
    Guid,
    Hash,
};

class ScriptValue final {
public:
    // LIB-041: UInt32/Hash reuse the std::uint64_t alternative (tagged by
    // `type_`, same pattern already used for Entity/Component) and
    // Name/Guid reuse the std::string alternative (tagged by `type_`) —
    // only Int64 and Double need genuinely new alternatives.
    using Storage = std::variant<std::monostate, bool, int, float, std::string, std::uint64_t, std::int64_t, double>;

    ScriptValue() = default;
    explicit ScriptValue(bool value);
    explicit ScriptValue(int value);
    explicit ScriptValue(float value);
    explicit ScriptValue(double value);
    explicit ScriptValue(std::int64_t value);
    // Dedicated (not the generic uint64_t+type ctor below) so a UInt32
    // ScriptValue can never be constructed out of 32-bit range: the
    // narrower std::uint32_t parameter makes an out-of-range value
    // impossible to pass in the first place, rather than needing a
    // validation branch with a silent-clamp-or-reject decision.
    explicit ScriptValue(std::uint32_t value);
    explicit ScriptValue(std::string value);
    // Name and Guid are both "a string used as an opaque identifier" at
    // the LIB-041 value-type level (format validation for Guid and any
    // future interning for Name are separate, later concerns — see
    // LIB-063 and the Engine21kbLibrary.md note next to this ctor's .cpp
    // definition). An unrecognized `type` here falls back to String,
    // mirroring the existing uint64_t+type ctor's fallback-to-Void
    // pattern for its own restricted type set.
    ScriptValue(std::string value, ScriptValueType type);
    ScriptValue(std::uint64_t value, ScriptValueType type);

    [[nodiscard]] ScriptValueType Type() const noexcept;
    [[nodiscard]] bool AsBool(bool fallback = false) const noexcept;
    [[nodiscard]] int AsInt(int fallback = 0) const noexcept;
    [[nodiscard]] float AsFloat(float fallback = 0.0F) const noexcept;
    [[nodiscard]] double AsDouble(double fallback = 0.0) const noexcept;
    [[nodiscard]] std::int64_t AsInt64(std::int64_t fallback = 0) const noexcept;
    // UInt32 is stored in the same std::uint64_t alternative as
    // Entity/Component/Hash; only ever constructed via the dedicated
    // std::uint32_t ctor above, so the narrowing cast back is always
    // lossless for a value actually tagged UInt32.
    [[nodiscard]] std::uint32_t AsUInt32(std::uint32_t fallback = 0U) const noexcept;
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
// The exact inverse of ToString(ScriptValueType): parses one of the
// canonical PascalCase names ToString emits back to its enum value. Defined
// in terms of ToString itself (not a second hand-maintained string table),
// so it can never drift from ToString as the enum grows. Returns false for
// any string ToString never produces. Used by tooling that reads back a
// machine-generated API catalog (kb_cli api-check, LIB-024) — deliberately
// strict (no lowercase aliases), unlike ScriptAssetLoader's own lenient
// parser for hand-authored `-- @expose` directives.
[[nodiscard]] bool TryParse(std::string_view text, ScriptValueType& out) noexcept;
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
        !std::is_pointer_v<std::string> && !std::is_pointer_v<std::uint64_t> &&
        !std::is_pointer_v<std::int64_t> && !std::is_pointer_v<double>,
    "ScriptValue::Storage must never hold a raw pointer or reference type");

} // namespace kb::script
