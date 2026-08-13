#include "CpuParticleBackend.hpp"
#include "ParticleSceneSystem.hpp"

#include "engine/assets/AssetMetadata.hpp"
#include "engine/particles/ParticlePlayback.hpp"
#include "engine/scene/ParticleEffectAsset.hpp"
#include "engine/scene/ParticleEffectAssetIO.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneSystemContext.hpp"

#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::atomic<bool> g_countAllocations{ false };
std::atomic<std::size_t> g_allocationCount{ 0U };

void CountAllocation() noexcept {
    if (g_countAllocations.load(std::memory_order_relaxed)) {
        g_allocationCount.fetch_add(1U, std::memory_order_relaxed);
    }
}

void Require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error{ std::string{ message } };
}

struct Fixture {
    kb::scene::Scene scene;
    kb::scene::SceneEntity owner = scene.Entities().CreateEntity();
    std::uint64_t effectAssetId = 71U;

    static kb::scene::ParticleEffectAsset MakeEffect(
        float rate = 0.0F,
        std::uint32_t maxParticles = 256U) {
        kb::scene::ParticleEffectAsset effect;
        effect.effectId = 1U;
        effect.displayName = "CPU Kernel Effect";
        effect.recipeCategory = "Simple";
        effect.determinismSeed = 0x123456789ABCDEF0ULL;
        effect.durationSeconds = 5.0F;
        effect.looping = true;
        kb::scene::ParticleEmitterAsset emitter;
        emitter.emitterId = 1U;
        emitter.name = "Primary";
        emitter.maxParticles = maxParticles;
        emitter.spawn.rateOverTime.keyframes = {
            kb::math::CurveKeyframe{ .time = 0.0F, .value = rate },
        };
        emitter.spawn.lifetimeMin = 2.0F;
        emitter.spawn.lifetimeMax = 2.0F;
        emitter.spawn.speedMin = 1.0F;
        emitter.spawn.speedMax = 1.0F;
        emitter.spawn.direction = {1.0F, 0.0F, 0.0F};
        emitter.output.material.virtualPath = "/Game/Materials/ParticleTest.21kb";
        effect.emitters.push_back(std::move(emitter));
        return effect;
    }

    explicit Fixture(kb::scene::ParticleEffectAsset effect = MakeEffect()) {
        Require(scene.Assets().Manager().RegisterAsset({
                    .id = kb::assets::AssetId{ effectAssetId },
                    .type = kb::scene::kParticleEffectAssetType,
                    .name = "Lifecycle Effect",
                    .virtualPath = "/Game/Effects/Lifecycle.kbvfx",
                    .physicalPath = "Lifecycle.kbvfx",
                    .contentHash = 1U,
                }),
            "effect metadata registration failed");
        Require(scene.Assets().Manager().PublishRuntimeAsset(
                    kb::assets::AssetId{effectAssetId},
                    std::make_shared<kb::scene::ParticleEffectAsset>(std::move(effect))),
            "effect runtime payload publication failed");
    }
};

[[nodiscard]] std::uint64_t HashParticles(
    const kb::scene::Scene& scene,
    std::uint64_t instanceId) {
    const auto query = kb::particles::ParticlePlayback::Query(scene, instanceId);
    Require(query.Succeeded(), "snapshot query failed");
    const auto states = kb::particles::ParticlePlayback::LiveParticleStates(scene, instanceId);
    Require(states.size() == query.liveParticleCount,
        "snapshot live-state count mismatch");
    std::uint64_t hash = 1469598103934665603ULL;
    const auto mix = [&](std::uint32_t value) {
        for (std::uint32_t byte = 0U; byte < 4U; ++byte) {
            hash ^= static_cast<std::uint8_t>(value >> (byte * 8U));
            hash *= 1099511628211ULL;
        }
    };
    for (const auto& state : states) {
        mix(std::bit_cast<std::uint32_t>(state.position.x));
        mix(std::bit_cast<std::uint32_t>(state.position.y));
        mix(std::bit_cast<std::uint32_t>(state.position.z));
        mix(std::bit_cast<std::uint32_t>(state.velocity.x));
        mix(std::bit_cast<std::uint32_t>(state.velocity.y));
        mix(std::bit_cast<std::uint32_t>(state.velocity.z));
        mix(std::bit_cast<std::uint32_t>(state.age));
        mix(std::bit_cast<std::uint32_t>(state.lifetime));
    }
    return hash;
}

struct RuntimeFixture {
    Fixture fixture;
    kb::scene::SceneSystemHandle systemHandle{};
    std::uint64_t instanceId = 0U;

