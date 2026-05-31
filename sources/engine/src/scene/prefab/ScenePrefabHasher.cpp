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
        ScenePrefabHashBuilder::MixString(hash, node.name);
        ScenePrefabHashBuilder::Mix(hash, node.parentNode);
        ScenePrefabHashBuilder::MixTransform(hash, node.transform);
        ScenePrefabHashBuilder::Mix(hash, node.visibility.visible ? 1U : 0U);
        ScenePrefabComponentHasher::Mix(hash, node.components);
    }
    return hash;
}

} // namespace kb::scene
