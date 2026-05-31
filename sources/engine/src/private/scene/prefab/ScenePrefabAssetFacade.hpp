#pragma once

#include "engine/scene/ScenePrefabCaptureSettings.hpp"
#include "engine/scene/ScenePrefabHandle.hpp"
#include "engine/scene/SceneObject.hpp"

#include <filesystem>
#include <string>

namespace kb::scene {

class Scene;

class ScenePrefabAssetFacade {
public:
    ScenePrefabAssetFacade() = delete;

    [[nodiscard]] static ScenePrefabHandle Create(Scene& scene, SceneObject root, const ScenePrefabCaptureSettings& settings, std::string name, const std::filesystem::path& path);
    [[nodiscard]] static bool Save(Scene& scene, ScenePrefabHandle handle, const std::filesystem::path& path);
    [[nodiscard]] static ScenePrefabHandle Load(Scene& scene, const std::filesystem::path& path);
};

} // namespace kb::scene
