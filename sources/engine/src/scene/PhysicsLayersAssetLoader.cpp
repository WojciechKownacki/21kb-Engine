#include "engine/scene/PhysicsLayersAssetLoader.hpp"

#include "engine/scene/PhysicsLayersAssetIO.hpp"

#include <memory>
#include <utility>

namespace kb::scene {

std::string_view PhysicsLayersAssetLoader::Type() const noexcept {
    return "PhysicsLayers";
}

std::type_index PhysicsLayersAssetLoader::PayloadType() const noexcept {
    return typeid(PhysicsLayersAsset);
}

std::vector<std::string> PhysicsLayersAssetLoader::Extensions() const {
    return { std::string{ PhysicsLayersAssetFormat::Extension } };
}

kb::assets::AssetLoadResult PhysicsLayersAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    std::vector<std::uint8_t> sourceBytes;
    std::string error;
    if (!request.ReadSourceBytes(sourceBytes, error)) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = std::move(error) };
    }
    PhysicsLayersAssetLoadResult loaded = DecodePhysicsLayersAsset(sourceBytes);
    if (!loaded.succeeded) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = std::move(loaded.error) };
    }
    return kb::assets::AssetLoadResult{
        .asset = std::make_shared<PhysicsLayersAsset>(std::move(loaded.asset)), .error = {} };
}

} // namespace kb::scene
