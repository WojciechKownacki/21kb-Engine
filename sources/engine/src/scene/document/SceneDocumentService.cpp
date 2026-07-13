#include "engine/scene/SceneDocumentService.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "scene/document/SceneDocumentCaptureService.hpp"
#include "scene/asset/io/SceneAssetFormat.hpp"
#include "scene/asset/io/SceneAssetReader.hpp"
#include "scene/asset/io/SceneAssetWriter.hpp"

#include <utility>

namespace kb::scene {
namespace {

void ClearSceneRoots(Scene& scene) noexcept {
    const std::vector<SceneEntity> roots = scene.Hierarchy().RootEntities();
    for (const SceneEntity root : roots) {
        scene.Entities().Destroy(root);
    }
}

} // namespace

SceneDocument SceneDocumentService::Capture(Scene& scene, std::string name) {
    return SceneDocumentCaptureService::Capture(scene, std::move(name));
}

bool SceneDocumentService::Save(Scene& scene, const std::filesystem::path& path, std::string name) {
    if (path.extension() != SceneAssetFormat::Extension) {
        return false;
    }

    SceneDocument captured = Capture(scene, std::move(name));
    captured.guid = "scene:" + captured.name;
    return SceneAssetWriter::Write(path, captured);
}

bool SceneDocumentService::Save(const SceneDocument& document, const std::filesystem::path& path) {
    if (path.extension() != SceneAssetFormat::Extension) {
        return false;
    }
    return SceneAssetWriter::Write(path, document);
}

SceneDocumentLoadResult SceneDocumentService::Load(const std::filesystem::path& path) {
    if (path.extension() != SceneAssetFormat::Extension) {
        return SceneDocumentLoadResult{ .succeeded = false, .document = {}, .error = "Scene asset extension is not supported." };
    }
    return SceneAssetReader::Read(path);
}

bool SceneDocumentService::LoadIntoScene(Scene& scene, const SceneDocument& document) {
    ClearSceneRoots(scene);
    if (!document.worldPrefab.Empty()) {
        const ScenePrefabInstance instance = scene.Prefabs().Instantiate(document.worldPrefab);
        if (instance.Empty()) {
            return false;
        }
    }
    scene.Runtime().SynchronizeTransforms();
    return true;
}

bool SceneDocumentService::LoadFileIntoScene(Scene& scene, const std::filesystem::path& path) {
    SceneDocumentLoadResult loaded = Load(path);
    return loaded.succeeded && LoadIntoScene(scene, loaded.document);
}

SceneDocumentAdditiveLoadResult SceneDocumentService::LoadIntoSceneAdditive(Scene& scene, const SceneDocument& document) {
    if (document.worldPrefab.Empty()) {
        return SceneDocumentAdditiveLoadResult{ .succeeded = false, .root = {} };
    }
    const ScenePrefabInstance instance = scene.Prefabs().Instantiate(document.worldPrefab);
    if (instance.Empty()) {
        return SceneDocumentAdditiveLoadResult{ .succeeded = false, .root = {} };
    }
    scene.Runtime().SynchronizeTransforms();
    return SceneDocumentAdditiveLoadResult{ .succeeded = true, .root = instance.ObjectAt(0).Entity() };
}

} // namespace kb::scene
