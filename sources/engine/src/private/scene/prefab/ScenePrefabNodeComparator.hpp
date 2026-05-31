#pragma once

#include "engine/scene/ScenePrefabNode.hpp"
#include "engine/scene/ScenePrefabOverrides.hpp"

namespace kb::scene {

class Scene;
class SceneObject;

class ScenePrefabNodeComparator {
public:
    ScenePrefabNodeComparator() = delete;

    [[nodiscard]] static ScenePrefabOverrideFlag Compare(Scene& scene, SceneObject object, SceneObject expectedParent, const ScenePrefabNodeDesc& node);
};

} // namespace kb::scene
