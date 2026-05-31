#pragma once

#include "engine/scene/ScenePrefab.hpp"

#include <filesystem>
#include <string_view>

namespace kb::scene {

class ScenePrefabAssetWriter {
public:
    ScenePrefabAssetWriter() = delete;

    [[nodiscard]] static bool Write(const std::filesystem::path& path, std::string_view name, const ScenePrefab& prefab);
};

} // namespace kb::scene
