#pragma once

#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabOverrides.hpp"
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
};

class ScenePrefabAssetReader {
public:
    ScenePrefabAssetReader() = delete;

    [[nodiscard]] static bool Read(const std::filesystem::path& path, ScenePrefabAssetReadResult& output);
};

} // namespace kb::scene
