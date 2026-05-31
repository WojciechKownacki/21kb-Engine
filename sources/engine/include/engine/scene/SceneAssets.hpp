#pragma once

#include "engine/assets/AssetHandle.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/scene/ScenePrefab.hpp"

#include <filesystem>

namespace kb::scene {

class Scene;

class SceneAssets {
public:
    explicit SceneAssets(Scene& scene) noexcept;

    [[nodiscard]] kb::assets::AssetManager& Manager() noexcept;
    [[nodiscard]] const kb::assets::AssetManager& Manager() const noexcept;
    [[nodiscard]] bool MountProject(const std::filesystem::path& projectRoot);
    [[nodiscard]] std::size_t Discover();
    [[nodiscard]] kb::assets::AssetHandle<ScenePrefab> LoadPrefab(const std::filesystem::path& virtualPath);

private:
    Scene& scene_;
};

} // namespace kb::scene
