#include "engine/scene/SkeletonAssetLoader.hpp"

#include "engine/scene/SkeletonAssetIO.hpp"

#include <memory>

namespace kb::scene {

std::string_view SkeletonAssetLoader::Type() const noexcept { return kSkeletonAssetType; }
std::type_index SkeletonAssetLoader::PayloadType() const noexcept { return typeid(SkeletonAsset); }
std::vector<std::string> SkeletonAssetLoader::Extensions() const { return { kSkeletonAssetExtension }; }

kb::assets::AssetLoadResult SkeletonAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    std::string error;
    if (request.SourceExtension() != kSkeletonAssetExtension) {
        return kb::assets::AssetLoadResult{ {}, "Skeleton asset has an unexpected file extension." };
    }
    std::vector<std::uint8_t> sourceBytes;
    if (!request.ReadSourceBytes(sourceBytes, error)) {
        return kb::assets::AssetLoadResult{ {}, std::move(error) };
    }
    std::optional<SkeletonAsset> asset = SkeletonAssetIO::Load(sourceBytes, &error);
    return asset ? kb::assets::AssetLoadResult{ std::make_shared<SkeletonAsset>(std::move(*asset)), {} }
                 : kb::assets::AssetLoadResult{ {}, std::move(error) };
}

} // namespace kb::scene
