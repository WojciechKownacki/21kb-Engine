#pragma once

#include "scene/prefab/ScenePrefabRecord.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kb::scene {

class ScenePrefabRecordFactory {
public:
    ScenePrefabRecordFactory() = delete;

    [[nodiscard]] static std::optional<ScenePrefabRecord> CreateTemplate(std::string name, ScenePrefab prefab, std::uint64_t localId);
    [[nodiscard]] static std::optional<ScenePrefabRecord> CreateLoadedTemplate(std::string guid, std::string name, ScenePrefab prefab);
    [[nodiscard]] static ScenePrefabRecord CreateVariant(std::string name, ScenePrefabHandle basePrefab, const ScenePrefabRecord& baseRecord, std::uint64_t localId, std::vector<ScenePrefabPropertyOverride> overrides);
    [[nodiscard]] static ScenePrefabRecord CreateLoadedVariant(std::string guid, std::string name, ScenePrefabHandle basePrefab, std::string basePrefabGuid, std::vector<ScenePrefabPropertyOverride> overrides);
};

} // namespace kb::scene