    explicit RuntimeFixture(kb::scene::ParticleEffectAsset effect)
        : fixture(std::move(effect)) {
        systemHandle = fixture.scene.Runtime().AddSceneSystem(
            std::make_unique<kb::particle_plugin::ParticleSceneSystem>());
        Require(systemHandle.IsValid(), "particle scene system registration failed");
        const auto created = kb::particles::ParticlePlayback::Create(
            fixture.scene, fixture.effectAssetId, fixture.owner);
        Require(created.Succeeded(), "runtime particle instance creation failed");
        instanceId = created.instanceId;
    }
};

[[nodiscard]] std::uint64_t RunFrameFeedHash(float frameDeltaSeconds) {
    auto effect = Fixture::MakeEffect(60.0F);
    effect.durationSeconds = 8.0F;
    RuntimeFixture runtime(std::move(effect));
    Require(kb::particles::ParticlePlayback::SetSeed(runtime.fixture.scene, runtime.instanceId,
                0xFEDCBA9876543210ULL).Succeeded(),
        "frame-feed seed failed");
    Require(kb::particles::ParticlePlayback::Play(runtime.fixture.scene, runtime.instanceId).Succeeded(),
        "frame-feed play failed");
    std::uint32_t frame = 0U;
    while (runtime.fixture.scene.Runtime().FixedStepIndex() < 120U && frame < 512U) {
        static_cast<void>(runtime.fixture.scene.Runtime().Update(frameDeltaSeconds));
        ++frame;
    }
    Require(runtime.fixture.scene.Runtime().FixedStepIndex() == 120U,
        "frame feed did not produce exactly the requested fixed-step count");
    return HashParticles(runtime.fixture.scene, runtime.instanceId);
}

void TestSharedFixedSchedulerDeterminism() {
    Fixture settingsFixture;
    const auto settings = settingsFixture.scene.Runtime().FixedStepSettings();
    Require(settings.fixedDeltaSeconds == kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds &&
            settings.maxFixedStepsPerFrame == kb::scene::kSceneRuntimeDefaultMaxFixedStepsPerFrame,
        "particle runtime defaults diverged from the authoritative 1/60 and eight-step contract");
    static_cast<void>(settingsFixture.scene.Runtime().AddSceneSystem(
        std::make_unique<kb::particle_plugin::ParticleSceneSystem>()));
    static_cast<void>(settingsFixture.scene.Runtime().Update(1.0F));
    Require(settingsFixture.scene.Runtime().LastFixedStepCount() ==
            kb::scene::kSceneRuntimeDefaultMaxFixedStepsPerFrame,
        "authoritative scene scheduler exceeded the eight-step catch-up ceiling");

    const std::uint64_t at30 = RunFrameFeedHash(1.0F / 30.0F);
    const std::uint64_t at60 = RunFrameFeedHash(1.0F / 60.0F);
    const std::uint64_t at144 = RunFrameFeedHash(1.0F / 144.0F);
    Require(at30 == at60 && at60 == at144,
        "equal authoritative fixed-step counts diverged across frame feeds");
}

