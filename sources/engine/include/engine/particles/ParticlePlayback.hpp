#pragma once

#include "engine/particles/IParticleSimulationBackend.hpp"
#include "engine/particles/ParticleRenderCapabilities.hpp"
#include "engine/particles/ParticleRenderSnapshot.hpp"
#include "engine/particles/ParticleGpuVisualStepJournal.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace kb::particles {

class ParticlePlayback final {
public:
    ParticlePlayback() = delete;

    [[nodiscard]] static ParticleRuntimeResult RegisterBackend(kb::scene::Scene& scene, IParticleSimulationBackend& backend) noexcept;
    [[nodiscard]] static ParticleRuntimeResult UnregisterBackend(kb::scene::Scene& scene, IParticleSimulationBackend& backend) noexcept;
    [[nodiscard]] static bool HasBackend(const kb::scene::Scene& scene) noexcept;
    [[nodiscard]] static std::uint64_t BackendEpoch(const kb::scene::Scene& scene) noexcept;

    [[nodiscard]] static ParticleRenderSnapshotResult WarmupRenderSnapshots(kb::scene::Scene& scene) noexcept;
    [[nodiscard]] static ParticleRenderSnapshotResult PublishRenderSnapshot(
        kb::scene::Scene& scene,
        IParticleSimulationBackend& backend,
        const ParticleRenderSnapshotPublishDesc& desc) noexcept;
    [[nodiscard]] static std::shared_ptr<const ParticleRenderSnapshot> ReadRenderSnapshot(
        const kb::scene::Scene& scene) noexcept;
    [[nodiscard]] static ParticleRenderSnapshotResult LastRenderSnapshotPublicationResult(
        const kb::scene::Scene& scene) noexcept;
    [[nodiscard]] static ParticleRenderCapabilityResult PublishRenderCapabilities(
        kb::scene::Scene& scene,
        std::uint64_t consumerId,
        ParticleRenderCapabilities capabilities) noexcept;
    [[nodiscard]] static ParticleRenderCapabilityResult AcknowledgeRenderedFixedStep(
        kb::scene::Scene& scene,
        std::uint64_t consumerId,
        std::uint64_t fixedStepIndex) noexcept;
    [[nodiscard]] static ParticleRenderCapabilityResult ClearRenderCapabilities(
        kb::scene::Scene& scene,
        std::uint64_t consumerId) noexcept;
    [[nodiscard]] static ParticleRenderCapabilities RenderCapabilities(const kb::scene::Scene& scene) noexcept;
    [[nodiscard]] static std::span<const ParticleGpuVisualStep> PendingGpuVisualSteps(
        const kb::scene::Scene& scene,
        std::uint64_t consumerId) noexcept;

    [[nodiscard]] static ParticleRuntimeResult Create(kb::scene::Scene& scene, std::uint64_t effectAssetId, kb::scene::SceneEntity owner);
    [[nodiscard]] static ParticleRuntimeResult Release(kb::scene::Scene& scene, std::uint64_t instanceId) noexcept;
    [[nodiscard]] static ParticleRuntimeResult Play(kb::scene::Scene& scene, std::uint64_t instanceId) noexcept;
    [[nodiscard]] static ParticleRuntimeResult Pause(kb::scene::Scene& scene, std::uint64_t instanceId) noexcept;
    [[nodiscard]] static ParticleRuntimeResult Stop(kb::scene::Scene& scene, std::uint64_t instanceId) noexcept;
    [[nodiscard]] static ParticleRuntimeResult Restart(kb::scene::Scene& scene, std::uint64_t instanceId) noexcept;
    [[nodiscard]] static ParticleRuntimeResult SetSeed(kb::scene::Scene& scene, std::uint64_t instanceId, std::uint64_t seed) noexcept;
    [[nodiscard]] static ParticleRuntimeResult SetParameterScalar(kb::scene::Scene& scene, std::uint64_t instanceId, std::string_view name, float value) noexcept;
    [[nodiscard]] static ParticleRuntimeResult ClearParameter(kb::scene::Scene& scene, std::uint64_t instanceId, std::string_view name) noexcept;
    [[nodiscard]] static ParticleRuntimeResult Emit(kb::scene::Scene& scene, std::uint64_t instanceId, std::uint32_t count);
    [[nodiscard]] static ParticleRuntimeResult Simulate(kb::scene::Scene& scene, float fixedDeltaSeconds);
    [[nodiscard]] static ParticleRuntimeResult ConfigureComponent(
        kb::scene::Scene& scene,
        std::uint64_t instanceId,
        float rateMultiplier,
        std::uint32_t maxParticlesOverride,
        bool followTransform,
        const kb::scene::WorldTransform& ownerTransform) noexcept;
    [[nodiscard]] static ParticleRuntimeQueryResult Query(const kb::scene::Scene& scene, std::uint64_t instanceId) noexcept;
    [[nodiscard]] static std::vector<std::uint64_t> LiveInstanceIds(const kb::scene::Scene& scene);
    [[nodiscard]] static std::span<const ParticleRuntimeState> LiveParticleStates(
        const kb::scene::Scene& scene,
        std::uint64_t instanceId);

    [[nodiscard]] static ParticleRuntimeResult QueueEvent(kb::scene::Scene& scene, PendingParticleRuntimeEvent event);
    [[nodiscard]] static std::vector<PendingParticleRuntimeEvent> DrainEvents(kb::scene::Scene& scene);
};

} // namespace kb::particles
