#include "scene/prefab/ScenePrefabInstanceRelinker.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/assets/ScenePrefabGuidAssetIndex.hpp"
#include "scene/prefab/ScenePrefabHasher.hpp"
#include "scene/prefab/ScenePrefabInstanceSynchronizer.hpp"
#include "scene/prefab/ScenePrefabNestedResolver.hpp"
#include "scene/prefab/ScenePrefabRecord.hpp"
#include "scene/prefab/io/ScenePrefabAssetService.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace kb::scene {
namespace {

constexpr std::uint32_t kNoNode = ScenePrefabNodeDesc::NoParent;

struct SharedResolvedPrefab {
    ScenePrefabHandle prefab;
    std::shared_ptr<const std::string> guid;
    std::shared_ptr<const ScenePrefab> resolved;
    std::shared_ptr<const std::vector<std::uint64_t>> nodeIds;
    std::uint64_t contentHash = 0U;
    // (stable id, node index), sorted by id.
    std::vector<std::pair<std::uint64_t, std::uint32_t>> nodeIndexById;
};

[[nodiscard]] ScenePrefabHandle FindOrLoadPrefab(Scene& scene, const std::string& guid, std::optional<ScenePrefabGuidAssetIndex>& assetIndex) {
    SceneState& state = SceneAccess::State(scene);
    const ScenePrefabHandle registered = state.prefabs.FindByGuid(guid);
    if (registered.IsValid()) {
        return registered;
    }

    if (!assetIndex.has_value()) {
        assetIndex.emplace();
    }
    const kb::assets::AssetRegistry& registry = scene.Assets().Manager().Registry();
    const ScenePrefabGuidClaim* claim = assetIndex->Find(registry, kb::assets::MakeAssetId(guid));
    const kb::assets::AssetMetadata* metadata = claim == nullptr || claim->IsContested() ? nullptr : registry.Find(claim->assetId);
    if (metadata == nullptr) {
        return {};
    }
    static_cast<void>(ScenePrefabAssetService::Load(scene, metadata->physicalPath));
    return state.prefabs.FindByGuid(guid);
}

[[nodiscard]] const SharedResolvedPrefab* FindOrResolve(SceneState& state, ScenePrefabHandle handle, std::vector<SharedResolvedPrefab>& resolvedPrefabs) {
    const auto known = std::ranges::find(resolvedPrefabs, handle, &SharedResolvedPrefab::prefab);
    if (known != resolvedPrefabs.end()) {
        return &*known;
    }

    const ScenePrefabRecord* record = state.prefabs.FindRecord(handle);
    if (record == nullptr) {
        return nullptr;
    }
    auto resolved = std::make_shared<const ScenePrefab>(ScenePrefabNestedResolver::Resolve(state.prefabs, record->prefab));
    auto nodeIds = std::make_shared<std::vector<std::uint64_t>>();
    nodeIds->reserve(resolved->NodeCount());
    std::vector<std::pair<std::uint64_t, std::uint32_t>> nodeIndexById;
    nodeIndexById.reserve(resolved->NodeCount());
    for (const ScenePrefabNodeDesc& node : resolved->Nodes()) {
        nodeIndexById.emplace_back(node.stableId, static_cast<std::uint32_t>(nodeIds->size()));
        nodeIds->push_back(node.stableId);
    }
    std::ranges::sort(nodeIndexById);
    const std::uint64_t contentHash = ScenePrefabHasher::Hash(*resolved);
    SharedResolvedPrefab& shared = resolvedPrefabs.emplace_back();
    shared.prefab = handle;
    shared.guid = std::make_shared<const std::string>(record->guid);
    shared.resolved = std::move(resolved);
    shared.nodeIds = std::move(nodeIds);
    shared.contentHash = contentHash;
    shared.nodeIndexById = std::move(nodeIndexById);
    return &shared;
}

// The overlay's overrides tell how the captured instance differed from its prefab: a renamed node
// is found under its new name, and a node the instance had lost has no object to link.
[[nodiscard]] const std::string* ExpectedName(const ScenePrefab& resolved, const ScenePrefabNodeDesc& overlay, std::span<const std::uint32_t> overrideNodes, std::uint32_t nodeIndex) noexcept {
    const std::string* name = &resolved.Nodes()[nodeIndex].name;
    for (std::size_t index = 0U; index < overrideNodes.size(); ++index) {
        if (overrideNodes[index] != nodeIndex) {
            continue;
        }
        const ScenePrefabPropertyOverride& property = overlay.nestedPrefabOverrides[index];
        if (property.flag == ScenePrefabOverrideFlag::MissingObject) {
            return nullptr;
        }
        if (property.flag == ScenePrefabOverrideFlag::Name) {
            name = &property.value;
        }
    }
    return name;
}

} // namespace

