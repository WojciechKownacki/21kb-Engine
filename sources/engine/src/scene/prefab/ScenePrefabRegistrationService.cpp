#include "scene/prefab/ScenePrefabRegistrationService.hpp"

#include "scene/prefab/ScenePrefabHasher.hpp"
#include "scene/prefab/ScenePrefabRecordFactory.hpp"
#include "scene/prefab/ScenePrefabVariantRefreshService.hpp"

#include <optional>
#include <utility>

namespace kb::scene {

ScenePrefabHandle ScenePrefabRegistrationService::Register(ScenePrefabRecordStore& records, std::string name, ScenePrefab prefab) {
    std::optional<ScenePrefabRecord> record = ScenePrefabRecordFactory::CreateTemplate(std::move(name), std::move(prefab), records.NextId());
    return record.has_value() ? records.Insert(std::move(*record)) : ScenePrefabHandle{};
}

ScenePrefabHandle ScenePrefabRegistrationService::RegisterLoaded(ScenePrefabRecordStore& records, std::string guid, std::string name, ScenePrefab prefab) {
    if (records.FindByGuid(guid).IsValid()) {
        return {};
    }

    std::optional<ScenePrefabRecord> record = ScenePrefabRecordFactory::CreateLoadedTemplate(std::move(guid), std::move(name), std::move(prefab));
    return record.has_value() ? records.Insert(std::move(*record)) : ScenePrefabHandle{};
}

ScenePrefabHandle ScenePrefabRegistrationService::RegisterVariant(ScenePrefabRecordStore& records, std::string name, ScenePrefabHandle basePrefab, std::vector<ScenePrefabPropertyOverride> overrides) {
    const ScenePrefabRecord* base = records.Find(basePrefab);
    if (base == nullptr) {
        return {};
    }

    ScenePrefabRecord record = ScenePrefabRecordFactory::CreateVariant(std::move(name), basePrefab, *base, records.NextId(), std::move(overrides));
    if (!ScenePrefabVariantRefreshService::Materialize(records, record)) {
        return {};
    }

    record.contentHash = ScenePrefabHasher::Hash(record.prefab);
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

    ScenePrefabRecord record = ScenePrefabRecordFactory::CreateLoadedVariant(std::move(guid), std::move(name), baseHandle, std::move(basePrefabGuid), std::move(overrides));
    if (!ScenePrefabVariantRefreshService::Materialize(records, record)) {
        return {};
    }

    record.contentHash = ScenePrefabHasher::Hash(record.prefab);
    return records.Insert(std::move(record));
}

} // namespace kb::scene
