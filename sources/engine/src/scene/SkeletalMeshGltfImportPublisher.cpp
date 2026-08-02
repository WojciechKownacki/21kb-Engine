#include "engine/scene/SkeletalMeshGltfImportPublisher.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/AnimationAssetIO.hpp"
#include "engine/scene/SkeletonAssetIO.hpp"
#include "engine/scene/SkeletalMeshAssetIO.hpp"

#include <functional>
#include <system_error>
#include <utility>

namespace kb::scene {
namespace {

struct StagedAsset {
    std::filesystem::path virtualPath;
    std::filesystem::path target;
    std::filesystem::path staging;
    std::filesystem::path backup;
    bool hadPrevious = false;
    bool installed = false;
};

template <typename T>
[[nodiscard]] std::optional<T> Fail(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
    return std::nullopt;
}

[[nodiscard]] std::filesystem::path StagingPath(const std::filesystem::path& target, std::size_t index) {
    return target.parent_path() / (target.stem().string() + ".skeletal-staging-" +
        std::to_string(index) + target.extension().string());
}

void RemoveIfExists(const std::filesystem::path& path) noexcept {
    std::error_code error;
    std::filesystem::remove(path, error);
}

[[nodiscard]] bool Move(const std::filesystem::path& from, const std::filesystem::path& to) noexcept {
    std::error_code error;
    std::filesystem::rename(from, to, error);
    return !error;
}

void Rollback(std::vector<StagedAsset>& staged) noexcept {
    for (auto it = staged.rbegin(); it != staged.rend(); ++it) {
        if (it->installed) RemoveIfExists(it->target);
        if (it->hadPrevious) static_cast<void>(Move(it->backup, it->target));
        RemoveIfExists(it->staging);
    }
}

[[nodiscard]] std::optional<std::filesystem::path> PhysicalPath(
    const kb::assets::AssetManager& manager,
    const std::filesystem::path& virtualPath) {
    return manager.Mounts().Resolve(virtualPath);
}

} // namespace

std::optional<SkeletalMeshGltfPublishResult> SkeletalMeshGltfImportPublisher::Publish(
    kb::assets::AssetManager& manager,
    const SkeletalMeshGltfImportPlan& plan,
    std::string* error) {
    if (error != nullptr) error->clear();
    if (!plan.skeletonAssetId.IsValid() || plan.skeletonVirtualPath.empty() || plan.meshVirtualPath.empty()) {
        return Fail<SkeletalMeshGltfPublishResult>(error, "Skeletal glTF import plan is incomplete.");
    }
    const std::filesystem::path folder = plan.meshVirtualPath.parent_path();
    const std::string stem = plan.meshVirtualPath.stem().string();
    const std::filesystem::path meshVirtualPath = plan.meshVirtualPath;
    const auto meshPhysicalPath = PhysicalPath(manager, meshVirtualPath);
    if (!meshPhysicalPath) return Fail<SkeletalMeshGltfPublishResult>(error,
        "Skeletal glTF mesh destination is not mounted.");

    std::vector<StagedAsset> staged;
    const auto add = [&](const std::filesystem::path& virtualPath,
                         const std::function<bool(const std::filesystem::path&)>& write) -> bool {
        const auto physical = PhysicalPath(manager, virtualPath);
        if (!physical) return false;
        StagedAsset asset{};
        asset.virtualPath = virtualPath;
        asset.target = *physical;
        asset.staging = StagingPath(asset.target, staged.size());
        asset.backup = asset.target.string() + ".skeletal-backup-" + std::to_string(staged.size());
        RemoveIfExists(asset.staging);
        RemoveIfExists(asset.backup);
        if (!write(asset.staging)) return false;
        staged.push_back(std::move(asset));
        return true;
    };
    if (!plan.reusesSkeleton && !add(plan.skeletonVirtualPath, [&](const auto& path) {
            return SkeletonAssetIO::Save(path, plan.imported.skeleton);
        })) {
        Rollback(staged);
        return Fail<SkeletalMeshGltfPublishResult>(error, "Skeletal glTF Skeleton staging write failed.");
    }
    if (!add(meshVirtualPath, [&](const auto& path) {
            return SkeletalMeshAssetIO::Save(path, plan.imported.mesh);
        })) {
        Rollback(staged);
        return Fail<SkeletalMeshGltfPublishResult>(error, "Skeletal glTF mesh staging write failed.");
    }
    std::vector<std::filesystem::path> clipVirtualPaths;
    clipVirtualPaths.reserve(plan.imported.clips.size());
    for (std::size_t index = 0U; index < plan.imported.clips.size(); ++index) {
        const std::filesystem::path clipPath = folder /
            (stem + "_Animation_" + std::to_string(index) + kAnimationClipAssetExtension);
        if (!add(clipPath, [&](const auto& path) {
                return AnimationAssetIO::SaveClip(path, plan.imported.clips[index]);
            })) {
            Rollback(staged);
            return Fail<SkeletalMeshGltfPublishResult>(error, "Skeletal glTF animation staging write failed.");
        }
        clipVirtualPaths.push_back(clipPath);
    }
    for (StagedAsset& asset : staged) {
        std::error_code existsError;
        asset.hadPrevious = std::filesystem::exists(asset.target, existsError) && !existsError;
        if (asset.hadPrevious && !Move(asset.target, asset.backup)) {
            Rollback(staged);
            return Fail<SkeletalMeshGltfPublishResult>(error, "Skeletal glTF import could not preserve the previous asset.");
        }
        if (!Move(asset.staging, asset.target)) {
            Rollback(staged);
            return Fail<SkeletalMeshGltfPublishResult>(error, "Skeletal glTF import could not publish staged assets.");
        }
        asset.installed = true;
    }
    for (const StagedAsset& asset : staged) RemoveIfExists(asset.backup);
    static_cast<void>(manager.DiscoverMountedAssets());
    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().FindByPath(meshVirtualPath);
    if (meshMetadata == nullptr || meshMetadata->type != kSkeletalMeshAssetType) {
        return Fail<SkeletalMeshGltfPublishResult>(error,
            "Skeletal glTF import published files but asset discovery did not register the mesh.");
    }
    SkeletalMeshGltfPublishResult result{};
    result.skeletonAssetId = plan.skeletonAssetId;
    result.meshAssetId = meshMetadata->id;
    result.createdSkeleton = !plan.reusesSkeleton;
    for (const std::filesystem::path& path : clipVirtualPaths) {
        const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath(path);
        if (metadata == nullptr || metadata->type != kAnimationClipAssetType) {
            return Fail<SkeletalMeshGltfPublishResult>(error,
                "Skeletal glTF import published an animation clip that asset discovery did not register.");
        }
        result.animationClipAssetIds.push_back(metadata->id);
    }
    return result;
}

} // namespace kb::scene
