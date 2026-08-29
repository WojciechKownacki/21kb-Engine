#include "scene/EditorScenePrefabActions.hpp"

#include "engine/assets/AssetMountTable.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/ScenePrefabs.hpp"

#include <system_error>

namespace kb::editor {
namespace {

[[nodiscard]] std::optional<std::filesystem::path> ResolvePrefabPath(kb::scene::Scene& scene, const std::filesystem::path& path, const std::filesystem::path& virtualPath) {
    std::error_code existsError;
    if (!path.empty() && std::filesystem::exists(path, existsError)) {
        return path;
    }

    const kb::assets::AssetMountTable& mounts = scene.Assets().Manager().Mounts();
    if (!virtualPath.empty()) {
        if (const std::optional<std::filesystem::path> resolved = mounts.Resolve(virtualPath)) {
            return *resolved;
        }
    }

    if (!path.empty()) {
        if (const std::optional<std::filesystem::path> resolved = mounts.Resolve(path)) {
            return *resolved;
        }
    }

    return std::nullopt;
}

} // namespace

bool EditorScenePrefabActions::CreateAsset(kb::scene::Scene& scene, kb::scene::SceneEntity entity, const std::filesystem::path& path) {
    kb::scene::SceneObject object = scene.Entities().Object(entity);
    if (!object.IsValid()) {
        return false;
    }

    const kb::scene::ScenePrefabHandle handle = scene.Prefabs().CreateAsset(object, scene.Entities().Name(entity), path);
    return handle.IsValid();
}

std::optional<kb::scene::SceneEntity> EditorScenePrefabActions::InstantiateAsset(kb::scene::Scene& scene, const std::filesystem::path& path, kb::scene::SceneEntity parent) {
    return InstantiateAsset(scene, path, {}, parent);
}

std::optional<kb::scene::SceneEntity> EditorScenePrefabActions::InstantiateAsset(kb::scene::Scene& scene, const std::filesystem::path& path, const std::filesystem::path& virtualPath, kb::scene::SceneEntity parent) {
    std::optional<std::filesystem::path> resolvedPath = ResolvePrefabPath(scene, path, virtualPath);
    if (!resolvedPath.has_value()) {
        // Drag-and-drop already carries a physical and a mounted virtual path.
        // Keep the full registry scan off the interactive path and reserve it
        // for stale external references that genuinely need rediscovery.
        static_cast<void>(scene.Assets().Discover());
        resolvedPath = ResolvePrefabPath(scene, path, virtualPath);
    }
    if (!resolvedPath.has_value()) {
        return std::nullopt;
    }

    const kb::scene::ScenePrefabHandle handle = scene.Prefabs().Load(*resolvedPath);
    if (!handle.IsValid()) {
        return std::nullopt;
    }

    kb::scene::SceneObject parentObject = scene.Entities().Object(parent);
    const kb::scene::ScenePrefabInstance instance = scene.Prefabs().Instantiate(
        handle,
        kb::scene::ScenePrefabInstantiationSettings{ .parent = parentObject });
    if (instance.Empty()) {
        return std::nullopt;
    }
    return instance.RootObject().Entity();
}

} // namespace kb::editor
