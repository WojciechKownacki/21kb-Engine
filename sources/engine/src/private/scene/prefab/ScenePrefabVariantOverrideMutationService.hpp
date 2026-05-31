#pragma once

#include "engine/scene/ScenePrefabHandle.hpp"
#include "engine/scene/ScenePrefabOverrides.hpp"

namespace kb::scene {

class ScenePrefabRecordStore;

class ScenePrefabVariantOverrideMutationService {
public:
    ScenePrefabVariantOverrideMutationService() = delete;

    [[nodiscard]] static bool Upsert(ScenePrefabRecordStore& records, ScenePrefabHandle handle, ScenePrefabPropertyOverride property);
};

} // namespace kb::scene
