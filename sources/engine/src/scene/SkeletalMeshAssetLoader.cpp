#include "engine/scene/SkeletalMeshAssetLoader.hpp"

#include "engine/assets/AssetRegistry.hpp"
#include "engine/scene/SkeletonAssetIO.hpp"
#include "engine/scene/SkeletalMeshAssetIO.hpp"

#include <memory>

namespace kb::scene {

std::string_view SkeletalMeshAssetLoader::Type() const noexcept { return kSkeletalMeshAssetType; }
std::type_index SkeletalMeshAssetLoader::PayloadType() const noexcept { return typeid(SkeletalMeshAsset); }
std::vector<std::string> SkeletalMeshAssetLoader::Extensions() const { return { kSkeletalMeshAssetExtension }; }

kb::assets::AssetLoadResult SkeletalMeshAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    std::string error;
    auto asset = SkeletalMeshAssetIO::Load(request.resolvedPath, &error);
    return asset ? kb::assets::AssetLoadResult{ std::make_shared<SkeletalMeshAsset>(std::move(*asset)), {} }
                 : kb::assets::AssetLoadResult{ {}, std::move(error) };
}

std::vector<kb::assets::AssetId> SkeletalMeshAssetLoader::DiscoverDependencies(
    const kb::assets::AssetMetadata& metadata,
    const kb::assets::AssetRegistry& registry) const {
    static_cast<void>(registry);
    const auto mesh = SkeletalMeshAssetIO::Load(metadata.physicalPath);
    if (!mesh || mesh->skeletonAssetId == 0U) return {};
    return { kb::assets::AssetId{ mesh->skeletonAssetId } };
}

std::optional<std::string> SkeletalMeshAssetLoader::ValidateDependencies(
    const kb::assets::AssetMetadata& metadata,
    const kb::assets::AssetRegistry& registry) const {
    std::string error;
    const auto mesh = SkeletalMeshAssetIO::Load(metadata.physicalPath, &error);
    if (!mesh) return error;

    const kb::assets::AssetMetadata* skeletonMetadata = registry.Find(
        kb::assets::AssetId{ mesh->skeletonAssetId });
    if (skeletonMetadata == nullptr) return std::nullopt;
    if (skeletonMetadata->type != kSkeletonAssetType) {
        return "references an asset that is not a Skeleton.";
    }
    const auto skeleton = SkeletonAssetIO::Load(skeletonMetadata->physicalPath, &error);
    if (!skeleton) return "references a Skeleton that cannot be loaded: " + error;
    if (SkeletonCompatibilitySignature(*skeleton) != mesh->skeletonCompatibilitySignature) {
        return "references a Skeleton with an incompatible compatibility signature.";
    }
    return std::nullopt;
}

} // namespace kb::scene
