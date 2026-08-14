#pragma once

#include "engine/particles/IParticleSimulationBackend.hpp"
#include "engine/particles/ParticleRenderSnapshot.hpp"
#include "engine/scene/ParticleEffectAsset.hpp"
#include "engine/scene/ParticleEffectAssetSchema.hpp"
#include "engine/scene/ParticleEffectComponent.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace kb::particle_plugin {

class CpuParticleBackend final : public kb::particles::IParticleSimulationBackend {
    friend class ParticleSceneSystem;

public:
    struct StepTelemetry {
        std::uint32_t requestedSpawns = 0U;
        std::uint32_t spawned = 0U;
        std::uint32_t rejectedByCapacity = 0U;
        std::uint32_t rejectedByStepBudget = 0U;
        std::uint32_t rejectedByEventBudget = 0U;
        std::uint32_t deaths = 0U;
        std::uint32_t collisions = 0U;
        std::uint32_t processedEvents = 0U;
    };

    CpuParticleBackend() = default;

    void Warmup();
    [[nodiscard]] bool IsWarmedUp() const noexcept;
    [[nodiscard]] std::size_t LiveInstanceCount() const noexcept;
    [[nodiscard]] std::size_t ParticleCapacity() const noexcept;
    [[nodiscard]] static constexpr std::size_t CommandCapacity() noexcept {
        return kb::scene::kParticleEffectMaxCommandsPerStep;
    }
    [[nodiscard]] static constexpr std::size_t EventCapacity() noexcept {
        return kb::scene::kParticleEffectMaxEventsPerStep;
    }
    [[nodiscard]] std::size_t BufferedCommandCount() const noexcept;
    [[nodiscard]] std::size_t BufferedEventCount() const noexcept;
    [[nodiscard]] StepTelemetry LastStepTelemetry() const noexcept;
    [[nodiscard]] std::size_t CompiledEffectCount() const noexcept;
    [[nodiscard]] kb::particles::ParticleRuntimeResult ConfigureOwnerDeathPolicy(
        std::uint64_t instanceId,
        kb::scene::ParticleOwnerDeathPolicy policy) noexcept;
    [[nodiscard]] kb::particles::ParticleRuntimeResult ConfigureComponent(
        std::uint64_t instanceId,
        float rateMultiplier,
        std::uint32_t maxParticlesOverride,
        bool followTransform,
        const kb::scene::WorldTransform& ownerTransform) noexcept;
    [[nodiscard]] kb::particles::ParticleRuntimeResult Step(
        kb::scene::Scene& scene,
        float fixedDeltaSeconds);

    [[nodiscard]] kb::particles::ParticleRuntimeResult Create(
        kb::scene::Scene& scene,
        std::uint64_t effectAssetId,
        kb::scene::SceneEntity owner) override;
    [[nodiscard]] kb::particles::ParticleRuntimeResult Release(
        kb::scene::Scene& scene,
        std::uint64_t instanceId) noexcept override;
    [[nodiscard]] kb::particles::ParticleRuntimeResult Play(
        kb::scene::Scene& scene,
        std::uint64_t instanceId) noexcept override;
    [[nodiscard]] kb::particles::ParticleRuntimeResult Pause(
        kb::scene::Scene& scene,
        std::uint64_t instanceId) noexcept override;
    [[nodiscard]] kb::particles::ParticleRuntimeResult Stop(
        kb::scene::Scene& scene,
        std::uint64_t instanceId) noexcept override;
    [[nodiscard]] kb::particles::ParticleRuntimeResult Restart(
        kb::scene::Scene& scene,
        std::uint64_t instanceId) noexcept override;
    [[nodiscard]] kb::particles::ParticleRuntimeResult SetSeed(
        kb::scene::Scene& scene,
        std::uint64_t instanceId,
        std::uint64_t seed) noexcept override;
    [[nodiscard]] kb::particles::ParticleRuntimeResult SetParameterScalar(
        kb::scene::Scene& scene,
        std::uint64_t instanceId,
        std::string_view name,
        float value) noexcept override;
    [[nodiscard]] kb::particles::ParticleRuntimeResult ClearParameter(
        kb::scene::Scene& scene,
        std::uint64_t instanceId,
        std::string_view name) noexcept override;
    [[nodiscard]] kb::particles::ParticleRuntimeResult Emit(
        kb::scene::Scene& scene,
        std::uint64_t instanceId,
        std::uint32_t count) override;
    [[nodiscard]] kb::particles::ParticleRuntimeQueryResult Query(
        const kb::scene::Scene& scene,
        std::uint64_t instanceId) const noexcept override;
    [[nodiscard]] std::size_t CopyLiveInstanceIds(
        const kb::scene::Scene& scene,
        std::span<std::uint64_t> output) const noexcept override;
    [[nodiscard]] std::size_t CopyLiveParticleStates(
        const kb::scene::Scene& scene,
        std::uint64_t instanceId,
        std::span<kb::particles::ParticleRuntimeState> output) const noexcept override;

private:
    enum class PlaybackState : std::uint8_t { Stopped, Playing, Paused, Draining };
    enum class CommandType : std::uint8_t { Release, Play, Pause, Stop, Restart, SetSeed, SetParameter, ClearParameter };

