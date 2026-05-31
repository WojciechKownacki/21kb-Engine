#pragma once

#include "engine/scene/SceneObject.hpp"
#include "engine/scene/ScenePrefabNode.hpp"

#include <string_view>

namespace kb::scene {

class Scene;

class ScenePrefabPropertyApplier {
public:
    ScenePrefabPropertyApplier() = delete;

    [[nodiscard]] static bool Apply(Scene& scene, ScenePrefabNodeDesc& node, SceneObject object, std::string_view propertyPath);
};

} // namespace kb::scene
