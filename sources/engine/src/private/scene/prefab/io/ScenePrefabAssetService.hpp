#pragma once

#include "engine/scene/ScenePrefabHandle.hpp"
#include "engine/scene/ScenePrefabInstanceHandle.hpp"

#include <filesystem>
#include <string>

namespace kb::scene {

class Scene;
struct ScenePrefabAssetReadResult;

class ScenePrefabAssetService {
public:
    ScenePrefabAssetService() = delete;

    [[nodiscard]] static bool Save(Scene& scene, ScenePrefabHandle handle, const std::filesystem::path& path);
    [[nodiscard]] static ScenePrefabHandle Load(Scene& scene, const std::filesystem::path& path);
    [[nodiscard]] static ScenePrefabHandle LoadReadOnly(Scene& scene, ScenePrefabAssetReadResult asset, std::string sourceIdentity);
    [[nodiscard]] static bool SaveInstancePrefab(Scene& scene, ScenePrefabInstanceHandle handle, const std::filesystem::path& path);
};

} // namespace kb::scene
