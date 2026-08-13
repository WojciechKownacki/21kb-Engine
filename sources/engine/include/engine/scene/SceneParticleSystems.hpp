#pragma once

#include "engine/particles/ParticlePlayback.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace kb::scene {

class Scene;

// Compatibility name for the core-owned ABI state copied from the active provider.
using ParticleState = kb::particles::ParticleRuntimeState;

struct ParticleSystemFinishedEvent {
    SceneEntity target{};
    std::uint64_t instanceId = 0U;
    std::uint64_t effectAssetId = 0U;
};

// LIB-143: read-only half of SceneParticleSystems, mirroring
// SceneMaterialInstanceQueries' const/mutable split - obtained from a `const Scene&`, safe
// to call from kb::render's per-frame rendering bridge, which never mutates kb::scene state.
class SceneParticleSystemQueries {
public:
    explicit SceneParticleSystemQueries(const Scene& scene) noexcept;

    [[nodiscard]] bool Exists(std::uint64_t id) const noexcept;
    [[nodiscard]] bool IsPlaying(std::uint64_t id) const noexcept;
    // Returns 0 (never a valid asset id) if `id` names no currently live instance.
    [[nodiscard]] std::uint64_t EffectAsset(std::uint64_t id) const noexcept;
    // Returns 0 when the provider is absent or the instance is invalid.
    [[nodiscard]] std::uint64_t ResolvedMaterialAsset(std::uint64_t id) const noexcept;
    [[nodiscard]] std::uint32_t LiveParticleCount(std::uint64_t id) const noexcept;
    [[nodiscard]] std::span<const ParticleState> Particles(std::uint64_t id) const;
    // Provider-reported live instance IDs, copied through the stable core ABI.
    [[nodiscard]] std::vector<std::uint64_t> LiveInstanceIds() const;

private:
    const Scene& scene_;
};

// Compatibility facade over ParticlePlayback. The bool/zero wrappers intentionally discard
// typed failure diagnostics; production callers should prefer the Detailed methods or call
// ParticlePlayback directly. Instance lifetime, parameter support and capacity policy are
// defined by the registered provider. With no provider, Detailed methods return
// BackendUnavailable and compatibility wrappers return false or zero.
class SceneParticleSystems {
public:
    explicit SceneParticleSystems(Scene& scene) noexcept;

    // Returns zero whenever CreateDetailed does not succeed.
    [[nodiscard]] std::uint64_t Create(std::uint64_t effectAssetId, SceneEntity owner);
    [[nodiscard]] kb::particles::ParticleRuntimeResult CreateDetailed(std::uint64_t effectAssetId, SceneEntity owner);
    [[nodiscard]] bool Release(std::uint64_t id) noexcept;
    [[nodiscard]] kb::particles::ParticleRuntimeResult ReleaseDetailed(std::uint64_t id) noexcept;
    [[nodiscard]] bool Exists(std::uint64_t id) const noexcept;
    [[nodiscard]] bool Play(std::uint64_t id) noexcept;
    [[nodiscard]] kb::particles::ParticleRuntimeResult PlayDetailed(std::uint64_t id) noexcept;
    [[nodiscard]] kb::particles::ParticleRuntimeResult Pause(std::uint64_t id) noexcept;
    [[nodiscard]] bool Stop(std::uint64_t id) noexcept;
    [[nodiscard]] kb::particles::ParticleRuntimeResult StopDetailed(std::uint64_t id) noexcept;
    [[nodiscard]] kb::particles::ParticleRuntimeResult Restart(std::uint64_t id) noexcept;
    [[nodiscard]] bool IsPlaying(std::uint64_t id) const noexcept;
    [[nodiscard]] std::uint64_t EffectAsset(std::uint64_t id) const noexcept;
    [[nodiscard]] std::uint64_t ResolvedMaterialAsset(std::uint64_t id) const noexcept;
    [[nodiscard]] std::span<const ParticleState> Particles(std::uint64_t id) const;
    [[nodiscard]] bool SetSeed(std::uint64_t id, std::uint64_t seed) noexcept;
    [[nodiscard]] kb::particles::ParticleRuntimeResult SetSeedDetailed(std::uint64_t id, std::uint64_t seed) noexcept;
    [[nodiscard]] bool SetParameterScalar(std::uint64_t id, std::string_view name, float value) noexcept;
    [[nodiscard]] kb::particles::ParticleRuntimeResult SetParameterScalarDetailed(std::uint64_t id, std::string_view name, float value) noexcept;
    [[nodiscard]] bool ClearParameter(std::uint64_t id, std::string_view name) noexcept;
    [[nodiscard]] kb::particles::ParticleRuntimeResult ClearParameterDetailed(std::uint64_t id, std::string_view name) noexcept;
    [[nodiscard]] bool Emit(std::uint64_t id, std::uint32_t count);
    [[nodiscard]] kb::particles::ParticleRuntimeResult EmitDetailed(std::uint64_t id, std::uint32_t count);
    [[nodiscard]] std::uint32_t LiveParticleCount(std::uint64_t id) const noexcept;
    [[nodiscard]] std::vector<ParticleSystemFinishedEvent> DrainFinishedEvents();

private:
    Scene& scene_;
};

} // namespace kb::scene
