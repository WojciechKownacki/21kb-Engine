#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneTagCatalog.hpp"
#include "scene/SceneStreamingService.hpp"

#include "engine/audio/AudioPlayback.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAudioListenerAccess.hpp"
#include "engine/scene/SceneAudioMixerAccess.hpp"
#include "engine/scene/SceneAudioOcclusionAccess.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "scene/document/SceneDocumentCaptureService.hpp"
#include "scene/document/SceneDocumentAudioValidation.hpp"
#include "scene/asset/io/SceneAssetFormat.hpp"
#include "scene/asset/io/SceneAssetReader.hpp"
#include "scene/asset/io/SceneAssetWriter.hpp"

#include <utility>
#include <algorithm>

namespace kb::scene {
namespace {

// LIB-072: the persistent/gameplay boundary — a root marked persistent
// (World.SetPersistent) survives a non-additive Scene.Load along with its
// whole hierarchy (Destroy cascades to children, so skipping the root
// alone preserves the entire subtree). See SceneState::persistentEntities'
// comment for why this check is root-only.
void ClearSceneRoots(Scene& scene) noexcept {
    SceneStreamingService::CancelPending(scene);
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

namespace {

SceneDocumentOwnedLoadResult LoadIntoSceneInternal(Scene& scene, const SceneDocument& document, bool ownRoots) {
    if (!IsSceneDocumentAudioConfigurationValid(document)) {
        return {};
    }
    // A non-additive load replaces the gameplay world while reusing the Scene
    // container and its module backends. Stop scene-owned voices before their
    // source entities disappear, then discard marker events produced by the
    // outgoing world. The backend itself remains registered for the new world.
    kb::audio::AudioPlayback::StopAll(scene);
    static_cast<void>(
        kb::audio::AudioPlayback::DrainPendingMarkerEvents(scene));
    SceneAudioListenerAccess::SetLocalUser(scene, kb::input::kPrimaryLocalUser);
    SceneAudioMixerAccess::SetActiveMixer(scene, document.audioMixerAssetId);
    if (!SceneAudioMixerAccess::SetActiveSnapshot(scene, document.audioMixerSnapshot)) {
        return {};
    }
    SceneAudioMixerAccess::ResetRuntimeMixerState(scene);
    if (!SceneAudioOcclusionAccess::Configure(scene, document.audioOcclusionSettings)) {
        return {};
    }
    SceneAudioOcclusionAccess::PublishRuntimeStats(scene, {});
    ClearSceneRoots(scene);
    if (!scene.Tags().ReplaceDefinitions(document.tagDefinitions)) {
        return {};
    }
    SceneEntity root{};
    if (!document.worldPrefab.Empty()) {
        const auto nodes = document.worldPrefab.Nodes();
        const auto firstRoot = std::find_if(nodes.begin(), nodes.end(), [](const ScenePrefabNodeDesc& node) {
            return node.parentNode == ScenePrefabNodeDesc::NoParent;
        });
        const bool multipleRoots = ownRoots && std::count_if(nodes.begin(), nodes.end(), [](const ScenePrefabNodeDesc& node) {
            return node.parentNode == ScenePrefabNodeDesc::NoParent;
        }) > 1;
        const SceneObject owner = multipleRoots
            ? scene.Entities().CreateObject(SceneObjectDesc{ .name = document.name })
            : SceneObject{};
        if (multipleRoots && !owner.IsValid()) {
            return {};
        }
        if (ownRoots && firstRoot == nodes.end()) {
            return {};
        }
        ScenePrefabInstance instance;
        try {
            instance = scene.Prefabs().Instantiate(
                document.worldPrefab, ScenePrefabInstantiationSettings{ .parent = owner, .linkPrefabInstances = true });
        } catch (...) {
            if (owner.IsValid()) {
                scene.Entities().Destroy(owner);
            }
            throw;
        }
        if (instance.Empty()) {
            if (owner.IsValid()) {
                scene.Entities().Destroy(owner);
            }
            return {};
        }
        if (ownRoots) {
            root = owner.IsValid() ? owner.Entity() :
                instance.ObjectAt(static_cast<std::uint32_t>(firstRoot - nodes.begin())).Entity();
        }
    }
    // Old scene files carried only assignment text. Import it once at the load
    // boundary so the runtime catalogue remains the sole author-facing list.
    scene.Transforms().ForEach(&RegisterAssignedTagVisitor, &scene);
    scene.Runtime().SynchronizeTransforms();
    return SceneDocumentOwnedLoadResult{ .succeeded = true, .root = root };
}

} // namespace

bool SceneDocumentService::LoadIntoScene(Scene& scene, const SceneDocument& document) {
    return LoadIntoSceneInternal(scene, document, false).succeeded;
}

SceneDocumentOwnedLoadResult SceneDocumentService::LoadIntoSceneOwned(Scene& scene, const SceneDocument& document) {
    return LoadIntoSceneInternal(scene, document, true);
}

bool SceneDocumentService::LoadFileIntoScene(Scene& scene, const std::filesystem::path& path) {
    SceneDocumentLoadResult loaded = Load(path);
    return loaded.succeeded && LoadIntoScene(scene, loaded.document);
}

SceneDocumentAdditiveLoadResult SceneDocumentService::LoadIntoSceneAdditive(Scene& scene, const SceneDocument& document) {
    if (document.worldPrefab.Empty()) {
        return SceneDocumentAdditiveLoadResult{ .succeeded = false, .root = {} };
    }
    const auto nodes = document.worldPrefab.Nodes();
    const bool multipleRoots = std::count_if(nodes.begin(), nodes.end(), [](const ScenePrefabNodeDesc& node) {
        return node.parentNode == ScenePrefabNodeDesc::NoParent;
    }) > 1;
    const SceneObject owner = multipleRoots
        ? scene.Entities().CreateObject(SceneObjectDesc{ .name = document.name })
        : SceneObject{};
    try {
        const ScenePrefabInstance instance = scene.Prefabs().Instantiate(document.worldPrefab, ScenePrefabInstantiationSettings{ .parent = owner, .linkPrefabInstances = true });
        if (instance.Empty()) {
            if (owner.IsValid()) {
                scene.Entities().Destroy(owner);
            }
            return SceneDocumentAdditiveLoadResult{ .succeeded = false, .root = {} };
        }
        scene.Runtime().SynchronizeTransforms();
        return SceneDocumentAdditiveLoadResult{ .succeeded = true, .root = owner.IsValid() ? owner.Entity() : instance.ObjectAt(0).Entity() };
    } catch (...) {
        if (owner.IsValid()) {
            scene.Entities().Destroy(owner);
        }
        throw;
    }
}

} // namespace kb::scene
