#pragma once

#include "engine/scene/ScenePrefab.hpp"

namespace kb::scene {

class Scene;

class ScenePrefabInstantiationService {
public:
    ScenePrefabInstantiationService() = delete;

    [[nodiscard]] static ScenePrefabInstance Instantiate(Scene& scene, const ScenePrefab& prefab, const ScenePrefabInstantiationSettings& settings);
};

} // namespace kb::scene