void TestIndependentInstancesAndPrewarm() {
    auto effect = Fixture::MakeEffect(30.0F);
    effect.emitters[0].spawn.lifetimeMin = 5.0F;
    effect.emitters[0].spawn.lifetimeMax = 6.0F;
    effect.emitters[0].spawn.speedMin = 1.0F;
    effect.emitters[0].spawn.speedMax = 2.0F;
    RuntimeFixture runtime(effect);
    const auto second = kb::particles::ParticlePlayback::Create(
        runtime.fixture.scene, runtime.fixture.effectAssetId, runtime.fixture.owner);
    Require(second.Succeeded(), "second independent instance creation failed");
    Require(kb::particles::ParticlePlayback::SetSeed(runtime.fixture.scene, runtime.instanceId, 11U).Succeeded() &&
            kb::particles::ParticlePlayback::SetSeed(runtime.fixture.scene, second.instanceId, 22U).Succeeded(),
        "independent seed setup failed");
    Require(kb::particles::ParticlePlayback::Play(runtime.fixture.scene, runtime.instanceId).Succeeded() &&
            kb::particles::ParticlePlayback::Play(runtime.fixture.scene, second.instanceId).Succeeded(),
        "independent play failed");
    for (std::uint32_t step = 0U; step < 60U; ++step) {
        static_cast<void>(runtime.fixture.scene.Runtime().Update(1.0F / 60.0F));
    }
    Require(HashParticles(runtime.fixture.scene, runtime.instanceId) !=
            HashParticles(runtime.fixture.scene, second.instanceId),
        "independent instance seeds shared RNG state");
    Require(kb::particles::ParticlePlayback::Pause(runtime.fixture.scene, runtime.instanceId).Succeeded(),
        "independent pause failed");
    const auto pausedBefore = kb::particles::ParticlePlayback::Query(runtime.fixture.scene, runtime.instanceId);
    for (std::uint32_t step = 0U; step < 30U; ++step) {
        static_cast<void>(runtime.fixture.scene.Runtime().Update(1.0F / 60.0F));
    }
    const auto pausedAfter = kb::particles::ParticlePlayback::Query(runtime.fixture.scene, runtime.instanceId);
    const auto runningAfter = kb::particles::ParticlePlayback::Query(runtime.fixture.scene, second.instanceId);
    Require(pausedBefore.liveParticleCount == pausedAfter.liveParticleCount &&
            runningAfter.liveParticleCount > pausedAfter.liveParticleCount,
        "one instance pause affected another instance or advanced paused particles");

    auto prewarmedEffect = effect;
    prewarmedEffect.emitters[0].spawn.prewarmSeconds = 1.0F;
    RuntimeFixture prewarmed(std::move(prewarmedEffect));
    RuntimeFixture manual(std::move(effect));
    Require(kb::particles::ParticlePlayback::SetSeed(prewarmed.fixture.scene, prewarmed.instanceId, 91U).Succeeded() &&
            kb::particles::ParticlePlayback::SetSeed(manual.fixture.scene, manual.instanceId, 91U).Succeeded(),
        "prewarm seed setup failed");
    Require(kb::particles::ParticlePlayback::Play(prewarmed.fixture.scene, prewarmed.instanceId).Succeeded() &&
            kb::particles::ParticlePlayback::Play(manual.fixture.scene, manual.instanceId).Succeeded(),
        "prewarm play failed");
    for (std::uint32_t step = 0U; step < 60U; ++step) {
        static_cast<void>(manual.fixture.scene.Runtime().Update(1.0F / 60.0F));
    }
    Require(HashParticles(prewarmed.fixture.scene, prewarmed.instanceId) ==
            HashParticles(manual.fixture.scene, manual.instanceId),
        "prewarm did not use the fixed-step simulation kernel");

    auto twoEmitterManual = Fixture::MakeEffect(60.0F);
    twoEmitterManual.emitters[0].spawn.lifetimeMin = 5.0F;
    twoEmitterManual.emitters[0].spawn.lifetimeMax = 5.0F;
    kb::scene::ParticleEmitterAsset secondEmitter = twoEmitterManual.emitters[0];
    secondEmitter.emitterId = 2U;
    secondEmitter.name = "Secondary";
    secondEmitter.localPosition = {0.0F, 3.0F, 0.0F};
    twoEmitterManual.emitters.push_back(secondEmitter);
    auto twoEmitterPrewarmed = twoEmitterManual;
    twoEmitterPrewarmed.emitters[0].spawn.prewarmSeconds = 1.0F;
    twoEmitterPrewarmed.emitters[1].spawn.prewarmSeconds = 0.5F;
    RuntimeFixture multiPrewarmed(std::move(twoEmitterPrewarmed));
    Require(kb::particles::ParticlePlayback::SetSeed(
                multiPrewarmed.fixture.scene, multiPrewarmed.instanceId, 177U).Succeeded(),
        "two-emitter prewarm seed setup failed");
    Require(kb::particles::ParticlePlayback::Play(
                multiPrewarmed.fixture.scene, multiPrewarmed.instanceId).Succeeded(),
        "two-emitter prewarm play failed");
    kb::particle_plugin::CpuParticleBackend emitterZeroManual;
    kb::particle_plugin::CpuParticleBackend emitterOneManual;
    Fixture firstOnly(Fixture::MakeEffect(60.0F));
    auto secondOnlyAsset = Fixture::MakeEffect(60.0F);
    secondOnlyAsset.emitters[0].localPosition = {0.0F, 3.0F, 0.0F};
    Fixture secondOnly(std::move(secondOnlyAsset));
    emitterZeroManual.Warmup();
    emitterOneManual.Warmup();
    const auto firstManual = emitterZeroManual.Create(firstOnly.scene, firstOnly.effectAssetId, firstOnly.owner);
    const auto secondManual = emitterOneManual.Create(secondOnly.scene, secondOnly.effectAssetId, secondOnly.owner);
    Require(firstManual.Succeeded() && secondManual.Succeeded() &&
            emitterZeroManual.SetSeed(firstOnly.scene, firstManual.instanceId, 177U).Succeeded() &&
            emitterOneManual.SetSeed(secondOnly.scene, secondManual.instanceId, 177U).Succeeded() &&
            emitterZeroManual.Play(firstOnly.scene, firstManual.instanceId).Succeeded() &&
            emitterOneManual.Play(secondOnly.scene, secondManual.instanceId).Succeeded(),
        "two-emitter manual parity fixture setup failed");
    for (std::uint32_t step = 0U; step < 60U; ++step) {
        Require(emitterZeroManual.Step(firstOnly.scene,
                    kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds).Succeeded(),
            "first emitter manual prewarm step failed");
    }
    for (std::uint32_t step = 0U; step < 30U; ++step) {
        Require(emitterOneManual.Step(secondOnly.scene,
                    kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds).Succeeded(),
            "second emitter manual prewarm step failed");
    }
    const auto multiStates = kb::particles::ParticlePlayback::LiveParticleStates(
        multiPrewarmed.fixture.scene, multiPrewarmed.instanceId);
    std::size_t firstCount = 0U;
    std::size_t secondCount = 0U;
    for (const auto& state : multiStates) {
        if (state.position.y == 0.0F) ++firstCount;
        if (state.position.y == 3.0F) ++secondCount;
    }
    Require(firstCount == emitterZeroManual.Query(firstOnly.scene, firstManual.instanceId).liveParticleCount &&
            secondCount == emitterOneManual.Query(secondOnly.scene, secondManual.instanceId).liveParticleCount,
        "two-emitter authored prewarm did not match independent manual fixed-step counts");
}

