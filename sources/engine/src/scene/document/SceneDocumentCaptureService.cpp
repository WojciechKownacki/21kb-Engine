#include "scene/document/SceneDocumentCaptureService.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
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
    document.worldPrefab = ScenePrefabCaptureService::CaptureRoots(scene, std::span<const SceneObject>{ roots }, ScenePrefabCaptureSettings{});
    return document;
}

} // namespace kb::scene
