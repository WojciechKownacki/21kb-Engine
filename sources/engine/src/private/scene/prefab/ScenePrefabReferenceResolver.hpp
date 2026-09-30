#pragma once

#include "engine/scene/ScenePrefabNode.hpp"
#include <unordered_map>

namespace kb::scene {
class Scene;
class ScenePrefabReferenceResolver {
public:
    using EntityMap = std::unordered_map<std::uint64_t, SceneEntity>;
    static void Apply(Scene& scene, const ScenePrefabNodeDesc& node, SceneEntity owner, const EntityMap& entities);
};
}
