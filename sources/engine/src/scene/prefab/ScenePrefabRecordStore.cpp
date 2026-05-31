#include "scene/prefab/ScenePrefabRecordStore.hpp"

#include <utility>

namespace kb::scene {

std::uint64_t ScenePrefabRecordStore::NextId() const noexcept {
    return nextId_;
}

ScenePrefabHandle ScenePrefabRecordStore::Insert(ScenePrefabRecord record) {
    const std::uint64_t id = nextId_++;
    records_.emplace(id, std::move(record));
    return ScenePrefabHandle{ id };
}

bool ScenePrefabRecordStore::Contains(ScenePrefabHandle handle) const noexcept {
    return handle.IsValid() && records_.contains(handle.id_);
}

const ScenePrefabRecord* ScenePrefabRecordStore::Find(ScenePrefabHandle handle) const noexcept {
    if (!handle.IsValid()) {
        return nullptr;
    }

    const auto iterator = records_.find(handle.id_);
    return iterator == records_.end() ? nullptr : &iterator->second;
}

ScenePrefabRecord* ScenePrefabRecordStore::FindMutable(ScenePrefabHandle handle) noexcept {
    if (!handle.IsValid()) {
        return nullptr;
    }

    const auto iterator = records_.find(handle.id_);
    return iterator == records_.end() ? nullptr : &iterator->second;
}

ScenePrefabHandle ScenePrefabRecordStore::FindByGuid(std::string_view guid) const noexcept {
    if (guid.empty()) {
        return {};
    }

    for (const auto& [id, record] : records_) {
        if (record.guid == guid) {
            return ScenePrefabHandle{ id };
        }
    }
    return {};
}

std::vector<ScenePrefabHandle> ScenePrefabRecordStore::VariantChildrenOf(ScenePrefabHandle baseHandle) const {
    std::vector<ScenePrefabHandle> children;
    for (const auto& [id, record] : records_) {
        if (record.kind == ScenePrefabRecordKind::Variant && record.basePrefab == baseHandle) {
            children.push_back(ScenePrefabHandle{ id });
        }
    }
    return children;
}

std::size_t ScenePrefabRecordStore::Count() const noexcept {
    return records_.size();
}

void ScenePrefabRecordStore::Clear() noexcept {
    records_.clear();
    nextId_ = 1;
}

} // namespace kb::scene