void TestBurstLifetimeDurationLoopAndCapacity() {
    auto effect = Fixture::MakeEffect(0.0F, 4U);
    effect.looping = false;
    effect.durationSeconds = 0.1F;
    effect.emitters[0].spawn.mode = kb::scene::ParticleSpawnMode::Burst;
    effect.emitters[0].spawn.bursts = {{.timeSeconds = 0.0F, .count = 2U}};
    effect.emitters[0].spawn.lifetimeMin = 0.05F;
    effect.emitters[0].spawn.lifetimeMax = 0.05F;
    RuntimeFixture runtime(std::move(effect));
    Require(kb::particles::ParticlePlayback::Play(runtime.fixture.scene, runtime.instanceId).Succeeded(),
        "burst fixture play failed");
    static_cast<void>(runtime.fixture.scene.Runtime().Update(1.0F / 60.0F));
    Require(kb::particles::ParticlePlayback::Query(runtime.fixture.scene, runtime.instanceId).liveParticleCount == 2U,
        "time-zero authored burst did not fire exactly once");
    for (std::uint32_t step = 0U; step < 6U; ++step) {
        static_cast<void>(runtime.fixture.scene.Runtime().Update(1.0F / 60.0F));
    }
    const auto drained = kb::particles::ParticlePlayback::Query(runtime.fixture.scene, runtime.instanceId);
    Require(drained.liveParticleCount == 0U && !drained.state,
        "finite duration did not stop after natural lifetime drain");

    auto looping = Fixture::MakeEffect(0.0F, 4U);
    looping.looping = true;
    looping.durationSeconds = 0.05F;
    looping.emitters[0].spawn.mode = kb::scene::ParticleSpawnMode::Burst;
    looping.emitters[0].spawn.bursts = {{.timeSeconds = 0.0F, .count = 1U}};
    looping.emitters[0].spawn.lifetimeMin = 1.0F;
    looping.emitters[0].spawn.lifetimeMax = 1.0F;
    RuntimeFixture loopRuntime(std::move(looping));
    Require(kb::particles::ParticlePlayback::Play(loopRuntime.fixture.scene, loopRuntime.instanceId).Succeeded(),
        "loop fixture play failed");
    for (std::uint32_t step = 0U; step < 7U; ++step) {
        static_cast<void>(loopRuntime.fixture.scene.Runtime().Update(1.0F / 60.0F));
    }
    Require(kb::particles::ParticlePlayback::Query(loopRuntime.fixture.scene, loopRuntime.instanceId)
                .liveParticleCount == 3U,
        "loop boundary did not repeat the authored burst deterministically");

    Fixture capacityFixture(Fixture::MakeEffect(0.0F, 4U));
    kb::particle_plugin::CpuParticleBackend backend;
    backend.Warmup();
    const auto created = backend.Create(capacityFixture.scene, capacityFixture.effectAssetId, capacityFixture.owner);
    Require(created.Succeeded(), "capacity fixture create failed");
    Require(backend.Emit(capacityFixture.scene, created.instanceId, 4U).Succeeded(),
        "particle capacity boundary emit failed");
    Require(backend.Emit(capacityFixture.scene, created.instanceId, 1U).status ==
            kb::particles::ParticleRuntimeStatus::ParticleCapacityReached,
        "particle capacity boundary plus one was accepted");
    const auto telemetry = backend.LastStepTelemetry();
    Require(telemetry.requestedSpawns == 1U && telemetry.spawned == 0U &&
            telemetry.rejectedByCapacity == 1U,
        "capacity rejection telemetry was not exact");
    Require(backend.Emit(capacityFixture.scene, created.instanceId,
                kb::scene::kParticleEffectMaxSpawnsPerStep + 1U).status ==
            kb::particles::ParticleRuntimeStatus::SpawnBudgetExceeded,
        "spawn-per-step boundary plus one was accepted");

    auto drainEffect = Fixture::MakeEffect(0.0F, 2U);
    drainEffect.emitters[0].spawn.lifetimeMin = 0.05F;
    drainEffect.emitters[0].spawn.lifetimeMax = 0.05F;
    Fixture drainFixture(std::move(drainEffect));
    kb::particle_plugin::CpuParticleBackend drainBackend;
    drainBackend.Warmup();
    const auto draining = drainBackend.Create(drainFixture.scene, drainFixture.effectAssetId, drainFixture.owner);
    Require(draining.Succeeded() && drainBackend.Emit(drainFixture.scene, draining.instanceId, 1U).Succeeded(),
        "stop-drain fixture setup failed");
    Require(drainBackend.Stop(drainFixture.scene, draining.instanceId).Succeeded() &&
            drainBackend.Query(drainFixture.scene, draining.instanceId).liveParticleCount == 1U,
        "stop cleared a live particle instead of draining its lifetime");
    for (std::uint32_t step = 0U; step < 4U; ++step) {
        Require(drainBackend.Step(drainFixture.scene,
                    kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds).Succeeded(),
            "stop-drain fixed step failed");
    }
    Require(drainBackend.Query(drainFixture.scene, draining.instanceId).liveParticleCount == 0U,
        "stopped instance did not drain its live particle naturally");
}

