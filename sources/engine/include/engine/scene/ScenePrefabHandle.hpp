#pragma once

#include <compare>
#include <cstdint>

namespace kb::scene {

class ScenePrefabRegistry;
class ScenePrefabRecordStore;

class ScenePrefabHandle {
public:
    constexpr ScenePrefabHandle() noexcept = default;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return id_ != 0;
    }

    constexpr auto operator<=>(const ScenePrefabHandle&) const noexcept = default;

private:
    friend class ScenePrefabRecordStore;
    friend class ScenePrefabRegistry;

    explicit constexpr ScenePrefabHandle(std::uint64_t id) noexcept
        : id_(id) {}

    std::uint64_t id_ = 0;
};

} // namespace kb::scene
