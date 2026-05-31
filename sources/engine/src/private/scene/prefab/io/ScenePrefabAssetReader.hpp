#pragma once

#include "engine/scene/ScenePrefab.hpp"

#include <filesystem>
#include <string>

namespace kb::scene {

struct ScenePrefabAssetReadResult {
    std::string name;
    ScenePrefab prefab;
};

class ScenePrefabAssetReader {
public:
    ScenePrefabAssetReader() = delete;

    [[nodiscard]] static bool Read(const std::filesystem::path& path, ScenePrefabAssetReadResult& output);
};

} // namespace kb::scene
