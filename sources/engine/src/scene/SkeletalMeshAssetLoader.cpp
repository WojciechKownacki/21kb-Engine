#include "engine/scene/SkeletalMeshAssetLoader.hpp"

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

} // namespace kb::scene
