#include "engine/scene/SkeletalMeshFbxImportPublisher.hpp"

#include "engine/scene/SkeletalMeshGltfImportPublisher.hpp"

namespace kb::scene {

std::optional<SkeletalMeshFbxPublishResult> SkeletalMeshFbxImportPublisher::Publish(
    kb::assets::AssetManager& manager, const SkeletalMeshFbxImportPlan& plan, std::string* error) {
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
    };
    const auto published = SkeletalMeshGltfImportPublisher::Publish(manager, canonicalPlan, error);
    if (!published) return std::nullopt;
    return SkeletalMeshFbxPublishResult{
        .skeletonAssetId = published->skeletonAssetId,
        .meshAssetId = published->meshAssetId,
        .animationClipAssetIds = published->animationClipAssetIds,
        .createdSkeleton = published->createdSkeleton,
    };
}

} // namespace kb::scene
