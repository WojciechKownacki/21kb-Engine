#include "scene/prefab/ScenePrefabRegistry.hpp"

#include "scene/prefab/ScenePrefabHasher.hpp"
#include "scene/prefab/ScenePrefabRegistrationService.hpp"
#include "scene/prefab/ScenePrefabVariantOverrideMutationService.hpp"
#include "scene/prefab/ScenePrefabVariantRefreshService.hpp"

#include <string_view>
#include <utility>

namespace kb::scene {

ScenePrefabHandle ScenePrefabRegistry::Register(std::string name, ScenePrefab prefab) {
    return ScenePrefabRegistrationService::Register(records_, std::move(name), std::move(prefab));
}

ScenePrefabHandle ScenePrefabRegistry::RegisterLoaded(std::string guid, std::string name, ScenePrefab prefab) {
    return ScenePrefabRegistrationService::RegisterLoaded(records_, std::move(guid), std::move(name), std::move(prefab));
}

ScenePrefabHandle ScenePrefabRegistry::RegisterVariant(std::string name, ScenePrefabHandle basePrefab, std::vector<ScenePrefabPropertyOverride> overrides) {
    return ScenePrefabRegistrationService::RegisterVariant(records_, std::move(name), basePrefab, std::move(overrides));
}

ScenePrefabHandle ScenePrefabRegistry::RegisterLoadedVariant(std::string guid, std::string name, std::string basePrefabGuid, std::vector<ScenePrefabPropertyOverride> overrides) {
    return ScenePrefabRegistrationService::RegisterLoadedVariant(records_, std::move(guid), std::move(name), std::move(basePrefabGuid), std::move(overrides));
}

bool ScenePrefabRegistry::Contains(ScenePrefabHandle handle) const noexcept {
    return records_.Contains(handle);
}

const ScenePrefabRecord* ScenePrefabRegistry::FindRecord(ScenePrefabHandle handle) const noexcept {
    return records_.Find(handle);
}

ScenePrefabRecord* ScenePrefabRegistry::FindMutableRecord(ScenePrefabHandle handle) noexcept {
    return records_.FindMutable(handle);
}

const ScenePrefab* ScenePrefabRegistry::Find(ScenePrefabHandle handle) const noexcept {
    const ScenePrefabRecord* record = FindRecord(handle);
    return record == nullptr ? nullptr : &record->prefab;
}

ScenePrefab* ScenePrefabRegistry::FindMutable(ScenePrefabHandle handle) noexcept {
    ScenePrefabRecord* record = records_.FindMutable(handle);
    return record == nullptr ? nullptr : &record->prefab;
}

ScenePrefabHandle ScenePrefabRegistry::FindByGuid(std::string_view guid) const noexcept {
    return records_.FindByGuid(guid);
}

std::vector<ScenePrefabHandle> ScenePrefabRegistry::VariantChildrenOf(ScenePrefabHandle baseHandle) const {
    return records_.VariantChildrenOf(baseHandle);
}

bool ScenePrefabRegistry::UpsertVariantOverride(ScenePrefabHandle handle, ScenePrefabPropertyOverride property) {
    return ScenePrefabVariantOverrideMutationService::Upsert(records_, handle, std::move(property));
}

void ScenePrefabRegistry::RefreshContentHash(ScenePrefabHandle handle) noexcept {
    if (!handle.IsValid()) {
        return;
    }

    ScenePrefabRecord* record = records_.FindMutable(handle);
    if (record != nullptr) {
        record->contentHash = ScenePrefabHasher::Hash(record->prefab);
    }
}

void ScenePrefabRegistry::RefreshDerivedPrefabs(ScenePrefabHandle baseHandle) {
    ScenePrefabVariantRefreshService::RefreshDerived(records_, baseHandle);
}

std::size_t ScenePrefabRegistry::Count() const noexcept {
    return records_.Count();
}

void ScenePrefabRegistry::Clear() noexcept {
    records_.Clear();
}

} // namespace kb::scene
