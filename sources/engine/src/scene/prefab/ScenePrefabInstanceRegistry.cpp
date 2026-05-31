#include "scene/prefab/ScenePrefabInstanceRegistry.hpp"

#include <utility>

namespace kb::scene {

ScenePrefabInstanceHandle ScenePrefabInstanceRegistry::Register(ScenePrefabHandle prefab, SceneObject rootParent, std::vector<SceneObject> objects) {
    if (!prefab.IsValid() || objects.empty()) {
        return {};
    }

    const std::uint64_t id = nextId_++;
    records_.emplace(
        id,
        ScenePrefabInstanceRecord{
            .prefab = prefab,
            .rootParent = rootParent,
            .objects = std::move(objects),
        });
    return ScenePrefabInstanceHandle{ id };
}

bool ScenePrefabInstanceRegistry::Contains(ScenePrefabInstanceHandle handle) const noexcept {
    return handle.IsValid() && records_.contains(handle.id_);
}

const ScenePrefabInstanceRecord* ScenePrefabInstanceRegistry::Find(ScenePrefabInstanceHandle handle) const noexcept {
    if (!handle.IsValid()) {
        return nullptr;
    }

    const auto iterator = records_.find(handle.id_);
    return iterator == records_.end() ? nullptr : &iterator->second;
}

ScenePrefabInstanceRecord* ScenePrefabInstanceRegistry::FindMutable(ScenePrefabInstanceHandle handle) noexcept {
    if (!handle.IsValid()) {
        return nullptr;
    }

    const auto iterator = records_.find(handle.id_);
    return iterator == records_.end() ? nullptr : &iterator->second;
}

std::size_t ScenePrefabInstanceRegistry::Count() const noexcept {
    return records_.size();
}

void ScenePrefabInstanceRegistry::Clear() noexcept {
    records_.clear();
    nextId_ = 1;
}

} // namespace kb::scene
