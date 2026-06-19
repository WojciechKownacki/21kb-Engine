#include "engine/scene/ScenePrefab.hpp"

#include <algorithm>
#include <memory>

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
    : sharedObjects_(std::make_shared<std::vector<SceneObject>>(std::move(objects))) {}

ScenePrefabInstance::ScenePrefabInstance(std::shared_ptr<const std::vector<SceneObject>> objects) noexcept
    : sharedObjects_(std::move(objects)) {}

ScenePrefabInstance::ScenePrefabInstance(std::shared_ptr<const std::vector<SceneObject>> objectSlab, std::size_t objectOffset, std::size_t objectCount) noexcept
    : sharedObjectSlab_(std::move(objectSlab))
    , objectOffset_(objectOffset)
    , objectCount_(objectCount) {}

ScenePrefabInstance::ScenePrefabInstance(ScenePrefabInstanceHandle handle, std::vector<SceneObject> objects) noexcept
    : handle_(handle)
    , sharedObjects_(std::make_shared<std::vector<SceneObject>>(std::move(objects))) {}

ScenePrefabInstance::ScenePrefabInstance(ScenePrefabInstanceHandle handle, std::shared_ptr<const std::vector<SceneObject>> objects) noexcept
    : handle_(handle)
    , sharedObjects_(std::move(objects)) {}

ScenePrefabInstance::ScenePrefabInstance(ScenePrefabInstanceHandle handle, std::shared_ptr<const std::vector<SceneObject>> objectSlab, std::size_t objectOffset, std::size_t objectCount) noexcept
    : handle_(handle)
    , sharedObjectSlab_(std::move(objectSlab))
    , objectOffset_(objectOffset)
    , objectCount_(objectCount) {}

bool ScenePrefabInstance::Empty() const noexcept {
    return ActiveObjects().empty();
}

ScenePrefabInstanceHandle ScenePrefabInstance::Handle() const noexcept {
    return handle_;
}

std::size_t ScenePrefabInstance::ObjectCount() const noexcept {
    return ActiveObjects().size();
}

std::span<const SceneObject> ScenePrefabInstance::Objects() const noexcept {
    return ActiveObjects();
}

std::vector<SceneObject> ScenePrefabInstance::TakeObjects() noexcept {
    if (sharedObjects_ != nullptr) {
        std::vector<SceneObject> objects{ sharedObjects_->begin(), sharedObjects_->end() };
        sharedObjects_.reset();
        return objects;
    }
    if (sharedObjectSlab_ != nullptr) {
        const std::span<const SceneObject> objects = ActiveObjects();
        std::vector<SceneObject> output{ objects.begin(), objects.end() };
        sharedObjectSlab_.reset();
        objectOffset_ = 0U;
        objectCount_ = 0U;
        return output;
    }
    return std::move(objects_);
}

SceneObject ScenePrefabInstance::ObjectAt(std::uint32_t nodeIndex) const noexcept {
    const std::span<const SceneObject> objects = ActiveObjects();
    return nodeIndex < objects.size() ? objects[nodeIndex] : SceneObject{};
}

SceneObject ScenePrefabInstance::RootObject() const noexcept {
    const std::span<const SceneObject> objects = ActiveObjects();
    return objects.empty() ? SceneObject{} : objects.front();
}

std::shared_ptr<const std::vector<SceneObject>> ScenePrefabInstance::SharedObjects() const {
    if (sharedObjects_ != nullptr) {
        return sharedObjects_;
    }
    if (sharedObjectSlab_ != nullptr) {
        if (objectOffset_ == 0U && objectCount_ == sharedObjectSlab_->size()) {
            return sharedObjectSlab_;
        }
        const std::span<const SceneObject> objects = ActiveObjects();
        return std::make_shared<std::vector<SceneObject>>(objects.begin(), objects.end());
    }
    return std::make_shared<std::vector<SceneObject>>(objects_);
}

std::shared_ptr<const std::vector<SceneObject>> ScenePrefabInstance::SharedObjectSlab() const noexcept {
    return sharedObjectSlab_;
}

const std::vector<SceneObject>* ScenePrefabInstance::SharedObjectSlabData() const noexcept {
    return sharedObjectSlab_.get();
}

std::size_t ScenePrefabInstance::SharedObjectOffset() const noexcept {
    return objectOffset_;
}

std::size_t ScenePrefabInstance::SharedObjectCount() const noexcept {
    return objectCount_;
}

void ScenePrefabInstance::AssignHandle(ScenePrefabInstanceHandle handle) noexcept {
    handle_ = handle;
}

std::span<const SceneObject> ScenePrefabInstance::ActiveObjects() const noexcept {
    if (sharedObjects_ != nullptr) {
        return std::span<const SceneObject>{ *sharedObjects_ };
    }
    if (sharedObjectSlab_ != nullptr) {
        if (objectOffset_ > sharedObjectSlab_->size()) {
            return {};
        }
        const std::size_t available = sharedObjectSlab_->size() - objectOffset_;
        const std::size_t count = std::min(objectCount_, available);
        return std::span<const SceneObject>{ sharedObjectSlab_->data() + objectOffset_, count };
    }
    return std::span<const SceneObject>{ objects_ };
}

} // namespace kb::scene
