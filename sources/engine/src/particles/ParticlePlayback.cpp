#include "engine/particles/ParticlePlayback.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/ParticleEffectAssetSchema.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

#include <algorithm>
#include <cassert>
#include <thread>
#include <utility>

namespace kb::particles {
namespace {

void AssertOwnerThread(const kb::scene::Scene& scene) noexcept {
#if !defined(NDEBUG)
    assert(kb::scene::SceneAccess::State(scene).particlePlaybackOwnerThread == std::this_thread::get_id() &&
           "particle playback must be accessed from its scene owner thread");
#else
    static_cast<void>(scene);
#endif
}

[[nodiscard]] IParticleSimulationBackend* Backend(const kb::scene::Scene& scene) noexcept {
    return kb::scene::SceneAccess::State(scene).particleSimulationBackend;
}

} // namespace

ParticleRuntimeResult ParticlePlayback::RegisterBackend(kb::scene::Scene& scene, IParticleSimulationBackend& backend) noexcept {
    AssertOwnerThread(scene);
    kb::scene::SceneState& state = kb::scene::SceneAccess::State(scene);
    if (state.particleSimulationBackend != nullptr && state.particleSimulationBackend != &backend) {
        return { .status = ParticleRuntimeStatus::BackendAlreadyRegistered };
    }
    state.particleSimulationBackend = &backend;
    return { .status = ParticleRuntimeStatus::Success };
}

ParticleRuntimeResult ParticlePlayback::UnregisterBackend(kb::scene::Scene& scene, IParticleSimulationBackend& backend) noexcept {
    AssertOwnerThread(scene);
    kb::scene::SceneState& state = kb::scene::SceneAccess::State(scene);
    if (state.particleSimulationBackend != &backend) return { .status = ParticleRuntimeStatus::InvalidRequest };
    state.particleSimulationBackend = nullptr;
    state.pendingParticleRuntimeEvents.clear();
    return { .status = ParticleRuntimeStatus::Success };
}

bool ParticlePlayback::HasBackend(const kb::scene::Scene& scene) noexcept {
    AssertOwnerThread(scene);
    return Backend(scene) != nullptr;
}

#define KB_PARTICLE_FORWARD(Method, ...)                                                                                \
    AssertOwnerThread(scene);                                                                                           \
    IParticleSimulationBackend* backend = Backend(scene);                                                               \
    return backend == nullptr ? ParticleRuntimeResult{} : backend->Method(scene, __VA_ARGS__)

ParticleRuntimeResult ParticlePlayback::Create(kb::scene::Scene& scene, std::uint64_t effectAssetId, kb::scene::SceneEntity owner) {
    KB_PARTICLE_FORWARD(Create, effectAssetId, owner);
}
ParticleRuntimeResult ParticlePlayback::Release(kb::scene::Scene& scene, std::uint64_t instanceId) noexcept { KB_PARTICLE_FORWARD(Release, instanceId); }
ParticleRuntimeResult ParticlePlayback::Play(kb::scene::Scene& scene, std::uint64_t instanceId) noexcept { KB_PARTICLE_FORWARD(Play, instanceId); }
ParticleRuntimeResult ParticlePlayback::Pause(kb::scene::Scene& scene, std::uint64_t instanceId) noexcept { KB_PARTICLE_FORWARD(Pause, instanceId); }
ParticleRuntimeResult ParticlePlayback::Stop(kb::scene::Scene& scene, std::uint64_t instanceId) noexcept { KB_PARTICLE_FORWARD(Stop, instanceId); }
ParticleRuntimeResult ParticlePlayback::Restart(kb::scene::Scene& scene, std::uint64_t instanceId) noexcept { KB_PARTICLE_FORWARD(Restart, instanceId); }
ParticleRuntimeResult ParticlePlayback::SetSeed(kb::scene::Scene& scene, std::uint64_t instanceId, std::uint64_t seed) noexcept { KB_PARTICLE_FORWARD(SetSeed, instanceId, seed); }
ParticleRuntimeResult ParticlePlayback::SetParameterScalar(kb::scene::Scene& scene, std::uint64_t instanceId, std::string_view name, float value) noexcept { KB_PARTICLE_FORWARD(SetParameterScalar, instanceId, name, value); }
ParticleRuntimeResult ParticlePlayback::ClearParameter(kb::scene::Scene& scene, std::uint64_t instanceId, std::string_view name) noexcept { KB_PARTICLE_FORWARD(ClearParameter, instanceId, name); }
ParticleRuntimeResult ParticlePlayback::Emit(kb::scene::Scene& scene, std::uint64_t instanceId, std::uint32_t count) { KB_PARTICLE_FORWARD(Emit, instanceId, count); }

#undef KB_PARTICLE_FORWARD

ParticleRuntimeQueryResult ParticlePlayback::Query(const kb::scene::Scene& scene, std::uint64_t instanceId) noexcept {
    AssertOwnerThread(scene);
    IParticleSimulationBackend* backend = Backend(scene);
    return backend == nullptr ? ParticleRuntimeQueryResult{} : backend->Query(scene, instanceId);
}

std::vector<std::uint64_t> ParticlePlayback::LiveInstanceIds(const kb::scene::Scene& scene) {
    AssertOwnerThread(scene);
    IParticleSimulationBackend* backend = Backend(scene);
    if (backend == nullptr) return {};
    const std::size_t count = backend->CopyLiveInstanceIds(scene, {});
    std::vector<std::uint64_t> ids(count);
    const std::size_t copied = backend->CopyLiveInstanceIds(scene, ids);
    if (copied < ids.size()) ids.resize(copied);
    return ids;
}

std::span<const ParticleRuntimeState> ParticlePlayback::LiveParticleStates(
    const kb::scene::Scene& scene,
    std::uint64_t instanceId) {
    AssertOwnerThread(scene);
    IParticleSimulationBackend* backend = Backend(scene);
    std::vector<ParticleRuntimeState>& scratch = kb::scene::SceneAccess::State(scene).particleRuntimeStateScratch;
    scratch.clear();
    if (backend == nullptr) return {};

    const std::size_t reported = backend->CopyLiveParticleStates(scene, instanceId, {});
    const std::size_t bounded = std::min<std::size_t>(reported, kb::scene::kParticleEffectMaxCpuParticlesPerScene);
    scratch.resize(bounded);
    const std::size_t copied = backend->CopyLiveParticleStates(scene, instanceId, scratch);
    if (copied < scratch.size()) scratch.resize(copied);
    return scratch;
}

ParticleRuntimeResult ParticlePlayback::QueueEvent(kb::scene::Scene& scene, PendingParticleRuntimeEvent event) {
    AssertOwnerThread(scene);
    kb::scene::SceneState& state = kb::scene::SceneAccess::State(scene);
    if (state.particleSimulationBackend == nullptr) return { .status = ParticleRuntimeStatus::BackendUnavailable };
    if (state.pendingParticleRuntimeEvents.size() >= kb::scene::kParticleEffectMaxEventsPerStep) {
        return { .status = ParticleRuntimeStatus::EventQueueFull };
    }
    state.pendingParticleRuntimeEvents.push_back(event);
    return { .status = ParticleRuntimeStatus::Success };
}

std::vector<PendingParticleRuntimeEvent> ParticlePlayback::DrainEvents(kb::scene::Scene& scene) {
    AssertOwnerThread(scene);
    return std::exchange(kb::scene::SceneAccess::State(scene).pendingParticleRuntimeEvents, {});
}

} // namespace kb::particles