void TestGlobalCapacityAndCompileRejection() {
    Fixture fixture(Fixture::MakeEffect(0.0F, kb::scene::kParticleEffectMaxCpuParticlesPerEmitter));
    kb::particle_plugin::CpuParticleBackend backend;
    backend.Warmup();
    std::array<std::uint64_t, 5U> ids{};
    for (std::uint32_t index = 0U; index < 4U; ++index) {
        const auto created = backend.Create(fixture.scene, fixture.effectAssetId, fixture.owner);
        Require(created.Succeeded(), "global capacity instance creation failed");
        ids[index] = created.instanceId;
        Require(backend.Emit(fixture.scene, ids[index],
                    kb::scene::kParticleEffectMaxCpuParticlesPerEmitter).Succeeded(),
            "global capacity boundary fill failed");
    }
    const auto extra = backend.Create(fixture.scene, fixture.effectAssetId, fixture.owner);
    Require(extra.Succeeded(), "global capacity probe instance creation failed");
    Require(backend.Query(fixture.scene, ids[0]).liveParticleCount ==
            kb::scene::kParticleEffectMaxCpuParticlesPerEmitter,
        "emitter capacity boundary was not retained");
    Require(backend.Emit(fixture.scene, extra.instanceId, 1U).status ==
            kb::particles::ParticleRuntimeStatus::ParticleCapacityReached,
        "CPU scene particle capacity boundary plus one was accepted");

    auto excessivePrewarm = Fixture::MakeEffect();
    excessivePrewarm.emitters[0].spawn.prewarmSeconds =
        kb::scene::kParticleEffectMaxPrewarmSeconds + 1.0F;
    Fixture invalidFixture(std::move(excessivePrewarm));
    kb::particle_plugin::CpuParticleBackend invalidBackend;
    invalidBackend.Warmup();
    Require(invalidBackend.Create(invalidFixture.scene, invalidFixture.effectAssetId,
                invalidFixture.owner).status == kb::particles::ParticleRuntimeStatus::InvalidAsset,
        "prewarm beyond the explicit step budget was silently clamped");

    auto excessiveRate = Fixture::MakeEffect(kb::scene::kParticleEffectMaxContinuousRatePerSecond + 1.0F);
    Fixture invalidRateFixture(std::move(excessiveRate));
    kb::particle_plugin::CpuParticleBackend invalidRateBackend;
    invalidRateBackend.Warmup();
    Require(invalidRateBackend.Create(invalidRateFixture.scene, invalidRateFixture.effectAssetId,
                invalidRateFixture.owner).status == kb::particles::ParticleRuntimeStatus::InvalidAsset,
        "continuous rate beyond the per-step spawn ceiling was accepted");
}

