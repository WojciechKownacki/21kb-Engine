#include "scene/document/SceneDocumentCaptureService.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAudioMixerAccess.hpp"
#include "engine/scene/SceneAudioOcclusionAccess.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneTagCatalog.hpp"
#include "scene/prefab/ScenePrefabCaptureService.hpp"

#include <utility>

namespace kb::scene {
namespace {

[[nodiscard]] std::string SceneGuid(std::string_view name) {
    return "scene:" + std::string{ name };
}

} // namespace

SceneDocument SceneDocumentCaptureService::Capture(Scene& scene, std::string name) {
    SceneDocument document{
        .fileVersion = SceneDocument::CurrentFileVersion,
        .guid = SceneGuid(name),
        .name = std::move(name),
        .worldType = "Editor",
        .worldPrefab = {},
    };

    const std::vector<SceneObject> roots = scene.Hierarchy().RootObjects();
    document.tagDefinitions.assign(scene.Tags().Names().begin(), scene.Tags().Names().end());
    document.audioMixerAssetId = SceneAudioMixerAccess::ActiveMixer(scene);
    document.audioMixerSnapshot = SceneAudioMixerAccess::ActiveSnapshot(scene);
    document.audioOcclusionSettings = SceneAudioOcclusionAccess::Settings(scene);
    document.worldPrefab = ScenePrefabCaptureService::CaptureRoots(scene, std::span<const SceneObject>{ roots }, ScenePrefabCaptureSettings{});
    return document;
}

} // namespace kb::scene