    struct RuntimeParameter {
        std::array<char, kb::scene::kParticleEffectMaxRuntimeParameterNameBytes> name{};
        std::uint16_t nameLength = 0U;
        float value = 0.0F;
    };

    struct ParameterEntry {
        std::uint64_t instanceId = 0U;
        RuntimeParameter parameter{};
    };

    struct Command {
        CommandType type = CommandType::Play;
        std::uint64_t instanceId = 0U;
        std::uint64_t seed = 0U;
        RuntimeParameter parameter{};
    };

    struct CompiledCurveKey {
        float time = 0.0F;
        float value = 0.0F;
        kb::math::Easing easing = kb::math::Easing::Linear;
    };

    struct CompiledCurve {
        std::uint8_t keyCount = 0U;
        std::array<CompiledCurveKey, kb::scene::kParticleEffectMaxCurveKeys> keys{};
    };

    struct CompiledGradientStop {
        float time = 0.0F;
        kb::math::Color color{};
    };

    struct CompiledGradient {
        std::uint8_t stopCount = 0U;
        std::array<CompiledGradientStop, kb::scene::kParticleEffectMaxGradientStops> stops{};
    };

    struct CompiledEmitter {
        kb::scene::ParticleStableId emitterId = 0U;
        kb::scene::ParticleOutputType outputType = kb::scene::ParticleOutputType::Billboard;
        std::uint64_t materialAssetId = 0U;
        std::uint64_t meshAssetId = 0U;
        std::uint64_t textureAtlasAssetId = 0U;
        kb::scene::ParticleBlendMode blendMode = kb::scene::ParticleBlendMode::Alpha;
        kb::scene::ParticleSortMode sortMode = kb::scene::ParticleSortMode::BackToFront;
        bool depthTest = true;
        bool depthWrite = false;
        std::uint16_t flipbookFrameCount = 1U;
        float flipbookFramesPerSecond = 0.0F;
        bool flipbookLooping = true;
        float stretchVelocityScale = 0.0F;
        float stretchMinimumLength = 0.0F;
        kb::scene::ParticleSimulationSpace simulationSpace = kb::scene::ParticleSimulationSpace::World;
        bool enabled = false;
        kb::scene::ParticleSpawnMode mode = kb::scene::ParticleSpawnMode::Continuous;
        std::uint32_t maxParticles = 0U;
        kb::math::Vec3 localPosition{};
        kb::scene::ParticleInitialVelocityModule initialVelocity{};
        float lifetimeMin = 1.0F;
        float lifetimeMax = 1.0F;
        float prewarmSeconds = 0.0F;
        std::uint8_t rateKeyCount = 0U;
        std::uint8_t burstCount = 0U;
        std::array<CompiledCurveKey, kb::scene::kParticleEffectMaxCurveKeys> rateKeys{};
        std::array<kb::scene::ParticleBurstAsset, kb::scene::kParticleEffectMaxBursts> bursts{};
        CompiledGradient colorOverLife{};
        CompiledCurve sizeOverLife{};
        CompiledCurve alphaOverLife{};
        using ModulePayload = std::variant<kb::scene::ParticleInitialVelocityModule,
                                           kb::scene::ParticleGravityModule,
                                           kb::scene::ParticleWindModule,
                                           kb::scene::ParticleDragModule,
                                           kb::scene::ParticleCollisionPlaneModule,
                                           kb::scene::ParticleSubEmitterModule>;
        struct Module {
            kb::scene::ParticleStableId moduleId = 0U;
            kb::scene::ParticleModuleType type = kb::scene::ParticleModuleType::InitialVelocity;
            bool enabled = false;
            ModulePayload payload = kb::scene::ParticleInitialVelocityModule{};
        };
        std::uint8_t moduleCount = 0U;
        std::array<Module, kb::scene::kParticleEffectMaxModulesPerEmitter> modules{};
    };

