#include "engine/scene/ScenePrefab.hpp"

namespace kb::scene {

bool ScenePrefab::Empty() const noexcept {
    return nodes_.empty();
}

std::size_t ScenePrefab::NodeCount() const noexcept {
    return nodes_.size();
}

std::span<const ScenePrefabNodeDesc> ScenePrefab::Nodes() const noexcept {
    return nodes_;
}

std::uint32_t ScenePrefab::AddNode(ScenePrefabNodeDesc desc) {
    const std::uint32_t nodeIndex = static_cast<std::uint32_t>(nodes_.size());
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

bool ScenePrefabInstance::Empty() const noexcept {
    return objects_.empty();
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
