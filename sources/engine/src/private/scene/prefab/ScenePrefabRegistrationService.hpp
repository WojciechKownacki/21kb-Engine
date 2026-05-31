#pragma once

#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabHandle.hpp"
#include "engine/scene/ScenePrefabOverrides.hpp"
#include "scene/prefab/ScenePrefabRecordStore.hpp"

#include <string>
#include <vector>

namespace kb::scene {

class ScenePrefabRegistrationService {
public:
    ScenePrefabRegistrationService() = delete;

    [[nodiscard]] static ScenePrefabHandle Register(ScenePrefabRecordStore& records, std::string name, ScenePrefab prefab);
    [[nodiscard]] static ScenePrefabHandle RegisterLoaded(ScenePrefabRecordStore& records, std::string guid, std::string name, ScenePrefab prefab);
    [[nodiscard]] static ScenePrefabHandle RegisterVariant(ScenePrefabRecordStore& records, std::string name, ScenePrefabHandle basePrefab, std::vector<ScenePrefabPropertyOverride> overrides);
    [[nodiscard]] static ScenePrefabHandle RegisterLoadedVariant(ScenePrefabRecordStore& records, std::string guid, std::string name, std::string basePrefabGuid, std::vector<ScenePrefabPropertyOverride> overrides);
};

} // namespace kb::scene
