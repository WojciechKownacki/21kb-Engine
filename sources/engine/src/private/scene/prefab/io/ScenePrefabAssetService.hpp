#pragma once

#include "engine/scene/ScenePrefabHandle.hpp"
#include "engine/scene/ScenePrefabInstanceHandle.hpp"

#include <filesystem>

namespace kb::scene {

class Scene;

class ScenePrefabAssetService {
public:
    ScenePrefabAssetService() = delete;

    [[nodiscard]] static bool Save(Scene& scene, ScenePrefabHandle handle, const std::filesystem::path& path);
    [[nodiscard]] static ScenePrefabHandle Load(Scene& scene, const std::filesystem::path& path);
    [[nodiscard]] static bool SaveInstancePrefab(Scene& scene, ScenePrefabInstanceHandle handle, const std::filesystem::path& path);
};

} // namespace kb::scene
