#pragma once

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <filesystem>
#include <optional>

namespace kb::editor {

class EditorScenePrefabActions {
public:
    EditorScenePrefabActions() = delete;

    [[nodiscard]] static bool CreateAsset(kb::scene::Scene& scene, kb::scene::SceneEntity entity, const std::filesystem::path& path);
    [[nodiscard]] static std::optional<kb::scene::SceneEntity> InstantiateAsset(kb::scene::Scene& scene, const std::filesystem::path& path, kb::scene::SceneEntity parent);
    [[nodiscard]] static std::optional<kb::scene::SceneEntity> InstantiateAsset(kb::scene::Scene& scene, const std::filesystem::path& path, const std::filesystem::path& virtualPath, kb::scene::SceneEntity parent);
};

} // namespace kb::editor
