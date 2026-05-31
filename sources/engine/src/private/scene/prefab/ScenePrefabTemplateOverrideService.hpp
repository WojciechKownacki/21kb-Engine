#pragma once

#include "engine/scene/SceneObject.hpp"
#include "engine/scene/ScenePrefabNode.hpp"
#include "scene/prefab/ScenePrefabInstanceRegistry.hpp"
#include "scene/prefab/ScenePrefabRegistry.hpp"

#include <string_view>

namespace kb::scene {

class Scene;

class ScenePrefabTemplateOverrideService {
public:
    ScenePrefabTemplateOverrideService() = delete;

    [[nodiscard]] static bool ApplyAll(Scene& scene, ScenePrefabRegistry& registry, ScenePrefabInstanceRecord& instance, ScenePrefabRecord& record);
    [[nodiscard]] static bool ApplyChildren(Scene& scene, ScenePrefabRegistry& registry, ScenePrefabInstanceRecord& instance, ScenePrefab& prefab);
    [[nodiscard]] static bool ApplyProperty(Scene& scene, ScenePrefabRegistry& registry, ScenePrefabInstanceRecord& instance, ScenePrefabNodeDesc& node, SceneObject object, std::string_view propertyPath);
};

} // namespace kb::scene
