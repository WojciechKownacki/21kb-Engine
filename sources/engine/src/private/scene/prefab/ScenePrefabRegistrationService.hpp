#pragma once

#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabHandle.hpp"
#include "engine/scene/ScenePrefabOverrides.hpp"
#include "scene/prefab/ScenePrefabRecord.hpp"
#include "scene/prefab/ScenePrefabRecordStore.hpp"

#include <string>
#include <vector>

namespace kb::scene {

class ScenePrefabRegistrationService {
public:
    ScenePrefabRegistrationService() = delete;

    [[nodiscard]] static ScenePrefabHandle Register(ScenePrefabRecordStore& records, std::string name, ScenePrefab prefab);
    // `sourcePath` is the canonical spelling of the asset file the prefab came
    // from; it is what distinguishes "the same file again" from "a second file
    // claiming this guid". Pass an empty string only for a prefab that has no
    // file behind it.
    [[nodiscard]] static ScenePrefabHandle RegisterLoaded(ScenePrefabRecordStore& records, std::string guid, std::string name, ScenePrefab prefab, std::string sourcePath);
    [[nodiscard]] static ScenePrefabHandle RegisterVariant(ScenePrefabRecordStore& records, std::string name, ScenePrefabHandle basePrefab, std::vector<ScenePrefabPropertyOverride> overrides);
    [[nodiscard]] static ScenePrefabHandle RegisterLoadedVariant(ScenePrefabRecordStore& records, std::string guid, std::string name, std::string basePrefabGuid, std::vector<ScenePrefabPropertyOverride> overrides, std::vector<ScenePrefabVariantAddedSubtree> addedChildren = {});
};

} // namespace kb::scene
