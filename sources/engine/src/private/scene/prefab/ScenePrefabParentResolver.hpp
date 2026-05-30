#pragma once

#include "engine/scene/ScenePrefab.hpp"

namespace kb::scene {

class ScenePrefabParentResolver {
public:
    ScenePrefabParentResolver() = delete;

    [[nodiscard]] static SceneObject Resolve(const ScenePrefabNodeDesc& node, const ScenePrefabInstantiationSettings& settings, std::span<const SceneObject> createdObjects) noexcept;
};

} // namespace kb::scene
