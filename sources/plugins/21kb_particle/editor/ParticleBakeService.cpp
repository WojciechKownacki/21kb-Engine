#include "editor/ParticleBakeService.hpp"

#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/scene/ParticleEffectAssetIO.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <vector>

namespace kb::particle_editor {
namespace {

void Append(std::vector<std::uint8_t>& bytes, std::span<const std::uint8_t> value) {
    bytes.insert(bytes.end(), value.begin(), value.end());
}
void AppendUInt64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (std::uint32_t shift = 0U; shift < 64U; shift += 8U)
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
}
void AppendText(std::vector<std::uint8_t>& bytes, std::string_view text) {
    AppendUInt64(bytes, text.size());
    Append(bytes, {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
}

[[nodiscard]] std::uint64_t DependencyHash(std::span<const kb::assets::AssetId> dependencies,
                                           const kb::assets::AssetRegistry& registry) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(dependencies.size() * 96U);
    for (const kb::assets::AssetId id : dependencies) {
        const kb::assets::AssetMetadata* metadata = registry.Find(id);
        if (metadata == nullptr) continue;
        AppendUInt64(bytes, metadata->id.value); AppendText(bytes, metadata->type);
        AppendText(bytes, kb::assets::NormalizeAssetPath(metadata->virtualPath));
        AppendUInt64(bytes, metadata->contentHash);
    }
    return kb::particles::ParticleCompiledEffectCache::HashBytes(bytes);
}

void AddCacheDiagnostic(ParticleBakeResult& result, kb::scene::ParticleEffectDiagnosticCode code,
                        std::string message) {
    result.diagnostics.push_back({.code = code, .propertyPath = "effect.bakeCache", .message = std::move(message)});
}

} // namespace

ParticleBakeResult ParticleBakeService::Bake(const ParticleBakeRequest& request) {
    ParticleBakeResult result;
    std::vector<kb::scene::ParticleEffectDiagnostic> serializationDiagnostics;
    const std::optional<std::string> canonical =
        kb::scene::ParticleEffectAssetIO::Serialize(request.workingAsset, serializationDiagnostics);
    if (!canonical || canonical->size() > kb::scene::kParticleEffectMaxSourceBytes) {
        result.diagnostics = std::move(serializationDiagnostics);
        if (result.diagnostics.empty())
            result.diagnostics.push_back({.code = kb::scene::ParticleEffectDiagnosticCode::SourceTooLarge,
                .propertyPath = "effect", .message = "canonical particle effect source exceeds its hard limit"});
        return result;
    }

    kb::particle_plugin::ParticleCompileResult compiled = kb::particle_plugin::ParticleEffectCompiler::Compile(
        request.workingAsset, request.owner, request.registry, request.compile);
    if (!compiled.Succeeded()) {
        result.diagnostics = std::move(compiled.diagnostics);
        result.status = std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [](const auto& diagnostic) {
            return diagnostic.code == kb::scene::ParticleEffectDiagnosticCode::UnsupportedCapability;
        }) ? ParticleBakeStatus::UnsupportedCapability : ParticleBakeStatus::InvalidAsset;
        return result;
    }

    result.key = {
        .sourceHash = kb::particles::ParticleCompiledEffectCache::HashText(*canonical),
        .dependencyHash = DependencyHash(compiled.transitiveDependencies, request.registry),
        .compilerVersion = kb::particles::kParticleCompiledEffectVersion,
        .platform = request.compile.platform,
        .capabilityKey = request.compile.capabilities.StableKey(),
    };
    result.cachePath = kb::particles::ParticleCompiledEffectCache::PathFor(request.cacheRoot, result.key);
    const kb::particles::ParticleCompiledEffectCacheResult cached =
        kb::particles::ParticleCompiledEffectCache::Load(result.cachePath, result.key);
    if (cached.Succeeded()) {
        result.status = ParticleBakeStatus::UpToDate;
        result.effect = cached.effect;
        return result;
    }
    if (cached.status == kb::particles::ParticleCompiledEffectCacheStatus::FileAccessFailed) {
        result.status = ParticleBakeStatus::CacheWriteFailed;
        AddCacheDiagnostic(result, kb::scene::ParticleEffectDiagnosticCode::FileAccessFailed,
                           "compiled particle effect cache cannot be read");
        return result;
    }

    const auto saved = kb::particles::ParticleCompiledEffectCache::Save(result.cachePath, result.key, *compiled.effect);
    if (saved != kb::particles::ParticleCompiledEffectCacheStatus::Success) {
        result.status = ParticleBakeStatus::CacheWriteFailed;
        AddCacheDiagnostic(result, saved == kb::particles::ParticleCompiledEffectCacheStatus::SourceTooLarge
            ? kb::scene::ParticleEffectDiagnosticCode::SourceTooLarge
            : kb::scene::ParticleEffectDiagnosticCode::AtomicWriteFailed,
            "compiled particle effect cache rebuild failed atomically");
        return result;
    }
    const kb::particles::ParticleCompiledEffectCacheResult verified =
        kb::particles::ParticleCompiledEffectCache::Load(result.cachePath, result.key);
    if (!verified.Succeeded()) {
        result.status = ParticleBakeStatus::CacheWriteFailed;
        AddCacheDiagnostic(result, kb::scene::ParticleEffectDiagnosticCode::InvalidCompiledCache,
                           "rebuilt compiled particle effect cache failed verification");
        return result;
    }
    result.status = ParticleBakeStatus::Baked;
    result.effect = verified.effect;
    return result;
}

} // namespace kb::particle_editor
