#pragma once

#include "engine/particles/ParticleCompiledEffect.hpp"
#include "engine/scene/ParticleEffectAssetValidation.hpp"

#include <cstdint>
#include <vector>

namespace kb::assets {
class AssetRegistry;
struct AssetMetadata;
} // namespace kb::assets

namespace kb::particle_plugin {

struct ParticleCompilerCapabilities {
    bool billboard = true;
    bool stretchedBillboard = true;
    bool pointSprite = true;

    [[nodiscard]] std::uint64_t StableKey() const noexcept;
};

struct ParticleCompileRequest {
    kb::particles::ParticleCompilePlatform platform = kb::particles::ParticleCompilePlatform::PlatformIndependent;
    ParticleCompilerCapabilities capabilities{};
};

struct ParticleCompileResult {
    kb::particles::ParticleCompiledEffectHandle effect;
    std::vector<kb::assets::AssetId> transitiveDependencies;
    std::vector<kb::scene::ParticleEffectDiagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept { return effect != nullptr && diagnostics.empty(); }
};

class ParticleEffectCompiler final {
  public:
    ParticleEffectCompiler() = delete;
    [[nodiscard]] static std::vector<kb::scene::ParticleEffectDiagnostic> ValidateCapabilities(
        const kb::scene::ParticleEffectAsset& asset, const ParticleCompileRequest& request = {});
    [[nodiscard]] static ParticleCompileResult Compile(const kb::scene::ParticleEffectAsset& asset,
                                                       const kb::assets::AssetMetadata& owner,
                                                       const kb::assets::AssetRegistry& registry,
                                                       const ParticleCompileRequest& request = {});
};

} // namespace kb::particle_plugin
