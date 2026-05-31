#include "scene/prefab/ScenePrefabRegistry.hpp"

#include "scene/prefab/ScenePrefabHasher.hpp"
#include "scene/prefab/ScenePrefabValidator.hpp"

#include <utility>

namespace kb::scene {

ScenePrefabHandle ScenePrefabRegistry::Register(std::string name, ScenePrefab prefab) {
    if (!ScenePrefabValidator::IsValid(prefab) || prefab.Empty()) {
        return {};
    }

    const std::uint64_t contentHash = ScenePrefabHasher::Hash(prefab);
    const std::uint64_t id = nextId_++;
    records_.emplace(
        id,
        ScenePrefabRecord{
            .name = std::move(name),
            .prefab = std::move(prefab),
            .contentHash = contentHash,
        });
    return ScenePrefabHandle{ id };
}

bool ScenePrefabRegistry::Contains(ScenePrefabHandle handle) const noexcept {
    return handle.IsValid() && records_.contains(handle.id_);
}

const ScenePrefabRecord* ScenePrefabRegistry::FindRecord(ScenePrefabHandle handle) const noexcept {
    if (!handle.IsValid()) {
        return nullptr;
    }

    const auto iterator = records_.find(handle.id_);
    return iterator == records_.end() ? nullptr : &iterator->second;
}

const ScenePrefab* ScenePrefabRegistry::Find(ScenePrefabHandle handle) const noexcept {
    const ScenePrefabRecord* record = FindRecord(handle);
    return record == nullptr ? nullptr : &record->prefab;
}

ScenePrefab* ScenePrefabRegistry::FindMutable(ScenePrefabHandle handle) noexcept {
    if (!handle.IsValid()) {
        return nullptr;
    }

    const auto iterator = records_.find(handle.id_);
    return iterator == records_.end() ? nullptr : &iterator->second.prefab;
}

void ScenePrefabRegistry::RefreshContentHash(ScenePrefabHandle handle) noexcept {
    if (!handle.IsValid()) {
        return;
    }

    const auto iterator = records_.find(handle.id_);
    if (iterator != records_.end()) {
        iterator->second.contentHash = ScenePrefabHasher::Hash(iterator->second.prefab);
    }
}

std::size_t ScenePrefabRegistry::Count() const noexcept {
    return records_.size();
}

void ScenePrefabRegistry::Clear() noexcept {
    records_.clear();
    nextId_ = 1;
}

} // namespace kb::scene