    struct CompiledEffect {
        struct EventBinding {
            std::uint8_t sourceEmitterIndex = 0U;
            kb::scene::ParticleEventTrigger trigger = kb::scene::ParticleEventTrigger::Death;
            kb::scene::ParticleStableId sourceModuleId = 0U;
            std::uint8_t targetEmitterIndex = 0U;
            std::uint32_t count = 1U;
            std::uint8_t maxDepth = 1U;
            std::uint32_t perStepBudget = 1U;
        };
        std::uint64_t assetId = 0U;
        std::uint64_t assetGeneration = 0U;
        std::uint64_t invalidCandidateGeneration = 0U;
        std::uint64_t determinismSeed = 0U;
        float durationSeconds = 0.0F;
        bool looping = false;
        std::uint8_t emitterCount = 0U;
        std::uint8_t eventBindingCount = 0U;
        std::uint16_t referenceCount = 0U;
        std::array<CompiledEmitter, kb::scene::kParticleEffectMaxEmitters> emitters{};
        std::array<EventBinding, kb::scene::kParticleEffectMaxEventBindings> eventBindings{};
    };

    struct InstanceRuntime {
        std::uint32_t compiledEffectIndex = UINT32_MAX;
        std::uint64_t randomState = 0U;
        std::uint64_t spawnOrdinal = 0U;
        std::array<float, kb::scene::kParticleEffectMaxEmitters> elapsedSeconds{};
        std::array<bool, kb::scene::kParticleEffectMaxEmitters> cycleStarted{};
        std::array<float, kb::scene::kParticleEffectMaxEmitters> emissionFractions{};
        std::array<std::uint8_t, kb::scene::kParticleEffectMaxEmitters> nextBurst{};
        std::array<std::uint32_t, kb::scene::kParticleEffectMaxEmitters> liveParticles{};
        std::array<std::uint32_t, kb::scene::kParticleEffectMaxEventBindings> bindingEventsThisStep{};
    };

    struct InternalEvent {
        std::uint64_t instanceId = 0U;
        std::uint8_t sourceEmitterIndex = 0U;
        kb::scene::ParticleStableId sourceModuleId = 0U;
        kb::scene::ParticleEventTrigger trigger = kb::scene::ParticleEventTrigger::Birth;
        std::uint8_t depth = 0U;
        std::uint8_t prewarmGroup = UINT8_MAX;
        kb::math::Vec3 position{};
    };

    static constexpr std::uint32_t kInvalidDenseIndex = UINT32_MAX;

