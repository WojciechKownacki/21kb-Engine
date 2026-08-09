#include "scene/EditorSceneMeshAssetActions.hpp"

#include "engine/scene/DrawD3DeformedGeometryComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/SkeletonBindingComponent.hpp"
#include "engine/scene/SkeletalMeshAssetIO.hpp"

#include <string>
#include <utility>

namespace kb::editor {
namespace {

[[nodiscard]] std::string MeshEntityName(std::string_view name) {
    return name.empty() ? std::string{ "Mesh" } : std::string{ name };
}

} // namespace

bool EditorSceneMeshAssetActions::IsMeshAsset(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "RenderMesh" || metadata.importCategory == "Model" || metadata.importCategory == "Mesh";
}

bool EditorSceneMeshAssetActions::IsScenePlaceableAsset(const kb::assets::AssetMetadata& metadata) noexcept {
    return IsMeshAsset(metadata) || metadata.type == kb::scene::kSkeletalMeshAssetType;
}

bool EditorSceneMeshAssetActions::AssignMesh(
    kb::scene::Scene& scene,
    kb::scene::SceneEntity entity,
    kb::assets::AssetId meshAssetId) {
    if (!scene.Entities().IsAlive(entity)) {
        return false;
    }

    kb::scene::MeshRendererComponent* renderer = scene.Components().MeshRenderers().TryGet(entity);
    if (renderer == nullptr) {
        return false;
    }

    renderer->meshAssetId = meshAssetId.value;
    scene.Components().MeshRenderers().MarkModified(entity);
    return true;
}

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

kb::scene::SceneEntity EditorSceneMeshAssetActions::CreateSkeletalMeshEntity(
    kb::scene::Scene& scene,
    kb::assets::AssetId meshAssetId,
    kb::assets::AssetId skeletonAssetId,
    std::uint64_t skeletonCompatibilitySignature,
    std::string_view name,
    kb::scene::Vec3 position) {
    if (!meshAssetId.IsValid() || !skeletonAssetId.IsValid() || skeletonCompatibilitySignature == 0U) {
        return {};
    }

    kb::scene::SceneObjectDesc desc{};
    desc.name = MeshEntityName(name);
    desc.transform.localPosition = position;
    const kb::scene::SceneEntity entity = scene.Entities().CreateEntity(std::move(desc));
    scene.Components().MeshRenderers().Set(entity, kb::scene::MeshRendererComponent{
        .meshAssetId = meshAssetId.value,
    });
    const bool geometryAttached = scene.Components().DeformedGeometries().Set(entity, kb::scene::DrawD3DeformedGeometryComponent{
        .skeletalMeshAssetId = meshAssetId.value,
        .enabled = true,
    });
    const bool skeletonAttached = scene.Components().SkeletonBindings().Set(entity, kb::scene::SkeletonBindingComponent{
        .skeletonAssetId = skeletonAssetId.value,
        .skeletonCompatibilitySignature = skeletonCompatibilitySignature,
        .enabled = true,
    });
    if (!geometryAttached || !skeletonAttached) {
        scene.Entities().Destroy(entity);
        return {};
    }
    return entity;
}

} // namespace kb::editor
