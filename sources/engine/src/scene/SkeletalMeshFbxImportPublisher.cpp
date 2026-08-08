#include "engine/scene/SkeletalMeshFbxImportPublisher.hpp"

#include "engine/scene/SkeletalMeshGltfImportPublisher.hpp"

namespace kb::scene {

std::optional<SkeletalMeshFbxPublishResult> SkeletalMeshFbxImportPublisher::Publish(
    kb::assets::AssetManager& manager, const SkeletalMeshFbxImportPlan& plan, std::string* error) {
    return PublishWithArtifacts(manager, plan, {}, error);
}

std::optional<SkeletalMeshFbxPublishResult> SkeletalMeshFbxImportPublisher::PublishWithArtifacts(
    kb::assets::AssetManager& manager, const SkeletalMeshFbxImportPlan& plan,
    std::span<const SkeletalMeshImportArtifact> artifacts, std::string* error) {
    // Publication is source-format agnostic. Reuse the existing transactional
    // writer so FBX has the identical all-or-nothing asset contract as glTF.
    const SkeletalMeshGltfImportPlan canonicalPlan{
        .imported = {
            .skeleton = plan.imported.skeleton,
            .mesh = plan.imported.mesh,
            .clips = plan.imported.clips,
        },
        .skeletonAssetId = plan.skeletonAssetId,
        .skeletonVirtualPath = plan.skeletonVirtualPath,
        .meshVirtualPath = plan.meshVirtualPath,
        .reusesSkeleton = plan.reusesSkeleton,
        .updatesSkeleton = plan.updatesSkeleton,
    };
    const auto published = SkeletalMeshGltfImportPublisher::PublishWithArtifacts(
        manager, canonicalPlan, artifacts, error);
    if (!published) return std::nullopt;
    return SkeletalMeshFbxPublishResult{
        .skeletonAssetId = published->skeletonAssetId,
        .meshAssetId = published->meshAssetId,
        .animationClipAssetIds = published->animationClipAssetIds,
        .createdSkeleton = published->createdSkeleton,
    };
}

} // namespace kb::scene
