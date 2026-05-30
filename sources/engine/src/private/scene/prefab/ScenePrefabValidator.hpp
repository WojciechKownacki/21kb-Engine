#pragma once

#include "engine/scene/ScenePrefab.hpp"

namespace kb::scene {

class ScenePrefabValidator {
public:
    ScenePrefabValidator() = delete;

    [[nodiscard]] static bool IsValid(const ScenePrefab& prefab) noexcept;
};

} // namespace kb::scene
