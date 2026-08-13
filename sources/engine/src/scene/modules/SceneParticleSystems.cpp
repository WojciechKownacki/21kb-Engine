#include "engine/scene/SceneParticleSystems.hpp"

#include "engine/particles/ParticlePlayback.hpp"

namespace kb::scene {

SceneParticleSystemQueries::SceneParticleSystemQueries(const Scene& scene) noexcept
    : scene_(scene) {}

bool SceneParticleSystemQueries::Exists(std::uint64_t id) const noexcept {
    return kb::particles::ParticlePlayback::Query(scene_, id).Succeeded();
}

bool SceneParticleSystemQueries::IsPlaying(std::uint64_t id) const noexcept {
    const kb::particles::ParticleRuntimeQueryResult result = kb::particles::ParticlePlayback::Query(scene_, id);
    return result.Succeeded() && result.state;
}

std::uint64_t SceneParticleSystemQueries::EffectAsset(std::uint64_t id) const noexcept {
    const kb::particles::ParticleRuntimeQueryResult result = kb::particles::ParticlePlayback::Query(scene_, id);
    return result.Succeeded() ? result.assetId : 0U;
}

std::uint64_t SceneParticleSystemQueries::ResolvedMaterialAsset(std::uint64_t id) const noexcept {
    const kb::particles::ParticleRuntimeQueryResult result = kb::particles::ParticlePlayback::Query(scene_, id);
    return result.Succeeded() ? result.materialAssetId : 0U;
}

std::uint32_t SceneParticleSystemQueries::LiveParticleCount(std::uint64_t id) const noexcept {
    const kb::particles::ParticleRuntimeQueryResult result = kb::particles::ParticlePlayback::Query(scene_, id);
    return result.Succeeded() ? result.liveParticleCount : 0U;
}

std::span<const ParticleState> SceneParticleSystemQueries::Particles(std::uint64_t id) const {
    return kb::particles::ParticlePlayback::LiveParticleStates(scene_, id);
}

std::vector<std::uint64_t> SceneParticleSystemQueries::LiveInstanceIds() const {
    return kb::particles::ParticlePlayback::LiveInstanceIds(scene_);
}

SceneParticleSystems::SceneParticleSystems(Scene& scene) noexcept
    : scene_(scene) {}

std::uint64_t SceneParticleSystems::Create(std::uint64_t effectAssetId, SceneEntity owner) {
    const kb::particles::ParticleRuntimeResult result = CreateDetailed(effectAssetId, owner);
    return result.Succeeded() ? result.instanceId : 0U;
}

kb::particles::ParticleRuntimeResult SceneParticleSystems::CreateDetailed(std::uint64_t effectAssetId, SceneEntity owner) {
    return kb::particles::ParticlePlayback::Create(scene_, effectAssetId, owner);
}

bool SceneParticleSystems::Release(std::uint64_t id) noexcept {
    return ReleaseDetailed(id).Succeeded();
}

kb::particles::ParticleRuntimeResult SceneParticleSystems::ReleaseDetailed(std::uint64_t id) noexcept {
    return kb::particles::ParticlePlayback::Release(scene_, id);
}

bool SceneParticleSystems::Exists(std::uint64_t id) const noexcept {
    return kb::particles::ParticlePlayback::Query(scene_, id).Succeeded();
}

bool SceneParticleSystems::Play(std::uint64_t id) noexcept {
    return PlayDetailed(id).Succeeded();
}

kb::particles::ParticleRuntimeResult SceneParticleSystems::PlayDetailed(std::uint64_t id) noexcept {
    return kb::particles::ParticlePlayback::Play(scene_, id);
}

kb::particles::ParticleRuntimeResult SceneParticleSystems::Pause(std::uint64_t id) noexcept {
    return kb::particles::ParticlePlayback::Pause(scene_, id);
}

bool SceneParticleSystems::Stop(std::uint64_t id) noexcept {
    return StopDetailed(id).Succeeded();
}

kb::particles::ParticleRuntimeResult SceneParticleSystems::StopDetailed(std::uint64_t id) noexcept {
    return kb::particles::ParticlePlayback::Stop(scene_, id);
}

kb::particles::ParticleRuntimeResult SceneParticleSystems::Restart(std::uint64_t id) noexcept {
    return kb::particles::ParticlePlayback::Restart(scene_, id);
}

bool SceneParticleSystems::IsPlaying(std::uint64_t id) const noexcept {
    const kb::particles::ParticleRuntimeQueryResult result = kb::particles::ParticlePlayback::Query(scene_, id);
    return result.Succeeded() && result.state;
}

std::uint64_t SceneParticleSystems::EffectAsset(std::uint64_t id) const noexcept {
    const kb::particles::ParticleRuntimeQueryResult result = kb::particles::ParticlePlayback::Query(scene_, id);
    return result.Succeeded() ? result.assetId : 0U;
}

std::uint64_t SceneParticleSystems::ResolvedMaterialAsset(std::uint64_t id) const noexcept {
    const kb::particles::ParticleRuntimeQueryResult result = kb::particles::ParticlePlayback::Query(scene_, id);
    return result.Succeeded() ? result.materialAssetId : 0U;
}

std::span<const ParticleState> SceneParticleSystems::Particles(std::uint64_t id) const {
    return kb::particles::ParticlePlayback::LiveParticleStates(scene_, id);
}

bool SceneParticleSystems::SetSeed(std::uint64_t id, std::uint64_t seed) noexcept {
    return SetSeedDetailed(id, seed).Succeeded();
}

kb::particles::ParticleRuntimeResult SceneParticleSystems::SetSeedDetailed(std::uint64_t id, std::uint64_t seed) noexcept {
    return kb::particles::ParticlePlayback::SetSeed(scene_, id, seed);
}

bool SceneParticleSystems::SetParameterScalar(std::uint64_t id, std::string_view name, float value) noexcept {
    return SetParameterScalarDetailed(id, name, value).Succeeded();
}

kb::particles::ParticleRuntimeResult SceneParticleSystems::SetParameterScalarDetailed(std::uint64_t id, std::string_view name, float value) noexcept {
    return kb::particles::ParticlePlayback::SetParameterScalar(scene_, id, name, value);
}

bool SceneParticleSystems::ClearParameter(std::uint64_t id, std::string_view name) noexcept {
    return ClearParameterDetailed(id, name).Succeeded();
}

kb::particles::ParticleRuntimeResult SceneParticleSystems::ClearParameterDetailed(std::uint64_t id, std::string_view name) noexcept {
    return kb::particles::ParticlePlayback::ClearParameter(scene_, id, name);
}

bool SceneParticleSystems::Emit(std::uint64_t id, std::uint32_t count) {
    return EmitDetailed(id, count).Succeeded();
}

kb::particles::ParticleRuntimeResult SceneParticleSystems::EmitDetailed(std::uint64_t id, std::uint32_t count) {
    return kb::particles::ParticlePlayback::Emit(scene_, id, count);
}

std::uint32_t SceneParticleSystems::LiveParticleCount(std::uint64_t id) const noexcept {
    const kb::particles::ParticleRuntimeQueryResult result = kb::particles::ParticlePlayback::Query(scene_, id);
    return result.Succeeded() ? result.liveParticleCount : 0U;
}

std::vector<ParticleSystemFinishedEvent> SceneParticleSystems::DrainFinishedEvents() {
    std::vector<ParticleSystemFinishedEvent> output;
    const std::vector<kb::particles::PendingParticleRuntimeEvent> events = kb::particles::ParticlePlayback::DrainEvents(scene_);
    output.reserve(events.size());
    for (const kb::particles::PendingParticleRuntimeEvent& event : events) {
        output.push_back({ .target = event.target, .instanceId = event.instanceId, .effectAssetId = event.effectAssetId });
    }
    return output;
}

} // namespace kb::scene
