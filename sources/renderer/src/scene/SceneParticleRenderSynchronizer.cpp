#include "kb/render/scene/SceneParticleRenderSynchronizer.hpp"

#include "engine/particles/ParticlePlayback.hpp"
#include "engine/scene/Scene.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <stdexcept>

namespace kb::render {

SceneParticleRenderSynchronizer::SceneParticleRenderSynchronizer() {
    static std::atomic<std::uint64_t> nextConsumerId{1U};
    consumerId_ = nextConsumerId.fetch_add(1U, std::memory_order_relaxed);
}

SceneParticleRenderSynchronizer::~SceneParticleRenderSynchronizer() { Clear(); }

bool SceneParticleRenderSynchronizer::Prepare(kb::scene::Scene& scene) noexcept {
    const kb::particles::ParticleRenderCapabilities capabilities{
        .capabilityEpoch = capabilityEpoch_,
        .outputs = kb::particles::ParticleRenderOutputCapability::Billboard |
            kb::particles::ParticleRenderOutputCapability::StretchedBillboard |
            kb::particles::ParticleRenderOutputCapability::PointSprite |
            kb::particles::ParticleRenderOutputCapability::Mesh |
            kb::particles::ParticleRenderOutputCapability::Trail |
            kb::particles::ParticleRenderOutputCapability::Ribbon |
            kb::particles::ParticleRenderOutputCapability::Beam |
            kb::particles::ParticleRenderOutputCapability::Volumetric |
            kb::particles::ParticleRenderOutputCapability::Decal,
        .gpuDrawing = true,
        .instancing = true,
        .softParticles = true,
        .subtractiveBlend = true,
        .gpuVisualAvailability = gpuVisualAvailability_,
        .maxGpuVisualParticles = gpuVisualAvailability_ ==
                kb::particles::ParticleGpuVisualAvailability::Ready
            ? kb::scene::kParticleEffectMaxGpuParticlesPerScene : 0U,
        .maxGpuResourceBytes = gpuVisualAvailability_ ==
                kb::particles::ParticleGpuVisualAvailability::Ready
            ? kb::scene::kParticleEffectMaxGpuResourceBytes : 0U,
    };
    try {
        auto [tracked, inserted] = scenes_.try_emplace(scene.Id());
        if (!kb::particles::ParticlePlayback::PublishRenderCapabilities(scene, consumerId_, capabilities).Succeeded()) {
            if (inserted) scenes_.erase(tracked);
            return false;
        }
        tracked->second.scene = &scene;
        return true;
    } catch (...) { return false; }
}

void SceneParticleRenderSynchronizer::Sync(const kb::scene::Scene& scene, RenderScene& renderScene) {
    kb::scene::Scene& mutableScene = const_cast<kb::scene::Scene&>(scene);
    if (!Prepare(mutableScene)) throw std::logic_error{"21kb Particle System renderer capability consumer conflict"};
    TrackedScene& tracked = scenes_.at(scene.Id());
    // Particles simulate around the render origin, so they stay precise where the camera is (docs/large_worlds.md).
    kb::particles::ParticlePlayback::SetSimulationOrigin(mutableScene, renderScene.RenderOrigin());
    std::shared_ptr<const kb::particles::ParticleRenderSnapshot> snapshot =
        kb::particles::ParticlePlayback::ReadRenderSnapshot(scene);
    if (!snapshot || snapshot->SceneId() != scene.Id() || snapshot->IsTombstone()) {
        SyncParticleLights(nullptr, renderScene, tracked.lightIds);
        renderScene.SetParticleRenderSnapshot({});
        return;
    }
    SyncParticleLights(snapshot.get(), renderScene, tracked.lightIds);
    renderScene.SetParticleRenderSnapshot(std::move(snapshot));
}

void SyncParticleLights(const kb::particles::ParticleRenderSnapshot* snapshot, RenderScene& renderScene,
    std::vector<std::uint64_t>& lightIds) {
    std::vector<std::uint64_t> previous;
    previous.swap(lightIds);
    if (snapshot != nullptr) {
        // Particle positions are relative to the simulation origin, light proxies to the render origin.
        const kb::math::Vec3 offset = kb::math::RelativeTo(snapshot->Origin(), renderScene.RenderOrigin());
        const auto emitters = snapshot->Emitters();
        const auto particles = snapshot->Particles();
        for (std::uint32_t emitterIndex = 0U; emitterIndex < emitters.size(); ++emitterIndex) {
            const auto& emitter = emitters[emitterIndex];
            const std::uint32_t count = std::min<std::uint32_t>(emitter.particleCount, emitter.lightMaxCount);
            for (std::uint32_t local = 0U; local < count; ++local) {
                const auto& particle = particles[emitter.firstParticle + local];
                const auto channel = [&](unsigned shift) noexcept {
                    return static_cast<float>((particle.packedColor >> shift) & 0xFFU) / 255.0F;
                };
                const std::uint64_t id = kParticleLightIdBase | (static_cast<std::uint64_t>(emitterIndex) << 16U) | local;
                static_cast<void>(renderScene.UpsertLight(LightRenderProxyDesc{
                    .entityId = id,
                    .kind = RenderLightKind::Point,
                    .position = {particle.position.x + offset.x, particle.position.y + offset.y,
                        particle.position.z + offset.z},
                    .color = {channel(0U), channel(8U), channel(16U)},
                    // The particle's HDR colour lights the scene (Niagara's light renderer): the packed
                    // colour is 0..1, its emitter's colour intensity carries the rest.
                    .intensity = emitter.lightIntensity * emitter.colorIntensity * channel(24U),
                    .range = emitter.lightRadius,
                    .castsShadow = false,
                }));
                lightIds.push_back(id);
            }
        }
    }
    std::ranges::sort(lightIds);
    for (const std::uint64_t id : previous) {
        if (!std::ranges::binary_search(lightIds, id)) static_cast<void>(renderScene.RemoveLight(id));
    }
}

void SceneParticleRenderSynchronizer::Acknowledge(
    const kb::scene::Scene& scene,
    std::uint64_t fixedStepIndex) {
    const auto tracked = scenes_.find(scene.Id());
    if (tracked == scenes_.end() || fixedStepIndex <= tracked->second.lastAcknowledgedFixedStep) {
        return;
    }
    if (!kb::particles::ParticlePlayback::AcknowledgeRenderedFixedStep(
            const_cast<kb::scene::Scene&>(scene), consumerId_, fixedStepIndex).Succeeded()) {
        throw std::logic_error{"21kb Particle System renderer progress consumer conflict"};
    }
    tracked->second.lastAcknowledgedFixedStep = fixedStepIndex;
}

void SceneParticleRenderSynchronizer::ReleaseScene(
    const kb::scene::Scene& scene,
    RenderScene* renderScene) noexcept {
    const auto tracked = scenes_.find(scene.Id());
    if (renderScene != nullptr) {
        renderScene->SetParticleRenderSnapshot({});
        if (tracked != scenes_.end()) {
            for (const std::uint64_t id : tracked->second.lightIds) static_cast<void>(renderScene->RemoveLight(id));
        }
    }
    if (tracked == scenes_.end()) {
        return;
    }
    if (!kb::particles::ParticlePlayback::ClearRenderCapabilities(
            const_cast<kb::scene::Scene&>(scene), consumerId_).Succeeded()) {
        std::terminate();
    }
    scenes_.erase(tracked);
}

void SceneParticleRenderSynchronizer::Clear() noexcept {
    for (const auto& [sceneId, tracked] : scenes_) {
        static_cast<void>(sceneId);
        if (!kb::particles::ParticlePlayback::ClearRenderCapabilities(*tracked.scene, consumerId_).Succeeded()) {
            std::terminate();
        }
    }
    scenes_.clear();
}

void SceneParticleRenderSynchronizer::SetGpuVisualAvailability(
    kb::particles::ParticleGpuVisualAvailability availability) noexcept {
    if (gpuVisualAvailability_ == availability) return;
    gpuVisualAvailability_ = availability;
    ++capabilityEpoch_;
    if (capabilityEpoch_ == 0U) capabilityEpoch_ = 1U;
}

} // namespace kb::render
