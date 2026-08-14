#include "CpuParticleBackend.hpp"
#include "ParticleSceneSystem.hpp"

#include "engine/assets/AssetMetadata.hpp"
#include "engine/particles/ParticlePlayback.hpp"
#include "engine/scene/ParticleEffectAsset.hpp"
#include "engine/scene/ParticleEffectAssetIO.hpp"
#include "engine/scene/ParticleEffectAssetMigration.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneSystemContext.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <algorithm>
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

template <typename Payload>
void AddModule(kb::scene::ParticleEmitterAsset& emitter,
               kb::scene::ParticleStableId moduleId,
               kb::scene::ParticleModuleType type,
               Payload payload,
               bool enabled = true) {
    emitter.modules.push_back(kb::scene::ParticleModuleAsset{
        .moduleId = moduleId,
        .type = type,
        .enabled = enabled,
        .payload = std::move(payload),
    });
}

[[nodiscard]] std::uint64_t HashStateSpan(std::span<const kb::particles::ParticleRuntimeState> states) {
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

[[nodiscard]] std::uint64_t HashVisualStateSpan(std::span<const kb::particles::ParticleRuntimeState> states) {
    std::uint64_t hash = HashStateSpan(states);
    const auto mix = [&](float value) {
        const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
        for (std::uint32_t byte = 0U; byte < 4U; ++byte) {
            hash ^= (bits >> (byte * 8U)) & 0xFFU;
            hash *= 1099511628211ULL;
        }
    };
    for (const auto& state : states) {
        mix(state.color.r);
        mix(state.color.g);
        mix(state.color.b);
        mix(state.color.a);
        mix(state.size);
    }
    return hash;
}

[[nodiscard]] std::uint64_t HashParticles(
    const kb::scene::Scene& scene,
    std::uint64_t instanceId) {
    const auto query = kb::particles::ParticlePlayback::Query(scene, instanceId);
    Require(query.Succeeded(), "snapshot query failed");
    const auto states = kb::particles::ParticlePlayback::LiveParticleStates(scene, instanceId);
    Require(states.size() == query.liveParticleCount,
        "snapshot live-state count mismatch");
    return HashStateSpan(states);
}

[[nodiscard]] kb::scene::ParticleEffectAsset MakeFourModuleEffect(float prewarmSeconds = 0.0F) {
    auto effect = Fixture::MakeEffect(60.0F, 4096U);
    effect.durationSeconds = 8.0F;
    kb::scene::ParticleEmitterAsset& emitter = effect.emitters[0];
    emitter.spawn.prewarmSeconds = prewarmSeconds;
    emitter.spawn.lifetimeMin = 6.0F;
    emitter.spawn.lifetimeMax = 7.0F;
    AddModule(emitter, 1U, kb::scene::ParticleModuleType::InitialVelocity,
        kb::scene::ParticleInitialVelocityModule{
            .direction = {0.0F, 1.0F, 0.0F},
            .speedMin = 2.0F,
            .speedMax = 5.0F,
            .randomization = 1.0F,
            .spreadDegrees = 45.0F,
        });
    AddModule(emitter, 2U, kb::scene::ParticleModuleType::Gravity,
        kb::scene::ParticleGravityModule{.acceleration = {}, .sceneGravityScale = 0.25F});
    AddModule(emitter, 3U, kb::scene::ParticleModuleType::Wind,
        kb::scene::ParticleWindModule{.acceleration = {0.75F, 0.0F, -0.5F}});
    AddModule(emitter, 4U, kb::scene::ParticleModuleType::Drag,
        kb::scene::ParticleDragModule{.coefficient = 0.35F});
    return effect;
}

[[nodiscard]] kb::scene::ParticleEffectAsset MakeVisualModuleEffect(float prewarmSeconds = 0.0F) {
    auto effect = MakeFourModuleEffect(prewarmSeconds);
    kb::scene::ParticleEmitterAsset& emitter = effect.emitters[0];
    AddModule(emitter, 5U, kb::scene::ParticleModuleType::ColorOverLife,
        kb::scene::ParticleColorOverLifeModule{.gradient = {.stops = {
            {.time = 0.0F, .color = {1.0F, 0.2F, 0.0F, 0.8F}},
            {.time = 0.5F, .color = {0.4F, 0.8F, 0.2F, 0.6F}},
            {.time = 1.0F, .color = {0.0F, 0.1F, 1.0F, 0.3F}},
        }}});
    AddModule(emitter, 6U, kb::scene::ParticleModuleType::SizeOverLife,
        kb::scene::ParticleSizeOverLifeModule{.curve = {.keyframes = {
            {.time = 0.0F, .value = 0.5F, .easing = kb::math::Easing::Linear},
            {.time = 0.5F, .value = 2.0F, .easing = kb::math::Easing::InQuad},
            {.time = 1.0F, .value = 0.25F, .easing = kb::math::Easing::Linear},
        }}});
    AddModule(emitter, 7U, kb::scene::ParticleModuleType::AlphaOverLife,
        kb::scene::ParticleAlphaOverLifeModule{.curve = {.keyframes = {
            {.time = 0.0F, .value = 0.25F, .easing = kb::math::Easing::OutQuad},
            {.time = 1.0F, .value = 1.0F, .easing = kb::math::Easing::Linear},
        }}});
    return effect;
}

[[nodiscard]] kb::scene::ParticleEffectAsset MakeInternalEventEffect(float prewarmSeconds = 0.0F) {
    auto effect = MakeVisualModuleEffect(prewarmSeconds);
    kb::scene::ParticleEmitterAsset target = effect.emitters[0];
    target.emitterId = 2U;
    target.name = "InternalTarget";
    target.localPosition = {0.0F, 5.0F, 0.0F};
    target.spawn.prewarmSeconds = 0.0F;
    target.spawn.rateOverTime.keyframes.front().value = 0.0F;
    target.modules.clear();
    AddModule(effect.emitters[0], 8U, kb::scene::ParticleModuleType::SubEmitter,
        kb::scene::ParticleSubEmitterModule{
            .targetEmitterId = 2U,
            .trigger = kb::scene::ParticleEventTrigger::Birth,
            .count = 1U,
            .maxDepth = 1U,
        });
    effect.emitters.push_back(std::move(target));
    return effect;
}

[[nodiscard]] kb::scene::ParticleEffectAsset MakeCollisionBindingEffect(
    float rate,
    float prewarmSeconds = 0.0F,
    std::uint32_t perStepBudget = 64U) {
    auto effect = Fixture::MakeEffect(rate, 256U);
    auto& source = effect.emitters[0];
    source.localPosition = {0.0F, -0.1F, 0.0F};
    source.spawn.prewarmSeconds = prewarmSeconds;
    source.spawn.speedMin = 0.0F;
    source.spawn.speedMax = 0.0F;
    source.spawn.lifetimeMin = 10.0F;
    source.spawn.lifetimeMax = 10.0F;
    AddModule(source, 1U, kb::scene::ParticleModuleType::CollisionPlane,
        kb::scene::ParticleCollisionPlaneModule{.normal = {0.0F, 1.0F, 0.0F}, .distance = 0.0F,
            .restitution = 0.0F, .friction = 0.0F,
            .maxEventsPerStep = kb::scene::kParticleEffectMaxEventsPerStep});
    auto target = source;
    target.emitterId = 2U;
    target.name = "CollisionTarget";
    target.localPosition = {0.0F, 3.0F, 0.0F};
    target.spawn.prewarmSeconds = 0.0F;
    target.spawn.rateOverTime.keyframes.front().value = 0.0F;
    target.modules.clear();
    effect.emitters.push_back(std::move(target));
    effect.eventBindings.push_back({.sourceEmitterId = 1U,
        .trigger = kb::scene::ParticleEventTrigger::Collision, .sourceModuleId = 1U,
        .action = kb::scene::ParticleEventAction::EmitTargetEmitter,
        .targetEmitterId = 2U, .count = 1U, .maxDepth = 1U, .perStepBudget = perStepBudget});
    return effect;
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

[[nodiscard]] std::uint64_t RunModuleFrameFeedHash(float frameDeltaSeconds) {
    RuntimeFixture runtime(MakeInternalEventEffect());
    Require(kb::particles::ParticlePlayback::SetSeed(runtime.fixture.scene, runtime.instanceId,
                0xA55A1234FEDC9876ULL).Succeeded() &&
            kb::particles::ParticlePlayback::Play(runtime.fixture.scene, runtime.instanceId).Succeeded(),
        "module frame-feed setup failed");
    std::uint32_t frames = 0U;
    while (runtime.fixture.scene.Runtime().FixedStepIndex() < 120U && frames < 512U) {
        static_cast<void>(runtime.fixture.scene.Runtime().Update(frameDeltaSeconds));
        ++frames;
    }
    Require(runtime.fixture.scene.Runtime().FixedStepIndex() == 120U,
        "module frame feed did not produce exactly 120 fixed steps");
    return HashVisualStateSpan(
        kb::particles::ParticlePlayback::LiveParticleStates(runtime.fixture.scene, runtime.instanceId));
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

    const std::uint64_t modulesAt30 = RunModuleFrameFeedHash(1.0F / 30.0F);
    const std::uint64_t modulesAt60 = RunModuleFrameFeedHash(1.0F / 60.0F);
    const std::uint64_t modulesAt144 = RunModuleFrameFeedHash(1.0F / 144.0F);
    Require(modulesAt30 == modulesAt60 && modulesAt60 == modulesAt144,
        "module executors diverged across equal authoritative fixed-step feeds");
}

[[nodiscard]] std::vector<kb::particles::ParticleRuntimeState> CopyBackendStates(
    const kb::particle_plugin::CpuParticleBackend& backend,
    const Fixture& fixture,
    std::uint64_t instanceId) {
    const auto query = backend.Query(fixture.scene, instanceId);
    Require(query.Succeeded(), "backend state-copy query failed");
    std::vector<kb::particles::ParticleRuntimeState> states(query.liveParticleCount);
    Require(backend.CopyLiveParticleStates(fixture.scene, instanceId, states) == states.size(),
        "backend state-copy total mismatch");
    return states;
}

void TestUniformSolidAngleConeGolden() {
    auto effect = Fixture::MakeEffect(0.0F, 8192U);
    kb::scene::ParticleEmitterAsset& emitter = effect.emitters[0];
    emitter.spawn.lifetimeMin = 10.0F;
    emitter.spawn.lifetimeMax = 10.0F;
    AddModule(emitter, 1U, kb::scene::ParticleModuleType::InitialVelocity,
        kb::scene::ParticleInitialVelocityModule{
            .direction = {0.0F, 1.0F, 0.0F},
            .speedMin = 1.0F,
            .speedMax = 1.0F,
            .randomization = 1.0F,
            .spreadDegrees = 60.0F,
        });
    Fixture fixture(std::move(effect));
    kb::particle_plugin::CpuParticleBackend backend;
    backend.Warmup();
    const auto created = backend.Create(fixture.scene, fixture.effectAssetId, fixture.owner);
    Require(created.Succeeded() && backend.SetSeed(fixture.scene, created.instanceId, 0x2468ACE013579BDFULL).Succeeded() &&
            backend.Emit(fixture.scene, created.instanceId, 8192U).Succeeded(),
        "uniform cone fixture setup failed");
    const auto states = CopyBackendStates(backend, fixture, created.instanceId);
    double cosineSum = 0.0;
    for (const auto& state : states) cosineSum += static_cast<double>(state.velocity.y);
    const double meanCosine = cosineSum / static_cast<double>(states.size());
    Require(std::abs(meanCosine - 0.75) < 0.01,
        "cone samples do not follow a uniform solid-angle distribution");
    constexpr std::uint64_t kExpectedConeHash = 10334376869005698480ULL;
    const std::uint64_t actualHash = HashStateSpan(states);
    Require(actualHash == kExpectedConeHash,
        ("uniform cone deterministic golden changed: actual=" + std::to_string(actualHash)).c_str());
}

void TestVisualModulesObservableGoldenAndDefaults() {
    const auto makeVisual = [](bool enabled) {
        auto effect = Fixture::MakeEffect(0.0F, 4U);
        kb::scene::ParticleEmitterAsset& emitter = effect.emitters[0];
        emitter.spawn.lifetimeMin = 1.0F;
        emitter.spawn.lifetimeMax = 1.0F;
        emitter.spawn.speedMin = 0.0F;
        emitter.spawn.speedMax = 0.0F;
        AddModule(emitter, 1U, kb::scene::ParticleModuleType::ColorOverLife,
            kb::scene::ParticleColorOverLifeModule{.gradient = {.stops = {
                {.time = 0.0F, .color = {1.0F, 0.2F, 0.0F, 0.8F}},
                {.time = 1.0F, .color = {0.0F, 0.6F, 1.0F, 0.4F}},
            }}}, enabled);
        AddModule(emitter, 2U, kb::scene::ParticleModuleType::SizeOverLife,
            kb::scene::ParticleSizeOverLifeModule{.curve = {.keyframes = {
                {.time = 0.0F, .value = 1.0F, .easing = kb::math::Easing::Linear},
                {.time = 1.0F, .value = 3.0F, .easing = kb::math::Easing::Linear},
            }}}, enabled);
        AddModule(emitter, 3U, kb::scene::ParticleModuleType::AlphaOverLife,
            kb::scene::ParticleAlphaOverLifeModule{.curve = {.keyframes = {
                {.time = 0.0F, .value = 0.5F, .easing = kb::math::Easing::Linear},
                {.time = 1.0F, .value = 1.0F, .easing = kb::math::Easing::Linear},
            }}}, enabled);
        return effect;
    };
    const auto sample = [&](bool enabled) {
        Fixture fixture(makeVisual(enabled));
        kb::particle_plugin::CpuParticleBackend backend;
        backend.Warmup();
        const auto created = backend.Create(fixture.scene, fixture.effectAssetId, fixture.owner);
        Require(created.Succeeded() && backend.Play(fixture.scene, created.instanceId).Succeeded() &&
                backend.Emit(fixture.scene, created.instanceId, 1U).Succeeded(),
            "visual module observable fixture setup failed");
        for (std::uint32_t step = 0U; step < 30U; ++step) {
            Require(backend.Step(fixture.scene, kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds).Succeeded(),
                "visual module observable step failed");
        }
        return CopyBackendStates(backend, fixture, created.instanceId);
    };

    const auto enabled = sample(true);
    Require(enabled.size() == 1U, "visual module fixture particle count changed");
    const auto& state = enabled.front();
    Require(std::abs(state.color.r - 0.5F) < 0.00001F &&
            std::abs(state.color.g - 0.4F) < 0.00001F &&
            std::abs(state.color.b - 0.5F) < 0.00001F &&
            std::abs(state.color.a - 0.45F) < 0.00001F &&
            std::abs(state.size - 2.0F) < 0.00001F,
        "curve, gradient, size, or composed alpha state is not publicly observable");
    constexpr std::uint64_t kExpectedVisualHash = 3957020943848305260ULL;
    const std::uint64_t actualHash = HashVisualStateSpan(enabled);
    Require(actualHash == kExpectedVisualHash,
        ("visual curve and gradient deterministic golden changed: actual=" + std::to_string(actualHash)).c_str());

    const auto disabled = sample(false);
    Require(disabled.size() == 1U && disabled.front().color.r == 1.0F &&
            disabled.front().color.g == 1.0F && disabled.front().color.b == 1.0F &&
            disabled.front().color.a == 1.0F && disabled.front().size == 1.0F,
        "disabled visual modules changed opaque-white unit-size defaults");
}

void TestCollisionPlaneExecutionAndDisable() {
    const auto sample = [](bool enabled) {
        auto effect = Fixture::MakeEffect(0.0F, 4U);
        auto& emitter = effect.emitters[0];
        emitter.localPosition = {0.0F, 0.1F, 0.0F};
        emitter.spawn.lifetimeMin = 2.0F;
        emitter.spawn.lifetimeMax = 2.0F;
        emitter.spawn.direction = {0.0F, -1.0F, 0.0F};
        emitter.spawn.speedMin = 12.0F;
        emitter.spawn.speedMax = 12.0F;
        emitter.spawn.randomization = 0.0F;
        emitter.spawn.spreadDegrees = 0.0F;
        AddModule(emitter, 1U, kb::scene::ParticleModuleType::Wind,
            kb::scene::ParticleWindModule{.acceleration = {60.0F, 0.0F, 0.0F}});
        AddModule(emitter, 2U, kb::scene::ParticleModuleType::CollisionPlane,
            kb::scene::ParticleCollisionPlaneModule{
                .normal = {0.0F, 2.0F, 0.0F},
                .distance = 0.0F,
                .restitution = 0.5F,
                .friction = 0.25F,
                .maxEventsPerStep = 4U,
            }, enabled);
        Fixture fixture(std::move(effect));
        kb::particle_plugin::CpuParticleBackend backend;
        backend.Warmup();
        const auto created = backend.Create(fixture.scene, fixture.effectAssetId, fixture.owner);
        Require(created.Succeeded() && backend.Play(fixture.scene, created.instanceId).Succeeded() &&
                backend.Emit(fixture.scene, created.instanceId, 1U).Succeeded() &&
                backend.Step(fixture.scene, kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds).Succeeded(),
            "collision plane fixture execution failed");
        return CopyBackendStates(backend, fixture, created.instanceId).front();
    };
    const auto collided = sample(true);
    Require(std::abs(collided.position.x - 1.0F / 60.0F) < 0.000001F &&
            std::abs(collided.position.y) < 0.000001F &&
            std::abs(collided.velocity.x - 0.75F) < 0.000001F &&
            std::abs(collided.velocity.y - 6.0F) < 0.000001F,
        "collision plane did not correct penetration and reflect normal velocity");
    const auto disabled = sample(false);
    Require(std::abs(disabled.position.x - 1.0F / 60.0F) < 0.000001F &&
            std::abs(disabled.position.y + 0.1F) < 0.000001F &&
            std::abs(disabled.velocity.x - 1.0F) < 0.000001F &&
            std::abs(disabled.velocity.y + 12.0F) < 0.000001F,
        "disabled collision plane changed position or velocity");
}

void TestInternalEventTriggersOrderingAndBounds() {
    const auto makeEmitter = [](std::uint64_t id, float y, std::uint32_t capacity = 256U) {
        auto emitter = Fixture::MakeEffect().emitters[0];
        emitter.emitterId = id;
        emitter.name = "EventEmitter" + std::to_string(id);
        emitter.localPosition = {0.0F, y, 0.0F};
        emitter.maxParticles = capacity;
        emitter.spawn.rateOverTime.keyframes.front().value = 0.0F;
        emitter.spawn.speedMin = 0.0F;
        emitter.spawn.speedMax = 0.0F;
        emitter.spawn.lifetimeMin = 10.0F;
        emitter.spawn.lifetimeMax = 10.0F;
        emitter.modules.clear();
        return emitter;
    };

    auto ordered = Fixture::MakeEffect(0.0F, 8U);
    ordered.emitters[0] = makeEmitter(1U, 0.0F, 8U);
    ordered.emitters.push_back(makeEmitter(2U, 10.0F, 8U));
    ordered.emitters.push_back(makeEmitter(3U, 20.0F, 8U));
    AddModule(ordered.emitters[0], 1U, kb::scene::ParticleModuleType::SubEmitter,
        kb::scene::ParticleSubEmitterModule{.targetEmitterId = 2U,
            .trigger = kb::scene::ParticleEventTrigger::Birth, .count = 1U, .maxDepth = 1U});
    ordered.eventBindings.push_back({.sourceEmitterId = 1U,
        .trigger = kb::scene::ParticleEventTrigger::Birth,
        .action = kb::scene::ParticleEventAction::EmitTargetEmitter,
        .targetEmitterId = 3U, .count = 1U, .maxDepth = 1U, .perStepBudget = 8U});
    Fixture orderedFixture(std::move(ordered));
    kb::particle_plugin::CpuParticleBackend orderedBackend;
    orderedBackend.Warmup();
    const auto orderedInstance = orderedBackend.Create(
        orderedFixture.scene, orderedFixture.effectAssetId, orderedFixture.owner);
    Require(orderedInstance.Succeeded() && orderedBackend.Emit(
                orderedFixture.scene, orderedInstance.instanceId, 1U).Succeeded(),
        "ordered internal event fixture failed");
    const auto orderedStates = CopyBackendStates(orderedBackend, orderedFixture, orderedInstance.instanceId);
    Require(orderedStates.size() == 3U && orderedStates[0].position.y == 0.0F &&
            orderedStates[1].position.y == 10.0F && orderedStates[2].position.y == 20.0F,
        "module actions did not execute before authored event bindings");

    auto death = Fixture::MakeEffect(0.0F, 4U);
    death.emitters[0] = makeEmitter(1U, 0.0F, 4U);
    death.emitters[0].spawn.lifetimeMin = kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds;
    death.emitters[0].spawn.lifetimeMax = kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds;
    death.emitters.push_back(makeEmitter(2U, 4.0F, 4U));
    AddModule(death.emitters[0], 1U, kb::scene::ParticleModuleType::SubEmitter,
        kb::scene::ParticleSubEmitterModule{.targetEmitterId = 2U,
            .trigger = kb::scene::ParticleEventTrigger::Death, .count = 1U, .maxDepth = 1U});
    Fixture deathFixture(std::move(death));
    kb::particle_plugin::CpuParticleBackend deathBackend;
    deathBackend.Warmup();
    const auto deathInstance = deathBackend.Create(deathFixture.scene, deathFixture.effectAssetId, deathFixture.owner);
    Require(deathInstance.Succeeded() && deathBackend.Play(deathFixture.scene, deathInstance.instanceId).Succeeded() &&
            deathBackend.Emit(deathFixture.scene, deathInstance.instanceId, 1U).Succeeded() &&
            deathBackend.Step(deathFixture.scene, kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds).Succeeded(),
        "death-trigger internal event fixture failed");
    const auto deathStates = CopyBackendStates(deathBackend, deathFixture, deathInstance.instanceId);
    Require(deathStates.size() == 1U && deathStates.front().position.y == 4.0F,
        "death trigger did not replace the source with its target particle");

    auto collision = Fixture::MakeEffect(0.0F, 4U);
    collision.emitters[0] = makeEmitter(1U, 0.1F, 4U);
    collision.emitters[0].spawn.direction = {0.0F, -1.0F, 0.0F};
    collision.emitters[0].spawn.speedMin = 12.0F;
    collision.emitters[0].spawn.speedMax = 12.0F;
    collision.emitters.push_back(makeEmitter(2U, 7.0F, 4U));
    AddModule(collision.emitters[0], 1U, kb::scene::ParticleModuleType::CollisionPlane,
        kb::scene::ParticleCollisionPlaneModule{.normal = {0.0F, 1.0F, 0.0F}, .distance = 0.0F,
            .restitution = 0.0F, .friction = 0.0F, .maxEventsPerStep = 4U});
    AddModule(collision.emitters[0], 2U, kb::scene::ParticleModuleType::SubEmitter,
        kb::scene::ParticleSubEmitterModule{.targetEmitterId = 2U,
            .trigger = kb::scene::ParticleEventTrigger::Collision, .count = 1U, .maxDepth = 1U});
    Fixture collisionFixture(std::move(collision));
    kb::particle_plugin::CpuParticleBackend collisionBackend;
    collisionBackend.Warmup();
    const auto collisionInstance = collisionBackend.Create(
        collisionFixture.scene, collisionFixture.effectAssetId, collisionFixture.owner);
    Require(collisionInstance.Succeeded() && collisionBackend.Play(
                collisionFixture.scene, collisionInstance.instanceId).Succeeded() &&
            collisionBackend.Emit(collisionFixture.scene, collisionInstance.instanceId, 1U).Succeeded() &&
            collisionBackend.Step(collisionFixture.scene,
                kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds).Succeeded(),
        "collision-trigger internal event fixture failed");
    Require(collisionBackend.Query(collisionFixture.scene, collisionInstance.instanceId).liveParticleCount == 2U,
        "collision trigger did not emit the target particle");

    auto depth = Fixture::MakeEffect(0.0F, 8U);
    depth.emitters.clear();
    for (std::uint64_t id = 1U; id <= 5U; ++id) depth.emitters.push_back(makeEmitter(id, float(id), 8U));
    for (std::uint64_t id = 1U; id <= 4U; ++id) {
        AddModule(depth.emitters[id - 1U], 1U, kb::scene::ParticleModuleType::SubEmitter,
            kb::scene::ParticleSubEmitterModule{.targetEmitterId = id + 1U,
                .trigger = kb::scene::ParticleEventTrigger::Birth, .count = 1U, .maxDepth = 3U});
    }
    Fixture depthFixture(std::move(depth));
    kb::particle_plugin::CpuParticleBackend depthBackend;
    depthBackend.Warmup();
    const auto depthInstance = depthBackend.Create(depthFixture.scene, depthFixture.effectAssetId, depthFixture.owner);
    Require(depthInstance.Succeeded() && depthBackend.Emit(
                depthFixture.scene, depthInstance.instanceId, 1U).Succeeded() &&
            depthBackend.Query(depthFixture.scene, depthInstance.instanceId).liveParticleCount == 4U,
        "breadth-first internal event depth three did not execute exactly once per level");

    auto capped = Fixture::MakeEffect(0.0F, 4U);
    capped.emitters[0] = makeEmitter(1U, 0.0F, 4U);
    capped.emitters.push_back(makeEmitter(2U, 1.0F, 2U));
    AddModule(capped.emitters[0], 1U, kb::scene::ParticleModuleType::SubEmitter,
        kb::scene::ParticleSubEmitterModule{.targetEmitterId = 2U,
            .trigger = kb::scene::ParticleEventTrigger::Birth, .count = 3U, .maxDepth = 1U});
    Fixture cappedFixture(std::move(capped));
    kb::particle_plugin::CpuParticleBackend cappedBackend;
    cappedBackend.Warmup();
    const auto cappedInstance = cappedBackend.Create(cappedFixture.scene, cappedFixture.effectAssetId, cappedFixture.owner);
    Require(cappedInstance.Succeeded() && cappedBackend.Emit(cappedFixture.scene, cappedInstance.instanceId, 1U).status ==
            kb::particles::ParticleRuntimeStatus::ParticleCapacityReached &&
            cappedBackend.Query(cappedFixture.scene, cappedInstance.instanceId).liveParticleCount == 3U,
        "target emitter capacity rejection was not explicit and bounded");
}

void TestInternalEventBudgetsAndCollisionLocalLimit() {
    const auto makeEmitter = [](std::uint64_t id, std::uint32_t capacity) {
        auto emitter = Fixture::MakeEffect().emitters[0];
        emitter.emitterId = id;
        emitter.name = "BudgetEmitter" + std::to_string(id);
        emitter.maxParticles = capacity;
        emitter.spawn.rateOverTime.keyframes.front().value = 0.0F;
        emitter.spawn.speedMin = 0.0F;
        emitter.spawn.speedMax = 0.0F;
        emitter.spawn.lifetimeMin = 10.0F;
        emitter.spawn.lifetimeMax = 10.0F;
        emitter.modules.clear();
        return emitter;
    };

    auto eventCap = Fixture::MakeEffect();
    eventCap.emitters.clear();
    eventCap.emitters.push_back(makeEmitter(1U,
        static_cast<std::uint32_t>(kb::scene::kParticleEffectMaxEventsPerStep + 1U)));
    eventCap.emitters.push_back(makeEmitter(2U, kb::scene::kParticleEffectMaxCpuParticlesPerEmitter));
    eventCap.eventBindings.push_back({.sourceEmitterId = 1U,
        .trigger = kb::scene::ParticleEventTrigger::Birth,
        .action = kb::scene::ParticleEventAction::EmitTargetEmitter,
        .targetEmitterId = 2U, .count = 1U, .maxDepth = 1U,
        .perStepBudget = kb::scene::kParticleEffectMaxEventsPerStep});
    Fixture eventCapFixture(std::move(eventCap));
    kb::particle_plugin::CpuParticleBackend eventCapBackend;
    eventCapBackend.Warmup();
    const auto eventCapInstance = eventCapBackend.Create(
        eventCapFixture.scene, eventCapFixture.effectAssetId, eventCapFixture.owner);
    Require(eventCapInstance.Succeeded() && eventCapBackend.Emit(eventCapFixture.scene,
                eventCapInstance.instanceId,
                static_cast<std::uint32_t>(kb::scene::kParticleEffectMaxEventsPerStep + 1U)).status ==
            kb::particles::ParticleRuntimeStatus::EventQueueFull,
        "internal event queue boundary plus one was not rejected explicitly");
    Require(eventCapBackend.LastStepTelemetry().rejectedByEventBudget == 1U,
        "internal event queue overflow telemetry was not exact");

    auto spawnCap = Fixture::MakeEffect();
    spawnCap.emitters.clear();
    spawnCap.emitters.push_back(makeEmitter(1U, 1U));
    spawnCap.emitters.push_back(makeEmitter(2U, kb::scene::kParticleEffectMaxCpuParticlesPerEmitter));
    spawnCap.eventBindings.push_back({.sourceEmitterId = 1U,
        .trigger = kb::scene::ParticleEventTrigger::Birth,
        .action = kb::scene::ParticleEventAction::EmitTargetEmitter,
        .targetEmitterId = 2U, .count = kb::scene::kParticleEffectMaxSpawnsPerStep,
        .maxDepth = 1U, .perStepBudget = 1U});
    Fixture spawnCapFixture(std::move(spawnCap));
    kb::particle_plugin::CpuParticleBackend spawnCapBackend;
    spawnCapBackend.Warmup();
    const auto spawnCapInstance = spawnCapBackend.Create(
        spawnCapFixture.scene, spawnCapFixture.effectAssetId, spawnCapFixture.owner);
    Require(spawnCapInstance.Succeeded() && spawnCapBackend.Emit(
                spawnCapFixture.scene, spawnCapInstance.instanceId, 1U).status ==
            kb::particles::ParticleRuntimeStatus::SpawnBudgetExceeded &&
            spawnCapBackend.LastStepTelemetry().rejectedByStepBudget == 1U,
        "internal action spawn boundary plus one was not explicit in result and telemetry");

    auto collisionCap = Fixture::MakeEffect();
    collisionCap.emitters.clear();
    auto source = makeEmitter(1U, 4U);
    source.localPosition = {0.0F, 0.1F, 0.0F};
    source.spawn.direction = {0.0F, -1.0F, 0.0F};
    source.spawn.speedMin = 12.0F;
    source.spawn.speedMax = 12.0F;
    AddModule(source, 1U, kb::scene::ParticleModuleType::CollisionPlane,
        kb::scene::ParticleCollisionPlaneModule{.normal = {0.0F, 1.0F, 0.0F}, .distance = 0.0F,
            .restitution = 0.0F, .friction = 0.0F, .maxEventsPerStep = 1U});
    collisionCap.emitters.push_back(std::move(source));
    collisionCap.emitters.push_back(makeEmitter(2U, 4U));
    collisionCap.eventBindings.push_back({.sourceEmitterId = 1U,
        .trigger = kb::scene::ParticleEventTrigger::Collision, .sourceModuleId = 1U,
        .action = kb::scene::ParticleEventAction::EmitTargetEmitter,
        .targetEmitterId = 2U, .count = 1U, .maxDepth = 1U, .perStepBudget = 4U});
    Fixture collisionCapFixture(std::move(collisionCap));
    kb::particle_plugin::CpuParticleBackend collisionCapBackend;
    collisionCapBackend.Warmup();
    const auto collisionCapInstance = collisionCapBackend.Create(
        collisionCapFixture.scene, collisionCapFixture.effectAssetId, collisionCapFixture.owner);
    Require(collisionCapInstance.Succeeded() && collisionCapBackend.Play(
                collisionCapFixture.scene, collisionCapInstance.instanceId).Succeeded() &&
            collisionCapBackend.Emit(collisionCapFixture.scene, collisionCapInstance.instanceId, 2U).Succeeded() &&
            collisionCapBackend.Step(collisionCapFixture.scene,
                kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds).Succeeded(),
        "collision module local event overflow made the automatic fixed step fatal");
    const auto collisionTelemetry = collisionCapBackend.LastStepTelemetry();
    Require(collisionTelemetry.collisions == 2U && collisionTelemetry.rejectedByEventBudget == 1U &&
            collisionCapBackend.Query(collisionCapFixture.scene, collisionCapInstance.instanceId)
                .liveParticleCount == 3U,
        "collision event local budget telemetry or accepted action count was incorrect");

    auto collisionPrewarm = Fixture::MakeEffect(120.0F, 4U);
    collisionPrewarm.emitters[0] = makeEmitter(1U, 4U);
    collisionPrewarm.emitters[0].localPosition = {0.0F, -0.1F, 0.0F};
    collisionPrewarm.emitters[0].spawn.prewarmSeconds = kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds;
    collisionPrewarm.emitters[0].spawn.rateOverTime.keyframes.front().value = 120.0F;
    AddModule(collisionPrewarm.emitters[0], 1U, kb::scene::ParticleModuleType::CollisionPlane,
        kb::scene::ParticleCollisionPlaneModule{.normal = {0.0F, 1.0F, 0.0F}, .distance = 0.0F,
            .restitution = 0.0F, .friction = 0.0F, .maxEventsPerStep = 1U});
    collisionPrewarm.emitters.push_back(makeEmitter(2U, 4U));
    collisionPrewarm.eventBindings.push_back({.sourceEmitterId = 1U,
        .trigger = kb::scene::ParticleEventTrigger::Collision, .sourceModuleId = 1U,
        .action = kb::scene::ParticleEventAction::EmitTargetEmitter,
        .targetEmitterId = 2U, .count = 1U, .maxDepth = 1U, .perStepBudget = 4U});
    Fixture collisionPrewarmFixture(std::move(collisionPrewarm));
    kb::particle_plugin::CpuParticleBackend collisionPrewarmBackend;
    collisionPrewarmBackend.Warmup();
    const auto collisionPrewarmInstance = collisionPrewarmBackend.Create(
        collisionPrewarmFixture.scene, collisionPrewarmFixture.effectAssetId, collisionPrewarmFixture.owner);
    Require(collisionPrewarmInstance.Succeeded() && collisionPrewarmBackend.Play(
                collisionPrewarmFixture.scene, collisionPrewarmInstance.instanceId).status ==
                kb::particles::ParticleRuntimeStatus::EventBudgetExceeded &&
            !collisionPrewarmBackend.Query(collisionPrewarmFixture.scene,
                collisionPrewarmInstance.instanceId).state &&
            collisionPrewarmBackend.Query(collisionPrewarmFixture.scene,
                collisionPrewarmInstance.instanceId).liveParticleCount == 0U &&
            collisionPrewarmBackend.LastStepTelemetry().rejectedByEventBudget == 1U,
        "CollisionPlane event budget did not return its typed prewarm result and roll back cleanly");
}

void TestAutomaticLimitTelemetryPrewarmRollbackAndScriptDiagnostic() {
    auto capacityEffect = Fixture::MakeEffect(120.0F, 1U);
    Fixture capacityFixture(capacityEffect);
    kb::particle_plugin::CpuParticleBackend capacityBackend;
    capacityBackend.Warmup();
    const auto capacityInstance = capacityBackend.Create(
        capacityFixture.scene, capacityFixture.effectAssetId, capacityFixture.owner);
    Require(capacityInstance.Succeeded() && capacityBackend.Play(
                capacityFixture.scene, capacityInstance.instanceId).Succeeded() &&
            capacityBackend.Step(capacityFixture.scene,
                kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds).Succeeded(),
        "automatic capacity overflow was treated as a fatal fixed-step failure");
    Require(capacityBackend.LastStepTelemetry().spawned == 1U &&
            capacityBackend.LastStepTelemetry().rejectedByCapacity == 1U,
        "automatic capacity overflow telemetry was not exact");
    RuntimeFixture capacityRuntime(std::move(capacityEffect));
    Require(kb::particles::ParticlePlayback::Play(
                capacityRuntime.fixture.scene, capacityRuntime.instanceId).Succeeded(),
        "scene-system capacity fixture play failed");
    static_cast<void>(capacityRuntime.fixture.scene.Runtime().Update(
        kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds));

    auto spawnBudgetEffect = Fixture::MakeEffect(0.0F,
        kb::scene::kParticleEffectMaxCpuParticlesPerEmitter);
    spawnBudgetEffect.emitters[0].spawn.mode = kb::scene::ParticleSpawnMode::Burst;
    spawnBudgetEffect.emitters[0].spawn.bursts = {
        {.timeSeconds = 0.0F, .count = kb::scene::kParticleEffectMaxSpawnsPerStep},
        {.timeSeconds = 0.001F, .count = 1U},
    };
    Fixture spawnBudgetFixture(std::move(spawnBudgetEffect));
    kb::particle_plugin::CpuParticleBackend spawnBudgetBackend;
    spawnBudgetBackend.Warmup();
    const auto spawnBudgetInstance = spawnBudgetBackend.Create(
        spawnBudgetFixture.scene, spawnBudgetFixture.effectAssetId, spawnBudgetFixture.owner);
    Require(spawnBudgetInstance.Succeeded() && spawnBudgetBackend.Play(
                spawnBudgetFixture.scene, spawnBudgetInstance.instanceId).Succeeded() &&
            spawnBudgetBackend.Step(spawnBudgetFixture.scene,
                kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds).Succeeded() &&
            spawnBudgetBackend.LastStepTelemetry().spawned == kb::scene::kParticleEffectMaxSpawnsPerStep &&
            spawnBudgetBackend.LastStepTelemetry().rejectedByStepBudget == 1U,
        "automatic spawn budget overflow was fatal or telemetry was not exact");

    auto eventEffect = MakeCollisionBindingEffect(120.0F, 0.0F, 1U);
    Fixture eventFixture(eventEffect);
    kb::particle_plugin::CpuParticleBackend eventBackend;
    eventBackend.Warmup();
    const auto eventInstance = eventBackend.Create(eventFixture.scene, eventFixture.effectAssetId, eventFixture.owner);
    Require(eventInstance.Succeeded() && eventBackend.Play(eventFixture.scene, eventInstance.instanceId).Succeeded() &&
            eventBackend.Step(eventFixture.scene,
                kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds).Succeeded() &&
            eventBackend.LastStepTelemetry().rejectedByEventBudget == 1U,
        "automatic event action budget overflow was fatal or telemetry was not exact");
    RuntimeFixture eventRuntime(std::move(eventEffect));
    Require(kb::particles::ParticlePlayback::Play(eventRuntime.fixture.scene, eventRuntime.instanceId).Succeeded(),
        "scene-system event fixture play failed");
    static_cast<void>(eventRuntime.fixture.scene.Runtime().Update(
        kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds));

    Fixture prewarmFixture(MakeCollisionBindingEffect(120.0F,
        kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds, 1U));
    kb::particle_plugin::CpuParticleBackend prewarmBackend;
    prewarmBackend.Warmup();
    const auto prewarmInstance = prewarmBackend.Create(
        prewarmFixture.scene, prewarmFixture.effectAssetId, prewarmFixture.owner);
    Require(prewarmInstance.Succeeded() && prewarmBackend.Play(
                prewarmFixture.scene, prewarmInstance.instanceId).status ==
            kb::particles::ParticleRuntimeStatus::EventBudgetExceeded,
        "prewarm event budget failure did not return its typed result");
    const auto prewarmQuery = prewarmBackend.Query(prewarmFixture.scene, prewarmInstance.instanceId);
    Require(prewarmQuery.Succeeded() && !prewarmQuery.state && prewarmQuery.liveParticleCount == 0U &&
            prewarmBackend.BufferedEventCount() == 0U,
        "failed prewarm did not roll back to a clean stopped instance");

    auto scriptEffect = Fixture::MakeEffect(0.0F, 4U);
    auto scriptTarget = scriptEffect.emitters[0];
    scriptTarget.emitterId = 2U;
    scriptTarget.name = "ScriptEventTarget";
    scriptTarget.spawn.rateOverTime.keyframes.front().value = 0.0F;
    scriptEffect.emitters.push_back(std::move(scriptTarget));
    scriptEffect.eventBindings.push_back({.sourceEmitterId = 1U,
        .trigger = kb::scene::ParticleEventTrigger::Birth,
        .action = kb::scene::ParticleEventAction::EmitTargetEmitter,
        .targetEmitterId = 2U, .count = 1U, .maxDepth = 1U, .perStepBudget = 1U});
    Fixture scriptFixture(std::move(scriptEffect));
    kb::particle_plugin::CpuParticleBackend scriptBackend;
    scriptBackend.Warmup();
    Require(kb::particles::ParticlePlayback::RegisterBackend(scriptFixture.scene, scriptBackend).Succeeded(),
        "script event-budget backend registration failed");
    const auto scriptInstance = scriptBackend.Create(
        scriptFixture.scene, scriptFixture.effectAssetId, scriptFixture.owner);
    Require(scriptInstance.Succeeded(), "script event-budget instance creation failed");
    kb::script::ScriptRuntimeHost host{scriptFixture.scene};
    Require(host.Succeeded(), "script event-budget host initialization failed");
    const std::array arguments{
        kb::script::ScriptFunctionArgument{.name = "instance",
            .value = kb::script::ScriptValue{scriptInstance.instanceId, kb::script::ScriptValueType::Hash}},
        kb::script::ScriptFunctionArgument{.name = "count", .value = kb::script::ScriptValue{2}},
    };
    const kb::script::ScriptFunctionCallResult emit = host.Functions().Call(
        "Particles.Emit", arguments, kb::script::ScriptFunctionCallContext{.scene = &scriptFixture.scene});
    Require(!emit.Succeeded() && emit.errors.size() == 1U &&
            emit.errors.front() == "particle event action budget was exceeded",
        "script API did not preserve the exact EventBudgetExceeded diagnostic");
    Require(kb::particles::ParticlePlayback::UnregisterBackend(scriptFixture.scene, scriptBackend).Succeeded(),
        "script event-budget backend unregister failed");
}

void TestModuleOrderEnableAndGravityContracts() {
    const auto makeOrdered = [](bool dragFirst, bool windEnabled) {
        auto effect = Fixture::MakeEffect(0.0F, 8U);
        kb::scene::ParticleEmitterAsset& emitter = effect.emitters[0];
        emitter.spawn.speedMin = 0.0F;
        emitter.spawn.speedMax = 0.0F;
        emitter.spawn.lifetimeMin = 10.0F;
        emitter.spawn.lifetimeMax = 10.0F;
        if (dragFirst) {
            AddModule(emitter, 1U, kb::scene::ParticleModuleType::Drag,
                kb::scene::ParticleDragModule{.coefficient = 6.0F});
            AddModule(emitter, 2U, kb::scene::ParticleModuleType::Wind,
                kb::scene::ParticleWindModule{.acceleration = {60.0F, 0.0F, 0.0F}}, windEnabled);
        } else {
            AddModule(emitter, 1U, kb::scene::ParticleModuleType::Wind,
                kb::scene::ParticleWindModule{.acceleration = {60.0F, 0.0F, 0.0F}}, windEnabled);
            AddModule(emitter, 2U, kb::scene::ParticleModuleType::Drag,
                kb::scene::ParticleDragModule{.coefficient = 6.0F});
        }
        return effect;
    };
    const auto sampleVelocityX = [&](kb::scene::ParticleEffectAsset effect) {
        Fixture fixture(std::move(effect));
        kb::particle_plugin::CpuParticleBackend backend;
        backend.Warmup();
        const auto created = backend.Create(fixture.scene, fixture.effectAssetId, fixture.owner);
        Require(created.Succeeded() && backend.Play(fixture.scene, created.instanceId).Succeeded() &&
                backend.Emit(fixture.scene, created.instanceId, 1U).Succeeded() &&
                backend.Step(fixture.scene, kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds).Succeeded(),
            "ordered module fixture execution failed");
        return CopyBackendStates(backend, fixture, created.instanceId).front().velocity.x;
    };
    const float windThenDrag = sampleVelocityX(makeOrdered(false, true));
    const float dragThenWind = sampleVelocityX(makeOrdered(true, true));
    const float disabledWind = sampleVelocityX(makeOrdered(false, false));
    Require(windThenDrag < dragThenWind && std::abs(windThenDrag - std::exp(-0.1F)) < 0.00001F &&
            std::abs(dragThenWind - 1.0F) < 0.00001F,
        "repeatable force modules did not preserve authored source order");
    Require(std::abs(disabledWind) < 0.000001F,
        "disabled force module affected particle velocity");

    const auto sampleInitialVelocity = [&](bool enabled) {
        auto effect = Fixture::MakeEffect(0.0F, 4U);
        effect.emitters[0].spawn.lifetimeMin = 10.0F;
        effect.emitters[0].spawn.lifetimeMax = 10.0F;
        effect.emitters[0].spawn.randomization = 0.0F;
        effect.emitters[0].spawn.spreadDegrees = 0.0F;
        AddModule(effect.emitters[0], 1U, kb::scene::ParticleModuleType::InitialVelocity,
            kb::scene::ParticleInitialVelocityModule{
                .direction = {0.0F, 1.0F, 0.0F},
                .speedMin = 2.0F,
                .speedMax = 2.0F,
                .randomization = 0.0F,
                .spreadDegrees = 80.0F,
            }, enabled);
        Fixture fixture(std::move(effect));
        kb::particle_plugin::CpuParticleBackend backend;
        backend.Warmup();
        const auto created = backend.Create(fixture.scene, fixture.effectAssetId, fixture.owner);
        Require(created.Succeeded() && backend.Emit(fixture.scene, created.instanceId, 1U).Succeeded(),
            "initial velocity enable fixture execution failed");
        return CopyBackendStates(backend, fixture, created.instanceId).front().velocity;
    };
    const kb::math::Vec3 enabledInitial = sampleInitialVelocity(true);
    const kb::math::Vec3 disabledInitial = sampleInitialVelocity(false);
    Require(std::abs(enabledInitial.y - 2.0F) < 0.000001F && std::abs(enabledInitial.x) < 0.000001F,
        "enabled InitialVelocity did not replace base spawn velocity");
    Require(std::abs(disabledInitial.x - 1.0F) < 0.000001F && std::abs(disabledInitial.y) < 0.000001F,
        "disabled InitialVelocity replaced base spawn velocity");

    kb::scene::LegacyParticleEffectAsset legacy;
    legacy.materialReference = "/Game/Materials/ParticleTest.21kb";
    legacy.gravityScale = 1.0F;
    const kb::scene::ParticleEffectAsset migrated = kb::scene::ParticleEffectAssetMigration::FromLegacy(legacy);
    const auto gravityModule = std::find_if(migrated.emitters[0].modules.begin(), migrated.emitters[0].modules.end(),
        [](const kb::scene::ParticleModuleAsset& module) {
            return module.type == kb::scene::ParticleModuleType::Gravity;
        });
    Require(gravityModule != migrated.emitters[0].modules.end(), "legacy gravity module was not migrated");
    const auto* migratedGravity = std::get_if<kb::scene::ParticleGravityModule>(&gravityModule->payload);
    Require(migratedGravity != nullptr && migratedGravity->acceleration.x == 0.0F &&
            migratedGravity->acceleration.y == 0.0F && migratedGravity->acceleration.z == 0.0F &&
            migratedGravity->sceneGravityScale == 1.0F,
        "legacy gravity did not migrate to the exclusive scene-gravity channel");

    const auto sampleGravityY = [&](kb::scene::ParticleGravityModule gravity) {
        auto effect = Fixture::MakeEffect(0.0F, 4U);
        effect.emitters[0].spawn.speedMin = 0.0F;
        effect.emitters[0].spawn.speedMax = 0.0F;
        effect.emitters[0].spawn.lifetimeMin = 10.0F;
        effect.emitters[0].spawn.lifetimeMax = 10.0F;
        AddModule(effect.emitters[0], 1U, kb::scene::ParticleModuleType::Gravity, gravity);
        Fixture fixture(std::move(effect));
        kb::particle_plugin::CpuParticleBackend backend;
        backend.Warmup();
        const auto created = backend.Create(fixture.scene, fixture.effectAssetId, fixture.owner);
        Require(created.Succeeded() && backend.Play(fixture.scene, created.instanceId).Succeeded() &&
                backend.Emit(fixture.scene, created.instanceId, 1U).Succeeded() &&
                backend.Step(fixture.scene, kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds).Succeeded(),
            "gravity channel fixture execution failed");
        return CopyBackendStates(backend, fixture, created.instanceId).front().velocity.y;
    };
    Require(std::abs(sampleGravityY({.acceleration = {}, .sceneGravityScale = 0.0F})) < 0.000001F,
        "zero legacy gravity scale applied gravity");
    Require(std::abs(sampleGravityY({.acceleration = {}, .sceneGravityScale = 1.0F}) + 9.81F / 60.0F) < 0.000001F,
        "unit scene gravity scale did not apply exactly -9.81 acceleration");
    Require(std::abs(sampleGravityY({.acceleration = {0.0F, 3.0F, 0.0F}, .sceneGravityScale = 0.0F}) -
            3.0F / 60.0F) < 0.000001F,
        "custom gravity acceleration channel was not executed");

    auto migratedRuntime = migrated;
    std::erase_if(migratedRuntime.emitters[0].modules, [](const kb::scene::ParticleModuleAsset& module) {
        return module.type != kb::scene::ParticleModuleType::Gravity;
    });
    migratedRuntime.emitters[0].spawn.rateOverTime.keyframes.front().value = 0.0F;
    migratedRuntime.emitters[0].spawn.speedMin = 0.0F;
    migratedRuntime.emitters[0].spawn.speedMax = 0.0F;
    Fixture migratedFixture(std::move(migratedRuntime));
    kb::particle_plugin::CpuParticleBackend migratedBackend;
    migratedBackend.Warmup();
    const auto migratedInstance = migratedBackend.Create(
        migratedFixture.scene, migratedFixture.effectAssetId, migratedFixture.owner);
    Require(migratedInstance.Succeeded() &&
            migratedBackend.Play(migratedFixture.scene, migratedInstance.instanceId).Succeeded() &&
            migratedBackend.Emit(migratedFixture.scene, migratedInstance.instanceId, 1U).Succeeded() &&
            migratedBackend.Step(migratedFixture.scene,
                kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds).Succeeded(),
        "migrated gravity runtime fixture execution failed");
    Require(std::abs(CopyBackendStates(migratedBackend, migratedFixture, migratedInstance.instanceId)
                .front().velocity.y + 9.81F / 60.0F) < 0.000001F,
        "migrated gravityScale=1 did not execute as exactly -9.81 acceleration");
}

void TestModuleCompileRejection() {
    const auto createStatus = [](kb::scene::ParticleEffectAsset effect) {
        Fixture fixture(std::move(effect));
        kb::particle_plugin::CpuParticleBackend backend;
        backend.Warmup();
        return backend.Create(fixture.scene, fixture.effectAssetId, fixture.owner).status;
    };

    auto unsupportedExternalEffect = Fixture::MakeEffect();
    unsupportedExternalEffect.eventBindings.push_back({
        .sourceEmitterId = 1U,
        .trigger = kb::scene::ParticleEventTrigger::Death,
        .action = kb::scene::ParticleEventAction::EmitEffectAsset,
        .targetEffect = {.virtualPath = "/Game/Effects/External.kbvfx"},
    });
    Require(createStatus(std::move(unsupportedExternalEffect)) ==
            kb::particles::ParticleRuntimeStatus::UnsupportedOutput,
        "external effect event action was accepted by the internal-event compiler");

    const auto addTargetAndBinding = [](kb::scene::ParticleEffectAsset& effect,
                                        kb::scene::ParticleEventTrigger trigger) {
        auto target = effect.emitters[0];
        target.emitterId = 2U;
        target.name = "BindingTarget";
        target.modules.clear();
        target.spawn.rateOverTime.keyframes.front().value = 0.0F;
        effect.emitters.push_back(std::move(target));
        effect.eventBindings.push_back({.sourceEmitterId = 1U, .trigger = trigger, .sourceModuleId = 1U,
            .action = kb::scene::ParticleEventAction::EmitTargetEmitter,
            .targetEmitterId = 2U, .count = 1U, .maxDepth = 1U, .perStepBudget = 1U});
    };
    auto gravitySourceBinding = Fixture::MakeEffect();
    AddModule(gravitySourceBinding.emitters[0], 1U, kb::scene::ParticleModuleType::Gravity,
        kb::scene::ParticleGravityModule{.acceleration = {}, .sceneGravityScale = 1.0F});
    addTargetAndBinding(gravitySourceBinding, kb::scene::ParticleEventTrigger::Collision);
    Require(createStatus(std::move(gravitySourceBinding)) == kb::particles::ParticleRuntimeStatus::UnsupportedOutput,
        "event binding sourced from a non-emitting Gravity module was compiled");

    auto birthModuleBinding = Fixture::MakeEffect();
    AddModule(birthModuleBinding.emitters[0], 1U, kb::scene::ParticleModuleType::CollisionPlane,
        kb::scene::ParticleCollisionPlaneModule{});
    addTargetAndBinding(birthModuleBinding, kb::scene::ParticleEventTrigger::Birth);
    Require(createStatus(std::move(birthModuleBinding)) == kb::particles::ParticleRuntimeStatus::UnsupportedOutput,
        "Birth binding with a CollisionPlane source module was compiled");

    auto bothGravityChannels = Fixture::MakeEffect();
    AddModule(bothGravityChannels.emitters[0], 1U, kb::scene::ParticleModuleType::Gravity,
        kb::scene::ParticleGravityModule{.acceleration = {0.0F, -1.0F, 0.0F}, .sceneGravityScale = 1.0F});
    Require(createStatus(std::move(bothGravityChannels)) == kb::particles::ParticleRuntimeStatus::InvalidAsset,
        "mutually exclusive gravity channels compiled together");

    auto invalidGravity = Fixture::MakeEffect();
    AddModule(invalidGravity.emitters[0], 1U, kb::scene::ParticleModuleType::Gravity,
        kb::scene::ParticleGravityModule{
            .acceleration = {},
            .sceneGravityScale = std::numeric_limits<float>::quiet_NaN(),
        });
    Require(createStatus(std::move(invalidGravity)) == kb::particles::ParticleRuntimeStatus::InvalidAsset,
        "non-finite Gravity payload reached the executor");

    auto invalidVelocity = Fixture::MakeEffect();
    AddModule(invalidVelocity.emitters[0], 1U, kb::scene::ParticleModuleType::InitialVelocity,
        kb::scene::ParticleInitialVelocityModule{
            .direction = {0.0F, 1.0F, 0.0F},
            .speedMin = std::numeric_limits<float>::quiet_NaN(),
            .speedMax = 1.0F,
            .randomization = 1.0F,
            .spreadDegrees = 15.0F,
        });
    Require(createStatus(std::move(invalidVelocity)) == kb::particles::ParticleRuntimeStatus::InvalidAsset,
        "NaN InitialVelocity payload reached the executor");

    auto invalidWind = Fixture::MakeEffect();
    AddModule(invalidWind.emitters[0], 1U, kb::scene::ParticleModuleType::Wind,
        kb::scene::ParticleWindModule{.acceleration = {std::numeric_limits<float>::infinity(), 0.0F, 0.0F}});
    Require(createStatus(std::move(invalidWind)) == kb::particles::ParticleRuntimeStatus::InvalidAsset,
        "non-finite Wind payload reached the executor");

    auto invalidDrag = Fixture::MakeEffect();
    AddModule(invalidDrag.emitters[0], 1U, kb::scene::ParticleModuleType::Drag,
        kb::scene::ParticleDragModule{.coefficient = -1.0F});
    Require(createStatus(std::move(invalidDrag)) == kb::particles::ParticleRuntimeStatus::InvalidAsset,
        "negative Drag payload reached the executor");

    auto invalidColor = Fixture::MakeEffect();
    AddModule(invalidColor.emitters[0], 1U, kb::scene::ParticleModuleType::ColorOverLife,
        kb::scene::ParticleColorOverLifeModule{});
    Require(createStatus(std::move(invalidColor)) == kb::particles::ParticleRuntimeStatus::InvalidAsset,
        "empty ColorOverLife gradient reached the executor");

    auto invalidSize = Fixture::MakeEffect();
    AddModule(invalidSize.emitters[0], 1U, kb::scene::ParticleModuleType::SizeOverLife,
        kb::scene::ParticleSizeOverLifeModule{.curve = {}});
    Require(createStatus(std::move(invalidSize)) == kb::particles::ParticleRuntimeStatus::InvalidAsset,
        "empty SizeOverLife curve reached the executor");

    auto invalidAlpha = Fixture::MakeEffect();
    AddModule(invalidAlpha.emitters[0], 1U, kb::scene::ParticleModuleType::AlphaOverLife,
        kb::scene::ParticleAlphaOverLifeModule{.curve = {.keyframes = {
            {.time = 0.0F, .value = std::numeric_limits<float>::quiet_NaN()},
        }}});
    Require(createStatus(std::move(invalidAlpha)) == kb::particles::ParticleRuntimeStatus::InvalidAsset,
        "non-finite AlphaOverLife curve reached the executor");
}

void TestModulePrewarmParity() {
    RuntimeFixture prewarmed(MakeInternalEventEffect(1.0F));
    RuntimeFixture manual(MakeInternalEventEffect());
    Require(kb::particles::ParticlePlayback::SetSeed(prewarmed.fixture.scene, prewarmed.instanceId, 913U).Succeeded() &&
            kb::particles::ParticlePlayback::SetSeed(manual.fixture.scene, manual.instanceId, 913U).Succeeded() &&
            kb::particles::ParticlePlayback::Play(prewarmed.fixture.scene, prewarmed.instanceId).Succeeded() &&
            kb::particles::ParticlePlayback::Play(manual.fixture.scene, manual.instanceId).Succeeded(),
        "module prewarm parity setup failed");
    for (std::uint32_t step = 0U; step < 60U; ++step) {
        static_cast<void>(manual.fixture.scene.Runtime().Update(kb::scene::kSceneRuntimeDefaultFixedDeltaSeconds));
    }
    Require(HashVisualStateSpan(kb::particles::ParticlePlayback::LiveParticleStates(
                prewarmed.fixture.scene, prewarmed.instanceId)) ==
            HashVisualStateSpan(kb::particles::ParticlePlayback::LiveParticleStates(
                manual.fixture.scene, manual.instanceId)),
        "module prewarm diverged from the shared fixed-step kernel");
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
    twoEmitterManual.emitters[0].spawn.direction = {1.0F, 0.0F, 0.0F};
    twoEmitterManual.emitters[0].spawn.spreadDegrees = 0.0F;
    twoEmitterManual.emitters[0].spawn.randomization = 0.0F;
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
    Fixture fixture(MakeCollisionBindingEffect(60.0F));
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
        TestUniformSolidAngleConeGolden();
        TestVisualModulesObservableGoldenAndDefaults();
        TestCollisionPlaneExecutionAndDisable();
        TestInternalEventTriggersOrderingAndBounds();
        TestInternalEventBudgetsAndCollisionLocalLimit();
        TestAutomaticLimitTelemetryPrewarmRollbackAndScriptDiagnostic();
        TestModuleOrderEnableAndGravityContracts();
        TestModuleCompileRejection();
        TestModulePrewarmParity();
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
