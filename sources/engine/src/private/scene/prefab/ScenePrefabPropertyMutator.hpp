#pragma once

#include "engine/scene/SceneObject.hpp"
#include "engine/scene/ScenePrefabNode.hpp"
#include "scene/prefab/ScenePrefabInstanceRegistry.hpp"

#include <string_view>

namespace kb::scene {

class Scene;

class ScenePrefabPropertyMutator {
public:
    ScenePrefabPropertyMutator() = delete;

    [[nodiscard]] static bool Revert(Scene& scene, const ScenePrefabInstanceRecord& instance, SceneObject object, const ScenePrefabNodeDesc& node, std::string_view propertyPath);
    [[nodiscard]] static bool Apply(Scene& scene, ScenePrefabNodeDesc& node, SceneObject object, std::string_view propertyPath);
};

} // namespace kb::scene
