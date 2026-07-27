#pragma once

#include "engine/scene/SceneParticleSystems.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace kb::scene {

class Scene;

// LIB-143: the engine-side logic behind Particles.Create/Play/Stop/SetSeed/
// SetParameterScalar/Emit/Advance - private (kb::scene internals), consumed through the
// public SceneParticleSystems/SceneParticleSystemQueries facades on Scene, mirroring
// SceneMaterialInstanceService's own facade/service split.
class SceneParticleSystemService {
public:
    SceneParticleSystemService() = delete;

    [[nodiscard]] static std::uint64_t Create(Scene& scene, std::uint64_t effectAssetId, SceneEntity owner);
    [[nodiscard]] static bool Release(Scene& scene, std::uint64_t id) noexcept;
    [[nodiscard]] static bool Exists(const Scene& scene, std::uint64_t id) noexcept;
    [[nodiscard]] static bool Play(Scene& scene, std::uint64_t id) noexcept;
    [[nodiscard]] static bool Stop(Scene& scene, std::uint64_t id) noexcept;
    [[nodiscard]] static bool IsPlaying(const Scene& scene, std::uint64_t id) noexcept;
    [[nodiscard]] static bool SetSeed(Scene& scene, std::uint64_t id, std::uint64_t seed) noexcept;
    [[nodiscard]] static bool SetParameterScalar(Scene& scene, std::uint64_t id, std::string_view name, float value) noexcept;
    [[nodiscard]] static bool ClearParameter(Scene& scene, std::uint64_t id, std::string_view name) noexcept;
    [[nodiscard]] static bool Emit(Scene& scene, std::uint64_t id, std::uint32_t count);
    [[nodiscard]] static std::uint64_t EffectAsset(const Scene& scene, std::uint64_t id) noexcept;
    [[nodiscard]] static std::uint64_t ResolvedMaterialAsset(const Scene& scene, std::uint64_t id) noexcept;
    [[nodiscard]] static std::uint32_t LiveParticleCount(const Scene& scene, std::uint64_t id) noexcept;
    [[nodiscard]] static std::span<const ParticleState> Particles(const Scene& scene, std::uint64_t id) noexcept;
    [[nodiscard]] static std::vector<std::uint64_t> LiveInstanceIds(const Scene& scene);
    [[nodiscard]] static std::vector<ParticleSystemFinishedEvent> DrainFinishedEvents(Scene& scene);

    static void Advance(Scene& scene, float deltaSeconds);
};

} // namespace kb::scene
