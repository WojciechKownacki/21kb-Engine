#pragma once

#include "engine/scene/ScenePrefab.hpp"

namespace kb::scene {

class Scene;

class ScenePrefabNodeFactory {
public:
    ScenePrefabNodeFactory() = delete;

    [[nodiscard]] static SceneObject Create(Scene& scene, const ScenePrefabNodeDesc& node, const ScenePrefabInstantiationSettings& settings, std::span<const SceneObject> createdObjects);
};

} // namespace kb::scene
