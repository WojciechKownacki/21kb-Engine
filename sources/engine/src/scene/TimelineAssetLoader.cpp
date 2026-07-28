#include "engine/scene/TimelineAssetLoader.hpp"

#include "engine/scene/TimelineAsset.hpp"
#include "engine/scene/TimelineAssetIO.hpp"

#include <memory>

namespace kb::scene {

std::string_view TimelineAssetLoader::Type() const noexcept {
    return kTimelineAssetType;
}

std::type_index TimelineAssetLoader::PayloadType() const noexcept {
    return typeid(TimelineAsset);
}

std::vector<std::string> TimelineAssetLoader::Extensions() const {
    return { kTimelineAssetExtension };
}

kb::assets::AssetLoadResult TimelineAssetLoader::Load(
    const kb::assets::AssetLoadRequest& request) {
    std::optional<TimelineAsset> asset =
        TimelineAssetIO::Load(request.resolvedPath);
    return asset
        ? kb::assets::AssetLoadResult{
              std::make_shared<TimelineAsset>(std::move(*asset)), {} }
        : kb::assets::AssetLoadResult{
              {}, "Timeline asset could not be loaded or validated." };
}

} // namespace kb::scene
