#pragma once

#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabHandle.hpp"
#include "engine/scene/ScenePrefabInstance.hpp"
#include "engine/scene/ScenePrefabInstantiationSettings.hpp"

namespace kb::scene {

class Scene;

class ScenePrefabInstanceFacade {
public:
    ScenePrefabInstanceFacade() = delete;

    [[nodiscard]] static ScenePrefabInstance Instantiate(Scene& scene, const ScenePrefab& prefab, const ScenePrefabInstantiationSettings& settings);
    [[nodiscard]] static ScenePrefabInstance Instantiate(Scene& scene, ScenePrefabHandle handle, const ScenePrefabInstantiationSettings& settings);
};

} // namespace kb::scene
