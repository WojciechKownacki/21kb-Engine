#pragma once

#include "engine/particles/ParticleRuntimeResult.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace kb::scene {

class Scene;

} // namespace kb::scene

namespace kb::particles {

class IParticleSimulationBackend {
public:
    virtual ~IParticleSimulationBackend() = default;

    [[nodiscard]] virtual ParticleRuntimeResult Create(
        kb::scene::Scene& scene,
        std::uint64_t effectAssetId,
        kb::scene::SceneEntity owner) = 0;
    [[nodiscard]] virtual ParticleRuntimeResult Release(kb::scene::Scene& scene, std::uint64_t instanceId) noexcept = 0;
    [[nodiscard]] virtual ParticleRuntimeResult Play(kb::scene::Scene& scene, std::uint64_t instanceId) noexcept = 0;
    [[nodiscard]] virtual ParticleRuntimeResult Pause(kb::scene::Scene& scene, std::uint64_t instanceId) noexcept = 0;
    [[nodiscard]] virtual ParticleRuntimeResult Stop(kb::scene::Scene& scene, std::uint64_t instanceId) noexcept = 0;
    [[nodiscard]] virtual ParticleRuntimeResult Restart(kb::scene::Scene& scene, std::uint64_t instanceId) noexcept = 0;
    [[nodiscard]] virtual ParticleRuntimeResult SetSeed(kb::scene::Scene& scene, std::uint64_t instanceId, std::uint64_t seed) noexcept = 0;
    [[nodiscard]] virtual ParticleRuntimeResult SetParameterScalar(
        kb::scene::Scene& scene,
        std::uint64_t instanceId,
        std::string_view name,
        float value) noexcept = 0;
    [[nodiscard]] virtual ParticleRuntimeResult ClearParameter(
        kb::scene::Scene& scene,
        std::uint64_t instanceId,
        std::string_view name) noexcept = 0;
    [[nodiscard]] virtual ParticleRuntimeResult Emit(
        kb::scene::Scene& scene,
        std::uint64_t instanceId,
        std::uint32_t count) = 0;
    [[nodiscard]] virtual ParticleRuntimeQueryResult Query(
        const kb::scene::Scene& scene,
        std::uint64_t instanceId) const noexcept = 0;
    [[nodiscard]] virtual std::size_t CopyLiveInstanceIds(
        const kb::scene::Scene& scene,
        std::span<std::uint64_t> output) const noexcept = 0;
    [[nodiscard]] virtual std::size_t CopyLiveParticleStates(
        const kb::scene::Scene& scene,
        std::uint64_t instanceId,
        std::span<ParticleRuntimeState> output) const noexcept = 0;
};

} // namespace kb::particles
