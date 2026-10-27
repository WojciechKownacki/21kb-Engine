#pragma once

#include "engine/scene/ScenePrefab.hpp"

#include <cstddef>

namespace kb::scene {

class ScenePrefabValidator {
public:
    ScenePrefabValidator() = delete;

    [[nodiscard]] static bool IsValid(const ScenePrefab& prefab, std::size_t rootDepth = 0U) noexcept;
};

} // namespace kb::scene
