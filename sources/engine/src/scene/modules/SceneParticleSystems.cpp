#include "engine/scene/SceneParticleSystems.hpp"

#include "scene/SceneParticleSystemService.hpp"

namespace kb::scene {

SceneParticleSystemQueries::SceneParticleSystemQueries(const Scene& scene) noexcept
    : scene_(scene) {}

bool SceneParticleSystemQueries::Exists(std::uint64_t id) const noexcept {
    return SceneParticleSystemService::Exists(scene_, id);
}

bool SceneParticleSystemQueries::IsPlaying(std::uint64_t id) const noexcept {
    return SceneParticleSystemService::IsPlaying(scene_, id);
}

std::uint64_t SceneParticleSystemQueries::EffectAsset(std::uint64_t id) const noexcept {
    return SceneParticleSystemService::EffectAsset(scene_, id);
}

std::uint64_t SceneParticleSystemQueries::ResolvedMaterialAsset(std::uint64_t id) const noexcept {
    return SceneParticleSystemService::ResolvedMaterialAsset(scene_, id);
}

std::uint32_t SceneParticleSystemQueries::LiveParticleCount(std::uint64_t id) const noexcept {
    return SceneParticleSystemService::LiveParticleCount(scene_, id);
}

std::span<const ParticleState> SceneParticleSystemQueries::Particles(std::uint64_t id) const noexcept {
    return SceneParticleSystemService::Particles(scene_, id);
}

std::vector<std::uint64_t> SceneParticleSystemQueries::LiveInstanceIds() const {
    return SceneParticleSystemService::LiveInstanceIds(scene_);
}

SceneParticleSystems::SceneParticleSystems(Scene& scene) noexcept
    : scene_(scene) {}

std::uint64_t SceneParticleSystems::Create(std::uint64_t effectAssetId, SceneEntity owner) {
    return SceneParticleSystemService::Create(scene_, effectAssetId, owner);
}

bool SceneParticleSystems::Release(std::uint64_t id) noexcept {
    return SceneParticleSystemService::Release(scene_, id);
}

bool SceneParticleSystems::Exists(std::uint64_t id) const noexcept {
    return SceneParticleSystemService::Exists(scene_, id);
}

bool SceneParticleSystems::Play(std::uint64_t id) noexcept {
    return SceneParticleSystemService::Play(scene_, id);
}

bool SceneParticleSystems::Stop(std::uint64_t id) noexcept {
    return SceneParticleSystemService::Stop(scene_, id);
}

bool SceneParticleSystems::IsPlaying(std::uint64_t id) const noexcept {
    return SceneParticleSystemService::IsPlaying(scene_, id);
}

std::uint64_t SceneParticleSystems::EffectAsset(std::uint64_t id) const noexcept {
    return SceneParticleSystemService::EffectAsset(scene_, id);
}

std::uint64_t SceneParticleSystems::ResolvedMaterialAsset(std::uint64_t id) const noexcept {
    return SceneParticleSystemService::ResolvedMaterialAsset(scene_, id);
}

std::span<const ParticleState> SceneParticleSystems::Particles(std::uint64_t id) const noexcept {
    return SceneParticleSystemService::Particles(scene_, id);
}

bool SceneParticleSystems::SetSeed(std::uint64_t id, std::uint64_t seed) noexcept {
    return SceneParticleSystemService::SetSeed(scene_, id, seed);
}

bool SceneParticleSystems::SetParameterScalar(std::uint64_t id, std::string_view name, float value) noexcept {
    return SceneParticleSystemService::SetParameterScalar(scene_, id, name, value);
}

bool SceneParticleSystems::ClearParameter(std::uint64_t id, std::string_view name) noexcept {
    return SceneParticleSystemService::ClearParameter(scene_, id, name);
}

bool SceneParticleSystems::Emit(std::uint64_t id, std::uint32_t count) {
    return SceneParticleSystemService::Emit(scene_, id, count);
}

std::uint32_t SceneParticleSystems::LiveParticleCount(std::uint64_t id) const noexcept {
    return SceneParticleSystemService::LiveParticleCount(scene_, id);
}

std::vector<ParticleSystemFinishedEvent> SceneParticleSystems::DrainFinishedEvents() {
    return SceneParticleSystemService::DrainFinishedEvents(scene_);
}

void SceneParticleSystems::Advance(float deltaSeconds) {
    SceneParticleSystemService::Advance(scene_, deltaSeconds);
}

} // namespace kb::scene
