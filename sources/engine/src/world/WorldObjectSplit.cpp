#include "world/WorldObjectSplit.hpp"

#include "engine/ui/UIEntityReferences.hpp"
#include "engine/world/WorldObjectFile.hpp"

#include <algorithm>
#include <unordered_map>

namespace kb::world {
namespace {

using kb::scene::ScenePrefabNodeDesc;

// Rewrites one link; 0 (no link) stays 0. Returns false for a link to a node
// that is not part of the split prefab.
[[nodiscard]] bool Remap(std::uint64_t& id, const std::unordered_map<std::uint64_t, std::uint64_t>& ids) {
    if (id == 0U) {
        return true;
    }
    const auto found = ids.find(id);
    if (found == ids.end()) {
        return false;
    }
    id = found->second;
    return true;
}

} // namespace

bool SplitIntoWorldObjects(
    const kb::scene::ScenePrefab& source,
    const std::function<std::string(std::size_t, const ScenePrefabNodeDesc&)>& guidFor,
    std::vector<WorldSplitObject>& objects,
    std::string& error) {
    objects.clear();
    const auto nodes = source.Nodes();
    std::vector<std::size_t> objectOf(nodes.size());
    std::vector<std::uint32_t> localIndex(nodes.size());
    std::vector<std::uint32_t> counts;
    for (std::uint32_t node = 0U; node < static_cast<std::uint32_t>(nodes.size()); ++node) {
        const std::uint32_t parent = nodes[node].parentNode;
        if (parent == ScenePrefabNodeDesc::NoParent) {
            objectOf[node] = objects.size();
            WorldSplitObject object;
            object.sourceRootNode = node;
            object.guid = guidFor(objects.size(), nodes[node]);
            if (!IsValidWorldObjectGuid(object.guid)) {
                error = "object guid \"" + object.guid + "\" is invalid";
                return false;
            }
            objects.push_back(std::move(object));
            counts.push_back(0U);
        } else if (parent >= node) {
            error = "a node precedes its parent";
            return false;
        } else {
            objectOf[node] = objectOf[parent];
        }
        localIndex[node] = counts[objectOf[node]]++;
    }
    std::unordered_map<std::uint64_t, std::uint64_t> ids;
    std::unordered_map<std::uint64_t, std::size_t> owners;
    ids.reserve(nodes.size());
    for (std::uint32_t node = 0U; node < static_cast<std::uint32_t>(nodes.size()); ++node) {
        const std::uint64_t id = WorldObjectStableId(objects[objectOf[node]].guid, localIndex[node]);
        if (!ids.emplace(nodes[node].stableId, id).second) {
            error = "two nodes share id " + std::to_string(nodes[node].stableId);
            return false;
        }
        owners.emplace(id, objectOf[node]);
    }
    for (std::uint32_t node = 0U; node < static_cast<std::uint32_t>(nodes.size()); ++node) {
        ScenePrefabNodeDesc copy = nodes[node];
        copy.stableId = ids.at(nodes[node].stableId);
        if (copy.parentNode != ScenePrefabNodeDesc::NoParent) {
            copy.parentNode = localIndex[copy.parentNode];
        }
        kb::scene::ScenePrefabNodeComponents& components = copy.components;
        bool linked = true;
        if (components.joint.has_value() && components.joint->connectedNodeStableId != kb::scene::ScenePrefabJointComponent::UnresolvedConnectedNodeStableId) {
            linked &= Remap(components.joint->connectedNodeStableId, ids);
        }
        if (components.regionPortal.has_value()) {
            linked &= Remap(components.regionPortal->sourceCellNodeStableId, ids);
            linked &= Remap(components.regionPortal->targetCellNodeStableId, ids);
        }
        if (components.lensEcho.has_value()) {
            linked &= Remap(components.lensEcho->sourceNodeStableId, ids);
        }
        kb::scene::ForEachUIEntityReference(components.ui, [&ids](std::uint64_t& reference) {
            // A UI link to something outside the prefab is dropped, as capture does.
            if (!Remap(reference, ids)) {
                reference = 0U;
            }
        });
        if (!linked) {
            error = "\"" + copy.name + "\" links to an object that is not part of the world";
            return false;
        }
        WorldSplitObject& object = objects[objectOf[node]];
        static_cast<void>(object.prefab.AddNode(std::move(copy)));
    }
    for (WorldSplitObject& object : objects) {
        for (const std::uint64_t reference : CollectNodeReferences(object.prefab)) {
            const auto owner = owners.find(reference);
            if (owner != owners.end() && objects[owner->second].guid != object.guid) {
                object.references.push_back(objects[owner->second].guid);
            }
        }
        std::ranges::sort(object.references);
        object.references.erase(std::unique(object.references.begin(), object.references.end()), object.references.end());
    }
    return true;
}

} // namespace kb::world
