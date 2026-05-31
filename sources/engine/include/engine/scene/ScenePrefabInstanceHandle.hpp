#pragma once

#include <compare>
#include <cstdint>

namespace kb::scene {

class ScenePrefabInstanceRegistry;

class ScenePrefabInstanceHandle {
public:
    constexpr ScenePrefabInstanceHandle() noexcept = default;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return id_ != 0;
    }

    constexpr auto operator<=>(const ScenePrefabInstanceHandle&) const noexcept = default;

private:
    friend class ScenePrefabInstanceRegistry;

    explicit constexpr ScenePrefabInstanceHandle(std::uint64_t id) noexcept
        : id_(id) {}

    std::uint64_t id_ = 0;
};

} // namespace kb::scene
