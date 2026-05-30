#pragma once

#include "engine/ecs/World.hpp"

#include <cstdint>
#include <cstddef>

namespace kb::scene {

class SceneEntityCounter {
public:
    SceneEntityCounter() = delete;

    [[nodiscard]] static std::size_t CountWithComponent(const kb::ecs::World& world, std::uint64_t componentId) noexcept;
};

} // namespace kb::scene
