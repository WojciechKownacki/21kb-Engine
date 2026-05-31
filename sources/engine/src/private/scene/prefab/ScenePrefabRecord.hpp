#pragma once

#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabHandle.hpp"
#include "engine/scene/ScenePrefabOverrides.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace kb::scene {

enum class ScenePrefabRecordKind {
    Template,
    Variant,
};

struct ScenePrefabRecord {
    ScenePrefabRecordKind kind = ScenePrefabRecordKind::Template;
    std::string guid;
    std::string name;
    ScenePrefab prefab;
    ScenePrefabHandle basePrefab{};
    std::string basePrefabGuid;
    std::vector<ScenePrefabPropertyOverride> variantOverrides;
    std::uint64_t contentHash = 0;
};

} // namespace kb::scene
