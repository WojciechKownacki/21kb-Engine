#include "engine/scene/ParticleEffectAssetLoader.hpp"

#include "engine/scene/ParticleEffectAssetIO.hpp"

#include <memory>
#include <utility>

namespace kb::scene {

std::string_view ParticleEffectAssetLoader::Type() const noexcept {
    return kParticleEffectAssetType;
}

std::type_index ParticleEffectAssetLoader::PayloadType() const noexcept {
    return typeid(ParticleEffectAsset);
}

std::vector<std::string> ParticleEffectAssetLoader::Extensions() const {
    return { kParticleEffectAssetExtension };
}

kb::assets::AssetLoadResult ParticleEffectAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    std::optional<ParticleEffectAsset> asset = ParticleEffectAssetIO::Load(request.resolvedPath);
    if (!asset.has_value()) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = "Particle effect asset could not be loaded or parsed." };
    }
    return kb::assets::AssetLoadResult{
        .asset = std::make_shared<ParticleEffectAsset>(std::move(*asset)),
        .error = {},
    };
}

} // namespace kb::scene
