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

} // namespace kb::ecs
