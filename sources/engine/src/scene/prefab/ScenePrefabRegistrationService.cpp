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

ScenePrefabHandle ScenePrefabRegistrationService::RegisterLoaded(ScenePrefabRecordStore& records, std::string guid, std::string name, ScenePrefab prefab) {
    if (const ScenePrefabHandle existing = records.FindByGuid(guid); existing.IsValid()) {
        const ScenePrefabRecord* record = records.Find(existing);
        if (record != nullptr && record->contentHash == ScenePrefabHasher::Hash(prefab)) {
            return existing;
        }

        std::optional<ScenePrefabRecord> uniqueRecord = ScenePrefabRecordFactory::CreateTemplate(std::move(name), std::move(prefab), records.NextId());
        return uniqueRecord.has_value() ? records.Insert(std::move(*uniqueRecord)) : ScenePrefabHandle{};
    }

    std::optional<ScenePrefabRecord> record = ScenePrefabRecordFactory::CreateLoadedTemplate(std::move(guid), std::move(name), std::move(prefab));
    return record.has_value() ? records.Insert(std::move(*record)) : ScenePrefabHandle{};
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

ScenePrefabHandle ScenePrefabRegistrationService::RegisterLoadedVariant(ScenePrefabRecordStore& records, std::string guid, std::string name, std::string basePrefabGuid, std::vector<ScenePrefabPropertyOverride> overrides) {
    if (guid.empty() || basePrefabGuid.empty() || records.FindByGuid(guid).IsValid()) {
        return {};
    }

    const ScenePrefabHandle baseHandle = records.FindByGuid(basePrefabGuid);
    if (!baseHandle.IsValid()) {
        return {};
    }

    ScenePrefabRecord record = ScenePrefabRecordFactory::CreateLoadedVariant(std::move(guid), std::move(name), baseHandle, std::move(basePrefabGuid), ScenePrefabVariantOverrideList::Normalize(std::move(overrides)));
    if (!ScenePrefabVariantRefreshService::Materialize(records, record)) {
        return {};
    }

    record.contentHash = ScenePrefabHasher::Hash(record.prefab);
    RefreshBakedPrefabCache(record);
    return records.Insert(std::move(record));
}

} // namespace kb::scene
