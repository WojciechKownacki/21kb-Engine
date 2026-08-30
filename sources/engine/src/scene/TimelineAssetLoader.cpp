#include "engine/scene/TimelineAssetLoader.hpp"

#include "engine/assets/AssetMemoryInputStream.hpp"
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
    std::vector<std::uint8_t> sourceBytes;
    std::string error;
    if (!request.ReadSourceBytes(sourceBytes, error)) {
        return kb::assets::AssetLoadResult{ {}, std::move(error) };
    }
    kb::assets::AssetMemoryInputStream input{ sourceBytes };
    std::optional<TimelineAsset> asset = TimelineAssetIO::Load(input);
    return asset
        ? kb::assets::AssetLoadResult{
              std::make_shared<TimelineAsset>(std::move(*asset)), {} }
        : kb::assets::AssetLoadResult{
              {}, "Timeline asset could not be loaded or validated." };
}

} // namespace kb::scene
