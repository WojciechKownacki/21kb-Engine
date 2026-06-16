#include "scene/prefab/ScenePrefabAssetFacade.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabCaptureFacade.hpp"
#include "scene/prefab/ScenePrefabChildrenReader.hpp"
#include "scene/prefab/ScenePrefabNestedResolver.hpp"
#include "scene/prefab/ScenePrefabRecord.hpp"
#include "scene/prefab/io/ScenePrefabAssetService.hpp"

#include <utility>
#include <vector>

namespace kb::scene {
namespace {

void AppendCapturedObjects(SceneObject object, const ScenePrefabCaptureSettings& settings, std::vector<SceneObject>& objects) {
    objects.push_back(object);
    for (const SceneObject child : ScenePrefabChildrenReader::Read(object, settings)) {
        AppendCapturedObjects(child, settings, objects);
    }
}

[[nodiscard]] std::vector<SceneObject> CapturedObjects(SceneObject root, const ScenePrefabCaptureSettings& settings) {
    std::vector<SceneObject> objects;
    AppendCapturedObjects(root, settings, objects);
    return objects;
}

void RegisterCreatedAssetInstance(Scene& scene, SceneObject root, const ScenePrefabCaptureSettings& settings, ScenePrefabHandle handle) {
    if (!handle.IsValid() || !root.IsValid()) {
        return;
    }

    SceneState& state = SceneAccess::State(scene);
    const ScenePrefabRecord* record = state.prefabs.FindRecord(handle);
    if (record == nullptr) {
        return;
    }

    std::vector<SceneObject> objects = CapturedObjects(root, settings);
    if (objects.empty()) {
        return;
    }

    ScenePrefab resolvedPrefab = ScenePrefabNestedResolver::Resolve(state.prefabs, record->prefab);
    static_cast<void>(state.prefabInstances.Register(handle, record->guid, root.Parent(), std::move(objects), std::move(resolvedPrefab)));
}

} // namespace

ScenePrefabHandle ScenePrefabAssetFacade::Create(Scene& scene, SceneObject root, const ScenePrefabCaptureSettings& settings, std::string name, const std::filesystem::path& path) {
    ScenePrefabHandle handle = ScenePrefabCaptureFacade::CaptureRegistered(scene, root, settings, std::move(name));
    if (!handle.IsValid() || !Save(scene, handle, path)) {
        return {};
    }
    RegisterCreatedAssetInstance(scene, root, settings, handle);
    return handle;
}

bool ScenePrefabAssetFacade::Save(Scene& scene, ScenePrefabHandle handle, const std::filesystem::path& path) {
    return ScenePrefabAssetService::Save(scene, handle, path);
}

ScenePrefabHandle ScenePrefabAssetFacade::Load(Scene& scene, const std::filesystem::path& path) {
    return ScenePrefabAssetService::Load(scene, path);
}

} // namespace kb::scene
