#include "scene/prefab/ScenePrefabVariantRefreshService.hpp"

#include "scene/prefab/ScenePrefabHasher.hpp"
#include "scene/prefab/ScenePrefabVariantMaterializer.hpp"

namespace kb::scene {

bool ScenePrefabVariantRefreshService::Materialize(const ScenePrefabRecordStore& records, ScenePrefabRecord& record) {
    const ScenePrefabRecord* base = records.Find(record.basePrefab);
    if (base == nullptr) {
        return false;
    }

    return ScenePrefabVariantMaterializer::Materialize(base->prefab, record.variantOverrides, record.prefab);
}

void ScenePrefabVariantRefreshService::RefreshDerived(ScenePrefabRecordStore& records, ScenePrefabHandle baseHandle) {
    const std::vector<ScenePrefabHandle> children = records.VariantChildrenOf(baseHandle);
    for (const ScenePrefabHandle child : children) {
        ScenePrefabRecord* record = records.FindMutable(child);
        if (record != nullptr && Materialize(records, *record)) {
            record->contentHash = ScenePrefabHasher::Hash(record->prefab);
            RefreshDerived(records, child);
        }
    }
}

} // namespace kb::scene
