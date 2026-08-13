#include "CpuParticleBackend.hpp"
#include "ParticleSceneSystem.hpp"

#include "engine/assets/AssetMetadata.hpp"
#include "engine/particles/ParticlePlayback.hpp"
#include "engine/scene/ParticleEffectAssetIO.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneSystemContext.hpp"

#include <array>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>

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

    Fixture() {
        Require(scene.Assets().Manager().RegisterAsset({
                    .id = kb::assets::AssetId{ effectAssetId },
                    .type = kb::scene::kParticleEffectAssetType,
                    .name = "Lifecycle Effect",
                    .virtualPath = "/Game/Effects/Lifecycle.kbvfx",
                    .physicalPath = "Lifecycle.kbvfx",
                    .contentHash = 1U,
                }),
            "effect metadata registration failed");
    }
};

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
    Require(backend.Emit(fixture.scene, created.instanceId, 1U).status ==
            kb::particles::ParticleRuntimeStatus::UnsupportedOutput,
        "pre-kernel emit did not report UnsupportedOutput");
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

    g_allocationCount.store(0U, std::memory_order_relaxed);
    g_countAllocations.store(true, std::memory_order_release);
    const auto created = backend.Create(fixture.scene, fixture.effectAssetId, fixture.owner);
    bool succeeded = created.Succeeded();
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
        std::cout << "21kb Particle System CPU backend tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        g_countAllocations.store(false, std::memory_order_release);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
