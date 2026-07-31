#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneTagCatalog.hpp"

#include "engine/audio/AudioPlayback.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "scene/document/SceneDocumentCaptureService.hpp"
#include "scene/asset/io/SceneAssetFormat.hpp"
#include "scene/asset/io/SceneAssetReader.hpp"
#include "scene/asset/io/SceneAssetWriter.hpp"

#include <utility>

namespace kb::scene {
namespace {

// LIB-072: the persistent/gameplay boundary — a root marked persistent
// (World.SetPersistent) survives a non-additive Scene.Load along with its
// whole hierarchy (Destroy cascades to children, so skipping the root
// alone preserves the entire subtree). See SceneState::persistentEntities'
// comment for why this check is root-only.
void ClearSceneRoots(Scene& scene) noexcept {
    const std::vector<SceneEntity> roots = scene.Hierarchy().RootEntities();
    for (const SceneEntity root : roots) {
        if (scene.Entities().IsPersistent(root)) {
            continue;
        }
        scene.Entities().Destroy(root);
    }
}

void RegisterAssignedTagVisitor(SceneEntity entity, const TransformComponent&, void* rawScene) {
    auto* scene = static_cast<Scene*>(rawScene);
    if (scene != nullptr) {
        scene->Tags().RegisterAssignedTags(entity);
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
    // A non-additive load replaces the gameplay world while reusing the Scene
    // container and its module backends. Stop scene-owned voices before their
    // source entities disappear, then discard marker events produced by the
    // outgoing world. The backend itself remains registered for the new world.
    kb::audio::AudioPlayback::StopAll(scene);
    static_cast<void>(
        kb::audio::AudioPlayback::DrainPendingMarkerEvents(scene));
    ClearSceneRoots(scene);
    if (!scene.Tags().ReplaceDefinitions(document.tagDefinitions)) {
        return false;
    }
    if (!document.worldPrefab.Empty()) {
        const ScenePrefabInstance instance = scene.Prefabs().Instantiate(document.worldPrefab);
        if (instance.Empty()) {
            return false;
        }
    }
    // Old scene files carried only assignment text. Import it once at the load
    // boundary so the runtime catalogue remains the sole author-facing list.
    scene.Transforms().ForEach(&RegisterAssignedTagVisitor, &scene);
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
