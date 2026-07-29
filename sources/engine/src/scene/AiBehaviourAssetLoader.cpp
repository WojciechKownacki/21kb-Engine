#include "engine/scene/AiBehaviourAssetLoader.hpp"

#include "engine/scene/AiBehaviourAssetIO.hpp"

#include <memory>

namespace kb::scene {

std::string_view AiBehaviourAssetLoader::Type() const noexcept { return kAiBehaviourAssetType; }
std::type_index AiBehaviourAssetLoader::PayloadType() const noexcept { return typeid(AiBehaviourAsset); }
std::vector<std::string> AiBehaviourAssetLoader::Extensions() const { return { kAiBehaviourAssetExtension }; }
kb::assets::AssetLoadResult AiBehaviourAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    auto asset = AiBehaviourAssetIO::Load(request.resolvedPath);
    return asset ? kb::assets::AssetLoadResult{ std::make_shared<AiBehaviourAsset>(std::move(*asset)), {} }
                 : kb::assets::AssetLoadResult{ {}, "AI behaviour asset could not be loaded or validated." };
}

} // namespace kb::scene
