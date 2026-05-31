#pragma once

#include "engine/scene/SceneObject.hpp"
#include "engine/scene/ScenePrefabInstanceHandle.hpp"
#include "engine/scene/ScenePrefabNode.hpp"
#include "scene/prefab/ScenePrefabInstanceRegistry.hpp"

#include <cstdint>

namespace kb::scene {

class Scene;
class ScenePrefab;
class SceneState;

struct ScenePrefabOverrideInstanceTarget {
    ScenePrefabInstanceRecord* instance = nullptr;
    ScenePrefab* prefab = nullptr;
};

struct ScenePrefabOverrideNodeTarget {
    ScenePrefabNodeDesc* node = nullptr;
    SceneObject object{};
};

class ScenePrefabOverrideTargetResolver {
public:
    ScenePrefabOverrideTargetResolver() = delete;

    [[nodiscard]] static const ScenePrefab* ResolveReadPrefab(const SceneState& state, const ScenePrefabInstanceRecord& instance) noexcept;
    [[nodiscard]] static ScenePrefabOverrideInstanceTarget ResolveMutablePrefab(SceneState& state, ScenePrefabInstanceHandle handle) noexcept;
    [[nodiscard]] static ScenePrefabOverrideNodeTarget ResolveNode(Scene& scene, ScenePrefab& prefab, ScenePrefabInstanceRecord& instance, std::uint32_t nodeIndex);
};

} // namespace kb::scene