void TestNoAllocationPerFixedStep() {
    Fixture fixture(Fixture::MakeEffect(60.0F));
    kb::particle_plugin::CpuParticleBackend backend;
    backend.Warmup();
    const auto created = backend.Create(fixture.scene, fixture.effectAssetId, fixture.owner);
    Require(created.Succeeded() && backend.Play(fixture.scene, created.instanceId).Succeeded(),
        "fixed-step allocation fixture setup failed");
    Require(backend.Step(fixture.scene, kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds).Succeeded(),
        "fixed-step allocation fixture warm step failed");
    g_allocationCount.store(0U, std::memory_order_relaxed);
    g_countAllocations.store(true, std::memory_order_release);
    bool succeeded = true;
    for (std::uint32_t step = 0U; step < 120U; ++step) {
        succeeded = succeeded &&
            backend.Step(fixture.scene, kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds).Succeeded();
    }
    g_countAllocations.store(false, std::memory_order_release);
    Require(succeeded, "fixed-step allocation measurement step failed");
    Require(g_allocationCount.load(std::memory_order_relaxed) == 0U,
        "fixed-step simulation allocated after explicit warmup and compile");
}

void TestValidationAndLifecycle() {
    Fixture fixture;
    kb::particle_plugin::CpuParticleBackend backend;
    Require(!backend.IsWarmedUp(), "backend unexpectedly started warm");
    Require(backend.Create(fixture.scene, fixture.effectAssetId, fixture.owner).status ==
            kb::particles::ParticleRuntimeStatus::InvalidRequest,
        "create before warmup was accepted");
    backend.Warmup();
    backend.Warmup();
    Require(backend.ParticleCapacity() == kb::scene::kParticleEffectMaxCpuParticlesPerScene,
        "particle SoA capacity diverged from the schema ceiling");
    Require(backend.CommandCapacity() == kb::scene::kParticleEffectMaxCommandsPerStep &&
            backend.EventCapacity() == kb::scene::kParticleEffectMaxEventsPerStep,
        "buffer capacities diverged from schema ceilings");

    Require(backend.Create(fixture.scene, 999U, fixture.owner).status ==
            kb::particles::ParticleRuntimeStatus::InvalidAsset,
        "unregistered effect asset was accepted");
    Require(backend.Create(fixture.scene, fixture.effectAssetId, {}).status ==
            kb::particles::ParticleRuntimeStatus::InvalidOwner,
        "invalid owner was accepted");

    const auto created = backend.Create(fixture.scene, fixture.effectAssetId, fixture.owner);
    Require(created.Succeeded() && created.instanceId != 0U, "valid instance creation failed");
    Require(!backend.Query(fixture.scene, created.instanceId).state, "new instance did not start stopped");
    Require(backend.Pause(fixture.scene, created.instanceId).status ==
            kb::particles::ParticleRuntimeStatus::InvalidRequest,
        "stopped instance paused successfully");
    Require(backend.Play(fixture.scene, created.instanceId).Succeeded() &&
            backend.Query(fixture.scene, created.instanceId).state,
        "play did not enter playing state");
    Require(backend.Pause(fixture.scene, created.instanceId).Succeeded() &&
            !backend.Query(fixture.scene, created.instanceId).state,
        "pause did not leave playing state");
    Require(backend.Restart(fixture.scene, created.instanceId).Succeeded() &&
            backend.Query(fixture.scene, created.instanceId).state,
        "restart did not enter playing state");
    Require(backend.Stop(fixture.scene, created.instanceId).Succeeded() &&
            !backend.Query(fixture.scene, created.instanceId).state,
        "stop did not enter stopped state");
    Require(backend.SetSeed(fixture.scene, created.instanceId, UINT64_MAX).Succeeded(),
        "full-width seed was rejected");
    Require(backend.SetParameterScalar(fixture.scene, created.instanceId, "rate", 2.0F).Succeeded(),
        "valid scalar override was rejected");
    Require(backend.SetParameterScalar(fixture.scene, created.instanceId, "rate", 3.0F).Succeeded(),
        "existing scalar override could not be updated");
    Require(backend.SetParameterScalar(fixture.scene, created.instanceId, "bad",
                std::numeric_limits<float>::infinity()).status ==
            kb::particles::ParticleRuntimeStatus::InvalidParameter,
        "non-finite scalar override was accepted");
    Require(backend.ClearParameter(fixture.scene, created.instanceId, "missing").status ==
            kb::particles::ParticleRuntimeStatus::InvalidParameter,
        "missing scalar override cleared successfully");
    Require(backend.ClearParameter(fixture.scene, created.instanceId, "rate").Succeeded(),
        "existing scalar override could not be cleared");
    std::array<std::array<char, 4U>, kb::scene::kParticleEffectMaxRuntimeParametersPerInstance + 1U> parameterNames{};
    for (std::size_t index = 0U; index < parameterNames.size(); ++index) {
        parameterNames[index] = { 'v', static_cast<char>('A' + index / 10U),
            static_cast<char>('0' + index % 10U), '\0' };
    }
    for (std::size_t index = 0U; index < kb::scene::kParticleEffectMaxRuntimeParametersPerInstance; ++index) {
        Require(backend.SetParameterScalar(fixture.scene, created.instanceId,
                    std::string_view{ parameterNames[index].data(), 3U }, static_cast<float>(index)).Succeeded(),
            "scalar override boundary entry was rejected");
    }
    Require(backend.SetParameterScalar(fixture.scene, created.instanceId,
                std::string_view{ parameterNames.back().data(), 3U }, 33.0F).status ==
            kb::particles::ParticleRuntimeStatus::InvalidParameter,
        "scalar override boundary plus one was accepted");
    Require(backend.Emit(fixture.scene, created.instanceId, 0U).status ==
            kb::particles::ParticleRuntimeStatus::InvalidRequest,
        "zero-count emit did not fail validation");
    Require(backend.Emit(fixture.scene, created.instanceId, 1U).Succeeded() &&
            backend.Query(fixture.scene, created.instanceId).liveParticleCount == 1U,
        "real kernel emit did not create one live particle");
    Require(backend.BufferedCommandCount() == 0U && backend.BufferedEventCount() == 0U,
        "synchronous lifecycle left buffered work behind");
}

