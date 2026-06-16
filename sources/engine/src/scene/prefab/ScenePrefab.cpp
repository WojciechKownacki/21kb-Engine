#include "engine/scene/ScenePrefab.hpp"

#include <algorithm>

namespace kb::scene {
namespace {

[[nodiscard]] std::uint64_t NextStableNodeId(std::span<const ScenePrefabNodeDesc> nodes) noexcept {
    std::uint64_t nextId = 1;
    for (const ScenePrefabNodeDesc& node : nodes) {
        nextId = std::max(nextId, node.stableId + 1U);
    }
    return nextId;
}

} // namespace

bool ScenePrefab::Empty() const noexcept {
    return nodes_.empty();
}

std::size_t ScenePrefab::NodeCount() const noexcept {
    return nodes_.size();
}

std::span<const ScenePrefabNodeDesc> ScenePrefab::Nodes() const noexcept {
    return nodes_;
}

const ScenePrefabNodeDesc* ScenePrefab::TryGetNode(std::uint32_t nodeIndex) const noexcept {
    return nodeIndex < nodes_.size() ? &nodes_[nodeIndex] : nullptr;
}

ScenePrefabNodeDesc* ScenePrefab::TryGetMutableNode(std::uint32_t nodeIndex) noexcept {
    return nodeIndex < nodes_.size() ? &nodes_[nodeIndex] : nullptr;
}

std::uint32_t ScenePrefab::FindNodeIndexByStableId(std::uint64_t stableId) const noexcept {
    if (stableId == ScenePrefabNodeDesc::InvalidStableId) {
        return ScenePrefabNodeDesc::NoParent;
    }
    for (std::uint32_t nodeIndex = 0; nodeIndex < static_cast<std::uint32_t>(nodes_.size()); ++nodeIndex) {
        if (nodes_[nodeIndex].stableId == stableId) {
            return nodeIndex;
        }
    }
    return ScenePrefabNodeDesc::NoParent;
}

const ScenePrefabNodeDesc* ScenePrefab::TryGetNodeByStableId(std::uint64_t stableId) const noexcept {
    const std::uint32_t nodeIndex = FindNodeIndexByStableId(stableId);
    return nodeIndex == ScenePrefabNodeDesc::NoParent ? nullptr : TryGetNode(nodeIndex);
}

ScenePrefabNodeDesc* ScenePrefab::TryGetMutableNodeByStableId(std::uint64_t stableId) noexcept {
    const std::uint32_t nodeIndex = FindNodeIndexByStableId(stableId);
    return nodeIndex == ScenePrefabNodeDesc::NoParent ? nullptr : TryGetMutableNode(nodeIndex);
}

std::uint32_t ScenePrefab::ResolveNodeIndex(const ScenePrefabPropertyOverride& property) const noexcept {
    const std::uint32_t stableNodeIndex = FindNodeIndexByStableId(property.nodeId);
    if (stableNodeIndex != ScenePrefabNodeDesc::NoParent) {
        return stableNodeIndex;
    }
    return property.nodeIndex < nodes_.size() ? property.nodeIndex : ScenePrefabNodeDesc::NoParent;
}

std::uint32_t ScenePrefab::AddNode(ScenePrefabNodeDesc desc) {
    const std::uint32_t nodeIndex = static_cast<std::uint32_t>(nodes_.size());
    if (desc.stableId == ScenePrefabNodeDesc::InvalidStableId) {
        desc.stableId = NextStableNodeId(nodes_);
    }
    nodes_.push_back(std::move(desc));
    return nodeIndex;
}

void ScenePrefab::Reserve(std::size_t nodeCount) {
    nodes_.reserve(nodeCount);
}

void ScenePrefab::Clear() noexcept {
    nodes_.clear();
}

ScenePrefabInstance::ScenePrefabInstance(std::vector<SceneObject> objects) noexcept
    : objects_(std::move(objects)) {}

ScenePrefabInstance::ScenePrefabInstance(ScenePrefabInstanceHandle handle, std::vector<SceneObject> objects) noexcept
    : handle_(handle)
    , objects_(std::move(objects)) {}

bool ScenePrefabInstance::Empty() const noexcept {
    return objects_.empty();
}

ScenePrefabInstanceHandle ScenePrefabInstance::Handle() const noexcept {
    return handle_;
}

std::size_t ScenePrefabInstance::ObjectCount() const noexcept {
    return objects_.size();
}

std::span<const SceneObject> ScenePrefabInstance::Objects() const noexcept {
    return objects_;
}

SceneObject ScenePrefabInstance::ObjectAt(std::uint32_t nodeIndex) const noexcept {
    return nodeIndex < objects_.size() ? objects_[nodeIndex] : SceneObject{};
}

SceneObject ScenePrefabInstance::RootObject() const noexcept {
    return objects_.empty() ? SceneObject{} : objects_.front();
}

} // namespace kb::scene
