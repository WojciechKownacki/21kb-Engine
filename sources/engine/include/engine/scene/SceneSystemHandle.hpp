#pragma once

#include <cstdint>

namespace kb::scene {

class SceneSystemScheduler;

// Stable ownership token for a scheduler-owned scene system. The process-unique
// scheduler lifetime identity makes stale and foreign-scene tokens harmless even if a
// later scheduler reuses the same address; only a scheduler can create a non-empty token.
class SceneSystemHandle final {
public:
    SceneSystemHandle() = default;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return schedulerLifetime_ != 0U && entry_ != 0U;
    }
    [[nodiscard]] friend constexpr bool operator==(SceneSystemHandle, SceneSystemHandle) noexcept = default;

private:
    friend class SceneSystemScheduler;

    constexpr SceneSystemHandle(std::uint64_t schedulerLifetime, std::uint64_t entry) noexcept
        : schedulerLifetime_(schedulerLifetime)
        , entry_(entry) {}

    std::uint64_t schedulerLifetime_ = 0U;
    std::uint64_t entry_ = 0U;
};

} // namespace kb::scene
