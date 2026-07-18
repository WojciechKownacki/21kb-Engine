#pragma once

#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabOverrides.hpp"
#include "scene/prefab/ScenePrefabRecord.hpp"
#include "scene/prefab/io/ScenePrefabAssetFormat.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace kb::scene {

struct ScenePrefabAssetReadResult {
    ScenePrefabAssetKind kind = ScenePrefabAssetKind::Template;
    std::string guid;
    std::string name;
    std::string baseGuid;
    ScenePrefab prefab;
    std::vector<ScenePrefabPropertyOverride> overrides;
    // LIB-092: added-child subtrees parsed from a variant asset (empty for
    // templates and for variants written before the feature existed).
    std::vector<ScenePrefabVariantAddedSubtree> addedChildren;
    bool missingNodeStableIds = false;
    bool missingOverrideNodeIds = false;
};

class ScenePrefabAssetReader {
public:
    ScenePrefabAssetReader() = delete;

    [[nodiscard]] static bool Read(const std::filesystem::path& path, ScenePrefabAssetReadResult& output);
};

} // namespace kb::scene
