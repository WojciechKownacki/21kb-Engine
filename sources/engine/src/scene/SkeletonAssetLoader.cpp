#include "engine/scene/SkeletonAssetLoader.hpp"

#include "engine/scene/SkeletonAssetIO.hpp"

#include <memory>

namespace kb::scene {

std::string_view SkeletonAssetLoader::Type() const noexcept { return kSkeletonAssetType; }
std::type_index SkeletonAssetLoader::PayloadType() const noexcept { return typeid(SkeletonAsset); }
std::vector<std::string> SkeletonAssetLoader::Extensions() const { return { kSkeletonAssetExtension }; }

kb::assets::AssetLoadResult SkeletonAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    std::optional<SkeletonAsset> asset = SkeletonAssetIO::Load(request.resolvedPath);
    return asset ? kb::assets::AssetLoadResult{ std::make_shared<SkeletonAsset>(std::move(*asset)), {} }
                 : kb::assets::AssetLoadResult{ {}, "Skeleton asset could not be loaded or validated." };
}

} // namespace kb::scene
