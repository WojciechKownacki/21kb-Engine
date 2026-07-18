#pragma once

#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabOverrides.hpp"
#include "scene/prefab/ScenePrefabRecord.hpp"
#include "scene/prefab/io/ScenePrefabAssetFormat.hpp"

#include <filesystem>
#include <string_view>
#include <vector>

namespace kb::scene {

struct ScenePrefabAssetWriteDesc {
    ScenePrefabAssetKind kind = ScenePrefabAssetKind::Template;
    std::string_view guid;
    std::string_view name;
    std::string_view baseGuid;
    const ScenePrefab* prefab = nullptr;
    const std::vector<ScenePrefabPropertyOverride>* overrides = nullptr;
    // LIB-092: a variant's added-child subtrees, serialized after the property
    // overrides so a saved variant reproduces its added children on reload.
    const std::vector<ScenePrefabVariantAddedSubtree>* addedChildren = nullptr;
};

class ScenePrefabAssetWriter {
public:
    ScenePrefabAssetWriter() = delete;

    [[nodiscard]] static bool Write(const std::filesystem::path& path, std::string_view name, const ScenePrefab& prefab);
    [[nodiscard]] static bool Write(const std::filesystem::path& path, const ScenePrefabAssetWriteDesc& asset);
};

} // namespace kb::scene
