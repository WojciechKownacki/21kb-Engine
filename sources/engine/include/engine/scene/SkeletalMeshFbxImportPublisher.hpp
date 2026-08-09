#pragma once

#include "engine/scene/SkeletalMeshFbxImportPlanner.hpp"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace kb::assets { class AssetManager; struct AssetId; }

namespace kb::scene {

struct SkeletalMeshImportArtifact;

struct SkeletalMeshFbxPublishResult {
    kb::assets::AssetId skeletonAssetId{};
    kb::assets::AssetId meshAssetId{};
    std::vector<kb::assets::AssetId> animationClipAssetIds;
    bool createdSkeleton = false;
};

class SkeletalMeshFbxImportPublisher final {
public:
    SkeletalMeshFbxImportPublisher() = delete;

    [[nodiscard]] static std::optional<SkeletalMeshFbxPublishResult> Publish(
        kb::assets::AssetManager& manager,
        const SkeletalMeshFbxImportPlan& plan,
        std::string* error = nullptr);

    [[nodiscard]] static std::optional<SkeletalMeshFbxPublishResult> PublishWithArtifacts(
        kb::assets::AssetManager& manager,
        const SkeletalMeshFbxImportPlan& plan,
        std::span<const SkeletalMeshImportArtifact> artifacts,
        std::string* error = nullptr);
};

} // namespace kb::scene