void ScenePrefabInstanceRelinker::Relink(Scene& scene, const ScenePrefab& prefab, SceneObject parent, const ScenePrefabInstance& instance) {
    const std::span<const ScenePrefabNodeDesc> nodes = prefab.Nodes();
    const auto firstLinked = std::ranges::find_if(nodes, [](const ScenePrefabNodeDesc& node) noexcept {
        return !node.nestedPrefabGuid.empty();
    });
    if (firstLinked == nodes.end() || instance.ObjectCount() != nodes.size()) {
        return;
    }

    // Instantiation creates nodes in index order, so these chains are also each object's child order.
    const std::uint32_t nodeCount = static_cast<std::uint32_t>(nodes.size());
    std::vector<std::uint32_t> firstChild(nodeCount, kNoNode);
    std::vector<std::uint32_t> nextSibling(nodeCount, kNoNode);
    std::vector<std::uint32_t> lastChild(nodeCount, kNoNode);
    for (std::uint32_t node = 0U; node < nodeCount; ++node) {
        const std::uint32_t parentNode = nodes[node].parentNode;
        if (parentNode >= node) {
            continue;
        }
        if (lastChild[parentNode] == kNoNode) {
            firstChild[parentNode] = node;
        } else {
            nextSibling[lastChild[parentNode]] = node;
        }
        lastChild[parentNode] = node;
    }

    SceneState& state = SceneAccess::State(scene);
    std::vector<std::uint8_t> claimed(nodeCount, 0U);
    std::optional<ScenePrefabGuidAssetIndex> assetIndex;
    std::vector<SharedResolvedPrefab> resolvedPrefabs;
    std::vector<std::uint32_t> sourceNodes;
    std::vector<std::uint32_t> overrideNodes;
    std::vector<std::uint32_t> subtree;
    for (std::uint32_t root = static_cast<std::uint32_t>(firstLinked - nodes.begin()); root < nodeCount; ++root) {
        const ScenePrefabNodeDesc& overlay = nodes[root];
        if (overlay.nestedPrefabGuid.empty()) {
            continue;
        }
        const ScenePrefabHandle handle = FindOrLoadPrefab(scene, overlay.nestedPrefabGuid, assetIndex);
        const SharedResolvedPrefab* shared = handle.IsValid() ? FindOrResolve(state, handle, resolvedPrefabs) : nullptr;
        if (shared == nullptr || shared->resolved->Empty()) {
            continue;
        }

        const ScenePrefab& resolved = *shared->resolved;
        const std::span<const ScenePrefabNodeDesc> resolvedNodes = resolved.Nodes();
        sourceNodes.assign(resolvedNodes.size(), kNoNode);
        sourceNodes[0] = root;

        // Depth first through the subtree, which is the order the capture recorded node ids in.
        subtree.clear();
        for (std::uint32_t node = root;;) {
            subtree.push_back(node);
            if (firstChild[node] != kNoNode) {
                node = firstChild[node];
                continue;
            }
            while (node != root && nextSibling[node] == kNoNode) {
                node = nodes[node].parentNode;
            }
            if (node == root) {
                break;
            }
            node = nextSibling[node];
        }

        if (subtree.size() == overlay.nestedPrefabNodeIds.size()) {
            for (std::size_t index = 1U; index < subtree.size(); ++index) {
                const std::uint64_t nodeId = overlay.nestedPrefabNodeIds[index];
                const auto found = std::ranges::lower_bound(shared->nodeIndexById, nodeId, {}, &std::pair<std::uint64_t, std::uint32_t>::first);
                if (nodeId != ScenePrefabNodeDesc::InvalidStableId && found != shared->nodeIndexById.end() && found->first == nodeId &&
                    sourceNodes[found->second] == kNoNode) {
                    sourceNodes[found->second] = subtree[index];
                }
            }
        } else {
            // A scene saved before objects recorded their prefab node: the first unclaimed child with the node's name.
            overrideNodes.clear();
            for (const ScenePrefabPropertyOverride& property : overlay.nestedPrefabOverrides) {
                overrideNodes.push_back(resolved.ResolveNodeIndex(property));
            }
            for (std::uint32_t node = 1U; node < static_cast<std::uint32_t>(resolvedNodes.size()); ++node) {
                const std::uint32_t parentNode = resolvedNodes[node].parentNode;
                const std::uint32_t sourceParent = parentNode < node ? sourceNodes[parentNode] : kNoNode;
                const std::string* name = sourceParent == kNoNode ? nullptr : ExpectedName(resolved, overlay, overrideNodes, node);
                if (name == nullptr) {
                    continue;
                }
                // A child that names its own prefab is a separate instance placed under this one.
                for (std::uint32_t child = firstChild[sourceParent]; child != kNoNode; child = nextSibling[child]) {
                    if (claimed[child] == 0U && nodes[child].nestedPrefabGuid.empty() && nodes[child].name == *name) {
                        sourceNodes[node] = child;
                        claimed[child] = 1U;
                        break;
                    }
                }
            }
        }

        std::vector<SceneObject> objects(resolvedNodes.size());
        for (std::size_t node = 0U; node < sourceNodes.size(); ++node) {
            if (sourceNodes[node] != kNoNode) {
                objects[node] = instance.ObjectAt(sourceNodes[node]);
            }
        }
        const SceneObject rootParent = overlay.parentNode == kNoNode ? parent : instance.ObjectAt(overlay.parentNode);
        const ScenePrefabInstanceHandle linked = state.prefabInstances.Register(handle, {}, rootParent, std::move(objects), {});
        if (ScenePrefabInstanceRecord* record = state.prefabInstances.FindMutable(linked)) {
            record->sharedPrefabGuid = shared->guid;
            record->SetSharedResolvedPrefab(shared->resolved, shared->nodeIds);
            // A capture taken against an older version of the prefab follows the prefab as it is now.
            if (overlay.nestedPrefabContentHash != shared->contentHash) {
                const std::span<const SceneObject> captured = record->Objects();
                const std::vector<SceneObject> capturedObjects{ captured.begin(), captured.end() };
                static_cast<void>(ScenePrefabInstanceSynchronizer::Rebase(scene, *record, overlay.nestedPrefabOverrides));
                state.prefabInstances.ReindexObjects(linked, capturedObjects);
            }
        }
    }
}

} // namespace kb::scene
