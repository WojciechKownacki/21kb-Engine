#pragma once

#include "engine/save/SaveValue.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kb::scene {

enum class AiBlackboardScope : std::uint8_t { Entity, Team, World };

struct AiBlackboardKey {
    std::string name;
    kb::save::SaveValueType type = kb::save::SaveValueType::Int;

    [[nodiscard]] bool IsValid() const noexcept { return !name.empty() && name.size() <= 128U; }
};

// `owner` is ignored for World, entity id for Entity, and an authored team id
// for Team. A type mismatch is always a miss; writes never coerce values.
class AiBlackboard final {
public:
    [[nodiscard]] bool Set(AiBlackboardScope scope, std::uint64_t owner, const AiBlackboardKey& key, kb::save::SaveValue value);
    [[nodiscard]] std::optional<kb::save::SaveValue> Get(AiBlackboardScope scope, std::uint64_t owner, const AiBlackboardKey& key) const;
    [[nodiscard]] bool Remove(AiBlackboardScope scope, std::uint64_t owner, std::string_view key) noexcept;
    void Clear(AiBlackboardScope scope, std::uint64_t owner) noexcept;

    // Deterministic, versioned bytes. Serialization includes each scope and
    // owner id, so entity and team values cannot accidentally alias world data.
    [[nodiscard]] std::vector<std::uint8_t> Serialize() const;
    [[nodiscard]] bool Deserialize(std::span<const std::uint8_t> bytes);

private:
    using Values = std::unordered_map<std::string, kb::save::SaveValue>;
    [[nodiscard]] Values* FindMutable(AiBlackboardScope scope, std::uint64_t owner) noexcept;
    [[nodiscard]] const Values* Find(AiBlackboardScope scope, std::uint64_t owner) const noexcept;

    Values world_;
    std::unordered_map<std::uint64_t, Values> teams_;
    std::unordered_map<std::uint64_t, Values> entities_;
};

} // namespace kb::scene
