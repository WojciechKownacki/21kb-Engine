#include "scene/assets/ScenePrefabAssetLoader.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "scene/prefab/io/ScenePrefabAssetService.hpp"

#include <memory>
#include <utility>

namespace kb::scene {

ScenePrefabAssetLoader::ScenePrefabAssetLoader(Scene& scene) noexcept
    : scene_(scene) {}

std::string_view ScenePrefabAssetLoader::Type() const noexcept {
    return "ScenePrefab";
}

std::type_index ScenePrefabAssetLoader::PayloadType() const noexcept {
    return typeid(ScenePrefab);
}

std::vector<std::string> ScenePrefabAssetLoader::Extensions() const {
    return { ".kbprefab" };
}

kb::assets::AssetLoadResult ScenePrefabAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    const ScenePrefabHandle handle = ScenePrefabAssetService::Load(scene_, request.resolvedPath);
    if (!handle.IsValid()) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = "Scene prefab asset load failed" };
    }

    ScenePrefab prefab = scene_.Prefabs().Get(handle);
    if (prefab.Empty()) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = "Scene prefab asset was empty after load" };
    }

    return kb::assets::AssetLoadResult{ .asset = std::make_shared<ScenePrefab>(std::move(prefab)), .error = {} };
}

} // namespace kb::scene
