#include "scene/prefab/ScenePrefabAssetFacade.hpp"

#include "scene/prefab/ScenePrefabCaptureFacade.hpp"
#include "scene/prefab/io/ScenePrefabAssetService.hpp"

#include <utility>

namespace kb::scene {

ScenePrefabHandle ScenePrefabAssetFacade::Create(Scene& scene, SceneObject root, const ScenePrefabCaptureSettings& settings, std::string name, const std::filesystem::path& path) {
    ScenePrefabHandle handle = ScenePrefabCaptureFacade::CaptureRegistered(scene, root, settings, std::move(name));
    if (!handle.IsValid() || !Save(scene, handle, path)) {
        return {};
    }
    return handle;
}

bool ScenePrefabAssetFacade::Save(Scene& scene, ScenePrefabHandle handle, const std::filesystem::path& path) {
    return ScenePrefabAssetService::Save(scene, handle, path);
}

ScenePrefabHandle ScenePrefabAssetFacade::Load(Scene& scene, const std::filesystem::path& path) {
    return ScenePrefabAssetService::Load(scene, path);
}

} // namespace kb::scene
