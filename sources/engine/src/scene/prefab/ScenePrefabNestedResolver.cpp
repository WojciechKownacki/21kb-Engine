#include "scene/prefab/ScenePrefabNestedResolver.hpp"

#include "scene/prefab/ScenePrefabNestedAppendContext.hpp"

#include <span>

namespace kb::scene {

ScenePrefab ScenePrefabNestedResolver::Resolve(const ScenePrefabRegistry& registry, const ScenePrefab& prefab) {
    std::vector<std::string> stack;
    return Resolve(registry, prefab, stack);
}

ScenePrefab ScenePrefabNestedResolver::Resolve(const ScenePrefabRegistry& registry, const ScenePrefab& prefab, std::vector<std::string>& stack) {
    ScenePrefab resolved;
    resolved.Reserve(prefab.NodeCount());
    ScenePrefabNestedAppendContext context{ registry, prefab, resolved, stack };
    const std::span<const ScenePrefabNodeDesc> nodes = prefab.Nodes();
    for (std::uint32_t index = 0; index < static_cast<std::uint32_t>(nodes.size()); ++index) {
        if (nodes[index].parentNode == ScenePrefabNodeDesc::NoParent) {
            context.Append(index, ScenePrefabNodeDesc::NoParent);
        }
    }
    return resolved;
}

} // namespace kb::scene