    [[nodiscard]] static std::uint64_t MakeInstanceId(std::uint32_t slot, std::uint32_t generation) noexcept;
    [[nodiscard]] std::uint32_t ResolveDenseIndex(std::uint64_t instanceId) const noexcept;
    [[nodiscard]] kb::particles::ParticleRuntimeResult Submit(Command command) noexcept;
    [[nodiscard]] kb::particles::ParticleRuntimeResult Execute(const Command& command) noexcept;
    void RemoveDenseInstance(std::uint32_t denseIndex) noexcept;
    void RemoveParticles(std::uint64_t instanceId) noexcept;
    [[nodiscard]] std::uint32_t LiveParticleCount(std::uint64_t instanceId) const noexcept;
    [[nodiscard]] std::uint32_t AcquireCompiledEffect(
        kb::scene::Scene& scene,
        std::uint64_t effectAssetId,
        kb::particles::ParticleRuntimeStatus& failureStatus);
    void ReleaseCompiledEffect(std::uint32_t index) noexcept;
    void RefreshCompiledEffects(kb::scene::Scene& scene);
    [[nodiscard]] static bool TopologyCompatible(
        const CompiledEffect& previous,
        const CompiledEffect& candidate) noexcept;
    void ProcessOwnerLifecycle(kb::scene::Scene& scene) noexcept;
    [[nodiscard]] bool FinishOwnerLifecycle(kb::scene::Scene& scene, std::uint32_t denseIndex) noexcept;
    void ResetInstance(std::uint32_t denseIndex) noexcept;
    [[nodiscard]] kb::particles::ParticleRuntimeStatus PrewarmInstance(std::uint32_t denseIndex) noexcept;
    void StepInstance(std::uint32_t denseIndex, float fixedDeltaSeconds, std::uint32_t& remainingSpawnBudget) noexcept;
    void StepEmitter(std::uint32_t denseIndex, std::uint8_t emitterIndex, float fixedDeltaSeconds,
                     std::uint32_t& remainingSpawnBudget, std::uint8_t prewarmGroup = UINT8_MAX) noexcept;
    void SpawnRequested(std::uint32_t denseIndex, std::uint8_t emitterIndex, std::uint32_t count,
                        std::uint32_t& remainingSpawnBudget, std::uint8_t prewarmGroup = UINT8_MAX) noexcept;
    [[nodiscard]] bool SpawnExact(std::uint32_t denseIndex, std::uint8_t emitterIndex, std::uint32_t count,
                                  std::uint8_t eventDepth = 0U, std::uint8_t prewarmGroup = UINT8_MAX,
                                  const kb::math::Vec3* eventPosition = nullptr) noexcept;
    void AdvanceParticleAges(float fixedDeltaSeconds, std::uint64_t onlyInstanceId = 0U,
                             std::uint8_t onlyPrewarmGroup = UINT8_MAX) noexcept;
    void ExecuteForcesAndIntegrate(float fixedDeltaSeconds, std::uint64_t onlyInstanceId = 0U,
                                   std::uint8_t onlyPrewarmGroup = UINT8_MAX) noexcept;
    [[nodiscard]] bool QueueInternalEvent(const InternalEvent& event) noexcept;
    [[nodiscard]] kb::particles::ParticleRuntimeStatus ProcessInternalEvents(
        std::uint32_t& remainingSpawnBudget) noexcept;
    void BeginEventStep() noexcept;
    void RemoveQueuedEvents(std::uint64_t instanceId) noexcept;
    [[nodiscard]] bool EventHasAction(const InternalEvent& event) const noexcept;
    void ApplyOwnerTransformDelta(
        std::uint32_t denseIndex,
        const kb::scene::WorldTransform& previous,
        const kb::scene::WorldTransform& current) noexcept;
    void CapturePreviousParticlePositions() noexcept;
    [[nodiscard]] kb::particles::ParticleRenderSnapshotResult PublishRenderSnapshot(
        kb::scene::Scene& scene,
        std::uint64_t fixedStepIndex,
        std::uint64_t revision) noexcept;
    [[nodiscard]] kb::particles::ParticleRenderSnapshotResult PublishRenderTombstone(
        kb::scene::Scene& scene,
        std::uint64_t fixedStepIndex,
        std::uint64_t revision) noexcept;
    [[nodiscard]] std::uint32_t InstanceParticleLimit(std::uint32_t denseIndex) const noexcept;
    [[nodiscard]] float EvaluateRate(const CompiledEmitter& emitter, float timeSeconds) const noexcept;
    [[nodiscard]] static float EvaluateCurve(const CompiledCurve& curve, float normalizedAge) noexcept;
    [[nodiscard]] static kb::math::Color EvaluateGradient(
        const CompiledGradient& gradient,
        float normalizedAge) noexcept;
    [[nodiscard]] float NextRandom01(InstanceRuntime& runtime) noexcept;
    [[nodiscard]] kb::math::Vec3 SampleInitialVelocity(
        const CompiledEmitter& emitter,
        InstanceRuntime& runtime) noexcept;

