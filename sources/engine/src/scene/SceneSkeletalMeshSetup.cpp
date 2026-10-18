#include "engine/scene/SceneSkeletalMeshSetup.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/DrawD3DeformedGeometryComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SkeletalMeshAssetIO.hpp"
#include "engine/scene/SkeletonBindingComponent.hpp"

namespace kb::scene {

SkeletalMeshSetupResult SceneSkeletalMeshSetup::Attach(Scene& scene, SceneEntity entity, kb::assets::AssetId skeletalMeshAssetId) {
    if (!scene.Entities().IsAlive(entity)) return { .error = "the entity does not exist" };
    const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().Find(skeletalMeshAssetId);
    if (metadata == nullptr || metadata->type != kSkeletalMeshAssetType) {
        return { .error = "the asset is not a Skeletal Mesh (import a rigged model with its skeleton first)" };
    }
    std::string error;
    const std::optional<SkeletalMeshAssetBinding> binding = SkeletalMeshAssetIO::LoadBinding(metadata->physicalPath, &error);
    if (!binding || binding->skeletonAssetId == 0U || binding->skeletonCompatibilitySignature == 0U) {
        return { .error = "the Skeletal Mesh names no skeleton" + (error.empty() ? std::string{} : ": " + error) };
    }
    if (scene.Assets().Manager().Registry().Find(kb::assets::AssetId{ binding->skeletonAssetId }) == nullptr) {
        return { .error = "the Skeletal Mesh's skeleton is missing from the project (re-import the model)" };
    }

    MeshRendererComponent renderer{};
    if (const MeshRendererComponent* existing = scene.Components().MeshRenderers().TryGet(entity)) renderer = *existing;
    renderer.meshAssetId = skeletalMeshAssetId.value;
    scene.Components().MeshRenderers().Set(entity, renderer);

    DrawD3DeformedGeometryComponent geometry{};
    if (const DrawD3DeformedGeometryComponent* existing = scene.Components().DeformedGeometries().TryGet(entity)) geometry = *existing;
    geometry.skeletalMeshAssetId = skeletalMeshAssetId.value;
    geometry.enabled = true;
    if (!scene.Components().DeformedGeometries().Set(entity, geometry)) {
        return { .error = "the Deformed Geometry was refused" };
    }
    if (!scene.Components().SkeletonBindings().Set(entity, SkeletonBindingComponent{
            .skeletonAssetId = binding->skeletonAssetId,
            .skeletonCompatibilitySignature = binding->skeletonCompatibilitySignature,
            .enabled = true,
        })) {
        return { .error = "the Skeleton Binding was refused" };
    }
    return { .succeeded = true };
}

} // namespace kb::scene
