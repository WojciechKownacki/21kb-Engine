#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/ScenePrefabNode.hpp"
#include "scene/prefab/ScenePrefabInstanceRegistry.hpp"

#include <vector>

namespace kb::scene {

class Scene;

using ScenePrefabTrackedEntitySet = std::vector<SceneEntity::IdType>;

class ScenePrefabInstanceTopology {
public:
    ScenePrefabInstanceTopology() = delete;

    [[nodiscard]] static SceneObject ExpectedParent(const ScenePrefabNodeDesc& node, const ScenePrefabInstanceRecord& instance) noexcept;
    [[nodiscard]] static ScenePrefabTrackedEntitySet TrackedEntities(const ScenePrefabInstanceRecord& instance);
    [[nodiscard]] static bool AllTrackedObjectsAlive(Scene& scene, const ScenePrefabInstanceRecord& instance);
    [[nodiscard]] static bool HasUntrackedChild(Scene& scene, SceneObject object, const ScenePrefabTrackedEntitySet& trackedEntities);
    static void DestroyUntrackedChildren(Scene& scene, const ScenePrefabInstanceRecord& instance, const ScenePrefabTrackedEntitySet& trackedEntities);
};

} // namespace kb::scene
