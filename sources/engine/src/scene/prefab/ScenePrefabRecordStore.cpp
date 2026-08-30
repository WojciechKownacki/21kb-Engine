#include "scene/prefab/ScenePrefabRecordStore.hpp"

#include "scene/prefab/ScenePrefabGuid.hpp"

#include <algorithm>
#include <utility>

namespace kb::scene {

std::uint64_t ScenePrefabRecordStore::NextId() const noexcept {
    return nextId_;
}

ScenePrefabHandle ScenePrefabRecordStore::Insert(ScenePrefabRecord record) {
    const std::uint64_t id = nextId_++;
    auto [iterator, inserted] = records_.emplace(id, std::move(record));
    const ScenePrefabHandle handle{ id };
    if (inserted) {
        IndexRecord(handle, iterator->second);
    }
    return handle;
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

    const auto iterator = guidIndex_.find(std::string{ guid });
    return iterator == guidIndex_.end() ? ScenePrefabHandle{} : iterator->second;
}

bool ScenePrefabRecordStore::RetireGuid(ScenePrefabHandle handle) {
    if (!handle.IsValid()) {
        return false;
    }

    const auto iterator = records_.find(handle.id_);
    if (iterator == records_.end() || iterator->second.guid.empty()) {
        return false;
    }

    ScenePrefabRecord& record = iterator->second;
    // The record id is unique for the lifetime of the store, so the generated
    // guid differs from every guid this store generated before; the loop only
    // covers the case of a loaded file having declared that exact string.
    std::string replacement = ScenePrefabGuid::Create(record.name, record.prefab, handle.id_);
    for (std::uint64_t attempt = 1U; guidIndex_.contains(replacement); ++attempt) {
        replacement = ScenePrefabGuid::Create(record.name, record.prefab, handle.id_ + attempt);
    }

    const auto guidIterator = guidIndex_.find(record.guid);
    if (guidIterator != guidIndex_.end() && guidIterator->second == handle) {
        guidIndex_.erase(guidIterator);
    }
    record.guid = std::move(replacement);
    guidIndex_[record.guid] = handle;
    return true;
}

std::vector<ScenePrefabHandle> ScenePrefabRecordStore::VariantChildrenOf(ScenePrefabHandle baseHandle) const {
    if (!baseHandle.IsValid()) {
        return {};
    }

    const auto iterator = variantChildrenIndex_.find(baseHandle.id_);
    return iterator == variantChildrenIndex_.end() ? std::vector<ScenePrefabHandle>{} : iterator->second;
}

std::size_t ScenePrefabRecordStore::Count() const noexcept {
    return records_.size();
}

bool ScenePrefabRecordStore::Remove(ScenePrefabHandle handle) noexcept {
    if (!handle.IsValid()) {
        return false;
    }

    const auto iterator = records_.find(handle.id_);
    if (iterator == records_.end()) {
        return false;
    }

    UnindexRecord(handle, iterator->second);
    records_.erase(iterator);
    return true;
}

void ScenePrefabRecordStore::Clear() noexcept {
    records_.clear();
    guidIndex_.clear();
    variantChildrenIndex_.clear();
}

void ScenePrefabRecordStore::IndexRecord(ScenePrefabHandle handle, const ScenePrefabRecord& record) {
    if (!record.guid.empty()) {
        guidIndex_[record.guid] = handle;
    }
    if (record.kind == ScenePrefabRecordKind::Variant && record.basePrefab.IsValid()) {
        variantChildrenIndex_[record.basePrefab.id_].push_back(handle);
    }
}

void ScenePrefabRecordStore::UnindexRecord(ScenePrefabHandle handle, const ScenePrefabRecord& record) noexcept {
    if (!record.guid.empty()) {
        const auto guidIterator = guidIndex_.find(record.guid);
        if (guidIterator != guidIndex_.end() && guidIterator->second == handle) {
            guidIndex_.erase(guidIterator);
        }
    }

    if (record.kind == ScenePrefabRecordKind::Variant && record.basePrefab.IsValid()) {
        const auto childrenIterator = variantChildrenIndex_.find(record.basePrefab.id_);
        if (childrenIterator != variantChildrenIndex_.end()) {
            std::vector<ScenePrefabHandle>& children = childrenIterator->second;
            children.erase(std::remove(children.begin(), children.end(), handle), children.end());
            if (children.empty()) {
                variantChildrenIndex_.erase(childrenIterator);
            }
        }
    }

    variantChildrenIndex_.erase(handle.id_);
    for (auto iterator = variantChildrenIndex_.begin(); iterator != variantChildrenIndex_.end();) {
        std::vector<ScenePrefabHandle>& children = iterator->second;
        children.erase(std::remove(children.begin(), children.end(), handle), children.end());
        if (children.empty()) {
            iterator = variantChildrenIndex_.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

} // namespace kb::scene
