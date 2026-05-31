#pragma once

#include "engine/scene/ScenePrefab.hpp"

#include <cstdint>

namespace kb::scene {

class ScenePrefabHasher {
public:
    ScenePrefabHasher() = delete;

    [[nodiscard]] static std::uint64_t Hash(const ScenePrefab& prefab) noexcept;
};

} // namespace kb::scene
