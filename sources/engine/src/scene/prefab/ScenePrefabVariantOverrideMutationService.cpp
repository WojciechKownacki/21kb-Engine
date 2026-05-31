#include "scene/prefab/ScenePrefabVariantOverrideMutationService.hpp"

#include "scene/prefab/ScenePrefabHasher.hpp"
#include "scene/prefab/ScenePrefabRecord.hpp"
#include "scene/prefab/ScenePrefabRecordStore.hpp"
#include "scene/prefab/ScenePrefabVariantOverrideList.hpp"
#include "scene/prefab/ScenePrefabVariantRefreshService.hpp"

#include <utility>
#include <vector>

namespace kb::scene {

bool ScenePrefabVariantOverrideMutationService::Upsert(ScenePrefabRecordStore& records, ScenePrefabHandle handle, ScenePrefabPropertyOverride property) {
    ScenePrefabRecord* record = records.FindMutable(handle);
    if (record == nullptr || record->kind != ScenePrefabRecordKind::Variant || property.propertyPath.empty()) {
        return false;
    }

    std::vector<ScenePrefabPropertyOverride> updatedOverrides;
    if (!ScenePrefabVariantOverrideList::Upsert(record->variantOverrides, std::move(property), updatedOverrides)) {
        return false;
    }

    ScenePrefabRecord candidate = *record;
    candidate.variantOverrides = std::move(updatedOverrides);
    if (!ScenePrefabVariantRefreshService::Materialize(records, candidate)) {
        return false;
    }

    record->variantOverrides = std::move(candidate.variantOverrides);
    record->prefab = std::move(candidate.prefab);
    record->contentHash = ScenePrefabHasher::Hash(record->prefab);
    ScenePrefabVariantRefreshService::RefreshDerived(records, handle);
    return true;
}

} // namespace kb::scene
