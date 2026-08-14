#pragma once

#include "ParticleEffectCompiler.hpp"
#include "engine/particles/ParticleCompiledEffectCache.hpp"

#include <filesystem>

namespace kb::particle_editor {

enum class ParticleBakeStatus : std::uint8_t {
    Baked,
    UpToDate,
    InvalidAsset,
    UnsupportedCapability,
    CacheWriteFailed,
};

struct ParticleBakeRequest {
    const kb::scene::ParticleEffectAsset& workingAsset;
    const kb::assets::AssetMetadata& owner;
    const kb::assets::AssetRegistry& registry;
    std::filesystem::path cacheRoot;
    kb::particle_plugin::ParticleCompileRequest compile{};
};

struct ParticleBakeResult {
    ParticleBakeStatus status = ParticleBakeStatus::InvalidAsset;
    kb::particles::ParticleCompiledEffectHandle effect;
    kb::particles::ParticleCompiledEffectCacheKey key{};
    std::filesystem::path cachePath;
    std::vector<kb::scene::ParticleEffectDiagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == ParticleBakeStatus::Baked || status == ParticleBakeStatus::UpToDate;
    }
};

class ParticleBakeService final {
  public:
    ParticleBakeService() = delete;
    [[nodiscard]] static ParticleBakeResult Bake(const ParticleBakeRequest& request);
};

} // namespace kb::particle_editor
