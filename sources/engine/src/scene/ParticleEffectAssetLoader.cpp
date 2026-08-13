#include "engine/scene/ParticleEffectAssetLoader.hpp"

#include "engine/scene/ParticleEffectAssetIO.hpp"

#include <memory>
#include <sstream>
#include <utility>

namespace kb::scene {

std::string_view ParticleEffectAssetLoader::Type() const noexcept {
    return kParticleEffectAssetType;
}

std::type_index ParticleEffectAssetLoader::PayloadType() const noexcept {
    return typeid(ParticleEffectAsset);
}

std::vector<std::string> ParticleEffectAssetLoader::Extensions() const {
    return {kParticleEffectAssetExtension};
}

kb::assets::AssetLoadResult ParticleEffectAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    ParticleEffectLoadResult result = ParticleEffectAssetIO::LoadDetailed(request.resolvedPath);
    if (!result.Succeeded()) {
        std::string error;
        for (const ParticleEffectDiagnostic& diagnostic : result.diagnostics) {
            if (!error.empty())
                error.push_back('\n');
            error += FormatParticleEffectDiagnostic(diagnostic);
        }
        return kb::assets::AssetLoadResult{.asset = {}, .error = std::move(error)};
    }
    return kb::assets::AssetLoadResult{.asset = std::make_shared<ParticleEffectAsset>(std::move(*result.asset))};
}

std::vector<kb::assets::AssetId>
ParticleEffectAssetLoader::DiscoverDependencies(const kb::assets::AssetMetadata& metadata,
                                                const kb::assets::AssetRegistry& registry) const {
    return ParticleEffectAssetValidator::ValidateDependencies(metadata, registry).dependencies;
}

std::optional<std::string>
ParticleEffectAssetLoader::ValidateDependencies(const kb::assets::AssetMetadata& metadata,
                                                const kb::assets::AssetRegistry& registry) const {
    const ParticleEffectDependencyResult result =
        ParticleEffectAssetValidator::ValidateDependencies(metadata, registry);
    if (result.diagnostics.empty())
        return std::nullopt;
    std::ostringstream message;
    for (std::size_t index = 0U; index < result.diagnostics.size(); ++index) {
        if (index != 0U)
            message << '\n';
        message << FormatParticleEffectDiagnostic(result.diagnostics[index]);
    }
    return message.str();
}

} // namespace kb::scene
