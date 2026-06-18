#pragma once

#include <compare>
#include <cstdint>

namespace kb::ecs {

class Entity {
public:
    using IdType = std::uint64_t;

    constexpr Entity() noexcept = default;
    explicit constexpr Entity(IdType id) noexcept
        : id_(id) {}

    [[nodiscard]] constexpr IdType Id() const noexcept { return id_; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return id_ != 0; }

    constexpr auto operator<=>(const Entity&) const noexcept = default;

private:
    IdType id_ = 0;
};

inline constexpr Entity::IdType kGeneratedEntityIndexBase = 1'000'000;
inline constexpr std::uint32_t kInvalidGeneratedEntityIndex = UINT32_MAX;

[[nodiscard]] constexpr std::uint32_t GeneratedEntityIndex(Entity entity) noexcept {
    const Entity::IdType packedIndex = entity.Id() & 0xFFFFFFFFULL;
    return packedIndex < kGeneratedEntityIndexBase ? kInvalidGeneratedEntityIndex : static_cast<std::uint32_t>(packedIndex - kGeneratedEntityIndexBase);
}

[[nodiscard]] constexpr bool HasGeneratedEntityIndex(Entity entity) noexcept {
    return GeneratedEntityIndex(entity) != kInvalidGeneratedEntityIndex;
}

} // namespace kb::ecs
