#include "scene/prefab/ScenePrefabInstanceRegistry.hpp"

#include <utility>

namespace kb::scene {

ScenePrefabInstanceHandle ScenePrefabInstanceRegistry::Register(ScenePrefabHandle prefab, SceneObject rootParent, std::vector<SceneObject> objects, ScenePrefab resolvedPrefab) {
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
            .resolvedPrefab = std::move(resolvedPrefab),
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

ScenePrefabInstanceHandle ScenePrefabInstanceRegistry::FindRootInstance(SceneObject object) const noexcept {
    if (!object.IsValid()) {
        return {};
    }

    for (const auto& [id, record] : records_) {
        if (!record.objects.empty() && record.objects.front().Entity() == object.Entity()) {
            return ScenePrefabInstanceHandle{ id };
        }
    }
    return {};
}

ScenePrefabInstanceHandle ScenePrefabInstanceRegistry::FindContainingInstance(SceneObject object, std::uint32_t& nodeIndex) const noexcept {
    nodeIndex = 0;
    if (!object.IsValid()) {
        return {};
    }

    for (const auto& [id, record] : records_) {
        for (std::uint32_t index = 0; index < static_cast<std::uint32_t>(record.objects.size()); ++index) {
            if (record.objects[index].Entity() == object.Entity()) {
                nodeIndex = index;
                return ScenePrefabInstanceHandle{ id };
            }
        }
    }
    return {};
}

std::vector<ScenePrefabInstanceHandle> ScenePrefabInstanceRegistry::Handles() const {
    std::vector<ScenePrefabInstanceHandle> handles;
    handles.reserve(records_.size());
    for (const auto& [id, record] : records_) {
        static_cast<void>(record);
        handles.push_back(ScenePrefabInstanceHandle{ id });
    }
    return handles;
}

std::vector<ScenePrefabInstanceHandle> ScenePrefabInstanceRegistry::HandlesForPrefab(ScenePrefabHandle prefab) const {
    std::vector<ScenePrefabInstanceHandle> handles;
    if (!prefab.IsValid()) {
        return handles;
    }

    for (const auto& [id, record] : records_) {
        if (record.prefab == prefab) {
            handles.push_back(ScenePrefabInstanceHandle{ id });
        }
    }
    return handles;
}

std::size_t ScenePrefabInstanceRegistry::Count() const noexcept {
    return records_.size();
}

bool ScenePrefabInstanceRegistry::Remove(ScenePrefabInstanceHandle handle) noexcept {
    if (!handle.IsValid()) {
        return false;
    }
    return records_.erase(handle.id_) > 0U;
}

void ScenePrefabInstanceRegistry::Clear() noexcept {
    records_.clear();
    nextId_ = 1;
}

} // namespace kb::scene
