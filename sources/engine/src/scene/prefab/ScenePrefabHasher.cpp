#include "scene/prefab/ScenePrefabHasher.hpp"

#include "scene/prefab/ScenePrefabComponentHasher.hpp"
#include "scene/prefab/ScenePrefabHashBuilder.hpp"

namespace kb::scene {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;

} // namespace

std::uint64_t ScenePrefabHasher::Hash(const ScenePrefab& prefab) noexcept {
    std::uint64_t hash = kFnvOffset;
    ScenePrefabHashBuilder::Mix(hash, prefab.NodeCount());
    for (const ScenePrefabNodeDesc& node : prefab.Nodes()) {
        ScenePrefabHashBuilder::Mix(hash, node.stableId);
        ScenePrefabHashBuilder::MixString(hash, node.name);
        ScenePrefabHashBuilder::MixString(hash, node.nestedPrefabGuid);
        ScenePrefabHashBuilder::Mix(hash, node.nestedPrefabOverrides.size());
        for (const ScenePrefabPropertyOverride& property : node.nestedPrefabOverrides) {
            ScenePrefabHashBuilder::Mix(hash, property.nodeIndex);
            ScenePrefabHashBuilder::Mix(hash, property.nodeId);
            ScenePrefabHashBuilder::MixString(hash, property.propertyPath);
            ScenePrefabHashBuilder::MixString(hash, property.value);
            ScenePrefabHashBuilder::Mix(hash, static_cast<std::uint32_t>(property.flag));
        }
        ScenePrefabHashBuilder::Mix(hash, node.parentNode);
        ScenePrefabHashBuilder::MixTransform(hash, node.transform);
        ScenePrefabHashBuilder::Mix(hash, static_cast<std::uint32_t>(node.visibility.mode));
        ScenePrefabHashBuilder::Mix(hash, node.visibility.mask);
        ScenePrefabComponentHasher::Mix(hash, node.components);
    }
    return hash;
}

} // namespace kb::scene