    bool warmedUp_ = false;
    std::uint32_t denseInstanceCount_ = 0U;
    std::array<std::uint32_t, kb::scene::kParticleEffectMaxInstancesPerScene> slotGenerations_{};
    std::array<std::uint32_t, kb::scene::kParticleEffectMaxInstancesPerScene> slotToDense_{};
    std::vector<std::uint32_t> denseToSlot_;
    std::vector<std::uint64_t> effectAssetIds_;
    std::vector<kb::scene::SceneEntity> owners_;
    std::vector<kb::scene::ParticleOwnerDeathPolicy> ownerDeathPolicies_;
    std::vector<std::uint8_t> ownerTerminalPending_;
    std::vector<float> rateMultipliers_;
    std::vector<std::uint32_t> maxParticlesOverrides_;
    std::vector<std::uint8_t> followTransforms_;
    std::vector<kb::scene::WorldTransform> ownerTransforms_;
    std::vector<std::uint8_t> reloadRestarted_;
    std::vector<std::uint64_t> seeds_;
    std::vector<PlaybackState> playbackStates_;
    std::vector<InstanceRuntime> instanceRuntime_;
    std::vector<CompiledEffect> compiledEffects_;
    std::vector<ParameterEntry> parameters_;

    std::vector<Command> commands_;
    std::vector<InternalEvent> currentEvents_;
    std::vector<InternalEvent> nextEvents_;
    std::vector<InternalEvent> prewarmCurrentEvents_;
    std::vector<InternalEvent> prewarmNextEvents_;
    std::uint64_t prewarmingInstanceId_ = 0U;

    std::vector<std::uint64_t> particleInstanceIds_;
    std::vector<std::uint64_t> particleIds_;
    std::vector<std::uint8_t> particleEmitterIndices_;
    std::vector<kb::math::Vec3> particlePositions_;
    std::vector<kb::math::Vec3> particlePreviousPositions_;
    std::vector<kb::math::Vec3> particleVelocities_;
    std::vector<float> particleAges_;
    std::vector<float> particleLifetimes_;
    std::vector<kb::math::Color> particleColors_;
    std::vector<float> particleSizes_;
    std::vector<std::uint8_t> particleEventDepths_;
    std::vector<std::uint8_t> particlePrewarmGroups_;
    std::vector<kb::particles::ParticleRenderEmitterRecord> renderEmitterScratch_;
    std::vector<kb::particles::ParticleRenderRecord> renderParticleScratch_;
    std::array<std::uint32_t, kb::scene::kParticleEffectMaxInstancesPerScene *
                                  kb::scene::kParticleEffectMaxEmitters>
        renderGroupCounts_{};
    std::array<std::uint32_t, kb::scene::kParticleEffectMaxInstancesPerScene *
                                  kb::scene::kParticleEffectMaxEmitters>
        renderGroupWriteOffsets_{};
    std::array<std::uint16_t, kb::scene::kParticleEffectMaxInstancesPerScene *
                                  kb::scene::kParticleEffectMaxEmitters>
        renderGroupRecordIndices_{};
    std::array<std::uint32_t, kb::scene::kParticleEffectMaxInstancesPerScene *
                                  kb::scene::kParticleEffectMaxEmitters>
        renderRejectedByCapacity_{};
    std::array<std::uint32_t, kb::scene::kParticleEffectMaxInstancesPerScene *
                                  kb::scene::kParticleEffectMaxEmitters>
        renderRejectedBySpawnBudget_{};
    std::array<std::uint32_t, kb::scene::kParticleEffectMaxInstancesPerScene *
                                  kb::scene::kParticleEffectMaxEmitters>
        renderRejectedByEventBudget_{};
    std::uint64_t nextParticleId_ = 1U;
    std::array<std::uint32_t, kb::scene::kParticleEffectMaxInstancesPerScene *
                                  kb::scene::kParticleEffectMaxEmitters *
                                  kb::scene::kParticleEffectMaxModulesPerEmitter>
        collisionEventsThisStep_{};
    bool eventQueueOverflowed_ = false;
    bool eventActionBudgetExceeded_ = false;
    StepTelemetry stepTelemetry_{};
};

} // namespace kb::particle_plugin
