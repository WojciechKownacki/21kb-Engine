#pragma once

#include "engine/scene/SceneObject.hpp"
#include "engine/scene/ScenePrefabNode.hpp"
#include "scene/prefab/ScenePrefabInstanceRegistry.hpp"

#include <string_view>

namespace kb::scene {

class Scene;

class ScenePrefabPropertyReverter {
public:
    ScenePrefabPropertyReverter() = delete;

    [[nodiscard]] static bool Revert(Scene& scene, const ScenePrefabInstanceRecord& instance, SceneObject object, const ScenePrefabNodeDesc& node, std::string_view propertyPath);
};

} // namespace kb::scene
