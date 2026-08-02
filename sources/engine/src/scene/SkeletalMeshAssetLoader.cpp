#include "engine/scene/SkeletalMeshAssetLoader.hpp"

#include "engine/scene/SkeletalMeshAssetIO.hpp"

#include <memory>

namespace kb::scene {

std::string_view SkeletalMeshAssetLoader::Type() const noexcept { return kSkeletalMeshAssetType; }
std::type_index SkeletalMeshAssetLoader::PayloadType() const noexcept { return typeid(SkeletalMeshAsset); }
std::vector<std::string> SkeletalMeshAssetLoader::Extensions() const { return { kSkeletalMeshAssetExtension }; }

kb::assets::AssetLoadResult SkeletalMeshAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    auto asset = SkeletalMeshAssetIO::Load(request.resolvedPath);
    return asset ? kb::assets::AssetLoadResult{ std::make_shared<SkeletalMeshAsset>(std::move(*asset)), {} }
                 : kb::assets::AssetLoadResult{ {}, "Skeletal mesh asset could not be loaded or validated." };
}

} // namespace kb::scene
