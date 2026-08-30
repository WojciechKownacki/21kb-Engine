#include "scene/prefab/ScenePrefabRegistrationService.hpp"

#include "scene/prefab/ScenePrefabBakedData.hpp"
#include "scene/prefab/ScenePrefabHasher.hpp"
#include "scene/prefab/ScenePrefabRecordFactory.hpp"
#include "scene/prefab/ScenePrefabVariantOverrideList.hpp"
#include "scene/prefab/ScenePrefabVariantRefreshService.hpp"

#include <optional>
#include <utility>

namespace kb::scene {
namespace {

void RefreshBakedPrefabCache(ScenePrefabRecord& record) {
    if (record.prefab.Empty()) {
        record.bakedPrefab = ScenePrefabBakedData{};
        record.bakedContentHash = 0;
        return;
    }
    record.bakedPrefab = ScenePrefabBakedData::Bake(record.prefab.Nodes());
    record.bakedContentHash = record.contentHash;
}

} // namespace

ScenePrefabHandle ScenePrefabRegistrationService::Register(ScenePrefabRecordStore& records, std::string name, ScenePrefab prefab) {
    std::optional<ScenePrefabRecord> record = ScenePrefabRecordFactory::CreateTemplate(std::move(name), std::move(prefab), records.NextId());
    return record.has_value() ? records.Insert(std::move(*record)) : ScenePrefabHandle{};
}

ScenePrefabHandle ScenePrefabRegistrationService::RegisterLoaded(ScenePrefabRecordStore& records, std::string guid, std::string name, ScenePrefab prefab, std::string sourcePath) {
    if (const ScenePrefabHandle existing = records.FindByGuid(guid); existing.IsValid()) {
        const ScenePrefabRecord* record = records.Find(existing);
        if (record != nullptr && record->contentHash == ScenePrefabHasher::Hash(prefab)) {
            return existing;
        }

        // A guid is an identity, and copying a ".kbprefab" copies its guid, so two
        // files can end up declaring one. Whichever of them were to keep it would
        // be decided by load order alone, while asset dependency discovery sees
        // both files at once and can only ever pick by a rule of its own - the two
        // answers would differ and a cooked build would package one prefab and
        // instantiate the other. A contested guid therefore names nothing: the
        // incumbent is moved off it here and the newcomer below never takes it, so
        // the reference fails to resolve, identically and on both sides, until the
        // duplicate file is given an identity of its own.
        if (record != nullptr && !sourcePath.empty() && !record->sourcePath.empty() && record->sourcePath != sourcePath) {
            static_cast<void>(records.RetireGuid(existing));
        }

        std::optional<ScenePrefabRecord> uniqueRecord = ScenePrefabRecordFactory::CreateTemplate(std::move(name), std::move(prefab), records.NextId());
        if (!uniqueRecord.has_value()) {
            return {};
        }
        uniqueRecord->sourcePath = std::move(sourcePath);
        return records.Insert(std::move(*uniqueRecord));
    }

    std::optional<ScenePrefabRecord> record = ScenePrefabRecordFactory::CreateLoadedTemplate(std::move(guid), std::move(name), std::move(prefab));
    if (!record.has_value()) {
        return {};
    }
    record->sourcePath = std::move(sourcePath);
    return records.Insert(std::move(*record));
}

ScenePrefabHandle ScenePrefabRegistrationService::RegisterVariant(ScenePrefabRecordStore& records, std::string name, ScenePrefabHandle basePrefab, std::vector<ScenePrefabPropertyOverride> overrides) {
    const ScenePrefabRecord* base = records.Find(basePrefab);
    if (base == nullptr) {
        return {};
    }

    // LIB-161: canonicalize the override list at registration (last-write-
    // wins per (nodeId|nodeIndex, propertyPath), empty paths dropped) so the
    // stored variant layer matches the instance-apply Upsert rule and never
    // carries a duplicate that a later single-property Upsert would leave
    // stale.
    ScenePrefabRecord record = ScenePrefabRecordFactory::CreateVariant(std::move(name), basePrefab, *base, records.NextId(), ScenePrefabVariantOverrideList::Normalize(std::move(overrides)));
    if (!ScenePrefabVariantRefreshService::Materialize(records, record)) {
        return {};
    }

    record.contentHash = ScenePrefabHasher::Hash(record.prefab);
    RefreshBakedPrefabCache(record);
    return records.Insert(std::move(record));
}

ScenePrefabHandle ScenePrefabRegistrationService::RegisterLoadedVariant(ScenePrefabRecordStore& records, std::string guid, std::string name, std::string basePrefabGuid, std::vector<ScenePrefabPropertyOverride> overrides, std::vector<ScenePrefabVariantAddedSubtree> addedChildren) {
    if (guid.empty() || basePrefabGuid.empty() || records.FindByGuid(guid).IsValid()) {
        return {};
    }

    const ScenePrefabHandle baseHandle = records.FindByGuid(basePrefabGuid);
    if (!baseHandle.IsValid()) {
        return {};
    }

    ScenePrefabRecord record = ScenePrefabRecordFactory::CreateLoadedVariant(std::move(guid), std::move(name), baseHandle, std::move(basePrefabGuid), ScenePrefabVariantOverrideList::Normalize(std::move(overrides)));
    // LIB-092: the added-child subtrees must be on the record BEFORE
    // materialization — the materializer appends them as real nodes, so a
    // loaded variant reproduces its added children the same way an in-memory
    // ApplyOverrides-built one does.
    record.variantAddedChildren = std::move(addedChildren);
    if (!ScenePrefabVariantRefreshService::Materialize(records, record)) {
        return {};
    }

    record.contentHash = ScenePrefabHasher::Hash(record.prefab);
    RefreshBakedPrefabCache(record);
    return records.Insert(std::move(record));
}

} // namespace kb::scene