void TestCapacityGenerationAndCopyBounds() {
    Fixture fixture;
    kb::particle_plugin::CpuParticleBackend backend;
    backend.Warmup();
    std::array<std::uint64_t, kb::scene::kParticleEffectMaxInstancesPerScene> ids{};
    for (std::size_t index = 0U; index < ids.size(); ++index) {
        const auto created = backend.Create(fixture.scene, fixture.effectAssetId, fixture.owner);
        Require(created.Succeeded(), "instance boundary create failed");
        ids[index] = created.instanceId;
    }
    Require(backend.LiveInstanceCount() == kb::scene::kParticleEffectMaxInstancesPerScene,
        "dense storage did not reach its schema boundary");
    Require(backend.Create(fixture.scene, fixture.effectAssetId, fixture.owner).status ==
            kb::particles::ParticleRuntimeStatus::InstanceLimitReached,
        "instance boundary plus one was accepted");

    std::array<std::uint64_t, kb::scene::kParticleEffectMaxInstancesPerScene + 1U> copiedIds{};
    copiedIds.back() = UINT64_MAX;
    Require(backend.CopyLiveInstanceIds(fixture.scene, std::span{ copiedIds }.first(ids.size())) == ids.size(),
        "live instance copy reported the wrong total");
    Require(copiedIds.back() == UINT64_MAX, "live instance copy wrote past the caller span");

    const std::uint64_t stale = ids[17U];
    Require(backend.Release(fixture.scene, stale).Succeeded(), "instance release failed");
    Require(backend.Query(fixture.scene, stale).status == kb::particles::ParticleRuntimeStatus::InvalidInstance,
        "released handle remained queryable");
    const auto replacement = backend.Create(fixture.scene, fixture.effectAssetId, fixture.owner);
    Require(replacement.Succeeded() && replacement.instanceId != stale,
        "reused slot did not advance its generation");
    Require(backend.Play(fixture.scene, stale).status == kb::particles::ParticleRuntimeStatus::InvalidInstance,
        "stale handle controlled its replacement");

    std::array<kb::particles::ParticleRuntimeState, 1U> states{};
    states[0].age = 91.0F;
    Require(backend.CopyLiveParticleStates(fixture.scene, replacement.instanceId, {}) == 0U,
        "pre-kernel instance reported live particles");
    Require(backend.CopyLiveParticleStates(fixture.scene, replacement.instanceId, states) == 0U &&
            states[0].age == 91.0F,
        "empty live-particle copy modified caller storage");
    Require(backend.CopyLiveParticleStates(fixture.scene, stale, states) == 0U,
        "stale handle exposed live-particle storage");
}

