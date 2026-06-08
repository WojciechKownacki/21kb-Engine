#include "scene/document/SceneDocumentCaptureService.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/ScenePrefabs.hpp"

#include <utility>

namespace kb::scene {
namespace {

void AppendPrefab(ScenePrefab& target, const ScenePrefab& source) {
    const std::uint32_t nodeOffset = static_cast<std::uint32_t>(target.NodeCount());
    for (ScenePrefabNodeDesc node : source.Nodes()) {
        if (node.parentNode != ScenePrefabNodeDesc::NoParent) {
            node.parentNode += nodeOffset;
        }
        static_cast<void>(target.AddNode(std::move(node)));
    }
}

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
    document.worldPrefab.Reserve(roots.size());
    for (const SceneObject& root : roots) {
        AppendPrefab(document.worldPrefab, scene.Prefabs().Capture(root));
    }
    return document;
}

} // namespace kb::scene
