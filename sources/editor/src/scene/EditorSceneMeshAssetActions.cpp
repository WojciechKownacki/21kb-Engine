#include "scene/EditorSceneMeshAssetActions.hpp"

#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObjectDesc.hpp"

#include <string>
#include <utility>

namespace kb::editor {
namespace {

[[nodiscard]] std::string MeshEntityName(std::string_view name) {
    return name.empty() ? std::string{ "Mesh" } : std::string{ name };
}

} // namespace

kb::scene::SceneEntity EditorSceneMeshAssetActions::CreateMeshEntity(
    kb::scene::Scene& scene,
    kb::assets::AssetId meshAssetId,
    std::string_view name,
    kb::scene::Vec3 position) {
    if (!meshAssetId.IsValid()) {
        return {};
    }

    kb::scene::SceneObjectDesc desc{};
    desc.name = MeshEntityName(name);
    desc.transform.localPosition = position;
    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(std::move(desc));
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshAssetId.value,
    });
    return entity;
}

} // namespace kb::editor