void TestRegistrationOwnershipAndCycles() {
    Fixture fixture;
    kb::particle_plugin::CpuParticleBackend conflicting;
    conflicting.Warmup();
    Require(kb::particles::ParticlePlayback::RegisterBackend(fixture.scene, conflicting).Succeeded(),
        "conflict fixture could not register");
    kb::particle_plugin::ParticleSceneSystem system;
    kb::scene::SceneSystemContext context{ fixture.scene, 0.0F };
    bool rejected = false;
    try {
        system.OnCreate(context);
    } catch (const std::logic_error&) {
        rejected = true;
    }
    Require(rejected && kb::particles::ParticlePlayback::HasBackend(fixture.scene),
        "scene system replaced a conflicting backend");
    Require(kb::particles::ParticlePlayback::UnregisterBackend(fixture.scene, conflicting).Succeeded(),
        "conflict fixture could not unregister");

    for (std::size_t cycle = 0U; cycle < 100U; ++cycle) {
        system.OnCreate(context);
        Require(kb::particles::ParticlePlayback::HasBackend(fixture.scene),
            "scene system did not own a backend after attach");
        system.OnDestroy(context);
        Require(!kb::particles::ParticlePlayback::HasBackend(fixture.scene),
            "scene system backend survived detach");
        Require(kb::particles::ParticlePlayback::DrainEvents(fixture.scene).empty(),
            "backend cycle left core events queued");
    }
}

void TestNoAllocationAfterWarmup() {
    Fixture fixture;
    kb::particle_plugin::CpuParticleBackend backend;
    backend.Warmup();
    std::array<std::array<char, 4U>, kb::scene::kParticleEffectMaxRuntimeParametersPerInstance> names{};
    for (std::size_t index = 0U; index < names.size(); ++index) {
        names[index] = { 'p', static_cast<char>('A' + index / 10U), static_cast<char>('0' + index % 10U), '\0' };
    }
    std::array<std::uint64_t, kb::scene::kParticleEffectMaxInstancesPerScene> copied{};
    std::array<kb::particles::ParticleRuntimeState, 1U> states{};
    const auto created = backend.Create(fixture.scene, fixture.effectAssetId, fixture.owner);
    Require(created.Succeeded(), "allocation fixture create failed");

    g_allocationCount.store(0U, std::memory_order_relaxed);
    g_countAllocations.store(true, std::memory_order_release);
    bool succeeded = true;
    succeeded = succeeded && backend.Play(fixture.scene, created.instanceId).Succeeded();
    succeeded = succeeded && backend.Pause(fixture.scene, created.instanceId).Succeeded();
    succeeded = succeeded && backend.Restart(fixture.scene, created.instanceId).Succeeded();
    succeeded = succeeded && backend.SetSeed(fixture.scene, created.instanceId, UINT64_MAX).Succeeded();
    for (std::size_t index = 0U; index < names.size(); ++index) {
        succeeded = succeeded && backend.SetParameterScalar(fixture.scene, created.instanceId,
            std::string_view{ names[index].data(), 3U }, static_cast<float>(index)).Succeeded();
    }
    succeeded = succeeded && backend.Query(fixture.scene, created.instanceId).Succeeded();
    succeeded = succeeded && backend.CopyLiveInstanceIds(fixture.scene, copied) == 1U;
    succeeded = succeeded && backend.CopyLiveParticleStates(fixture.scene, created.instanceId, states) == 0U;
    for (const auto& name : names) {
        succeeded = succeeded && backend.ClearParameter(fixture.scene, created.instanceId,
            std::string_view{ name.data(), 3U }).Succeeded();
    }
    succeeded = succeeded && backend.Stop(fixture.scene, created.instanceId).Succeeded();
    succeeded = succeeded && backend.Release(fixture.scene, created.instanceId).Succeeded();
    g_countAllocations.store(false, std::memory_order_release);

    Require(succeeded, "allocation measurement lifecycle operation failed");
    Require(g_allocationCount.load(std::memory_order_relaxed) == 0U,
        "lifecycle command/query path allocated after explicit warmup");
}

} // namespace

void* operator new(std::size_t size) {
    CountAllocation();
    if (void* memory = std::malloc(size == 0U ? 1U : size)) return memory;
    throw std::bad_alloc{};
}

void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

int main() {
    try {
        TestValidationAndLifecycle();
        TestCapacityGenerationAndCopyBounds();
        TestRegistrationOwnershipAndCycles();
        TestNoAllocationAfterWarmup();
        TestSharedFixedSchedulerDeterminism();
        TestIndependentInstancesAndPrewarm();
        TestBurstLifetimeDurationLoopAndCapacity();
        TestGlobalCapacityAndCompileRejection();
        TestNoAllocationPerFixedStep();
        std::cout << "21kb Particle System CPU backend tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        g_countAllocations.store(false, std::memory_order_release);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
