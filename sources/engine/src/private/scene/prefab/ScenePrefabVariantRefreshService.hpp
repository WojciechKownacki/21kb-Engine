#pragma once

#include "engine/scene/ScenePrefabHandle.hpp"
#include "scene/prefab/ScenePrefabRecord.hpp"
#include "scene/prefab/ScenePrefabRecordStore.hpp"

namespace kb::scene {

class ScenePrefabVariantRefreshService {
public:
    ScenePrefabVariantRefreshService() = delete;

    [[nodiscard]] static bool Materialize(const ScenePrefabRecordStore& records, ScenePrefabRecord& record);
    static void RefreshDerived(ScenePrefabRecordStore& records, ScenePrefabHandle baseHandle);
};

} // namespace kb::scene
