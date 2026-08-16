#include "kb/render/scene/SceneParticleRenderSynchronizer.hpp"

#include "engine/particles/ParticlePlayback.hpp"
#include "engine/scene/Scene.hpp"

#include <atomic>
#include <exception>
#include <stdexcept>

namespace kb::render {

SceneParticleRenderSynchronizer::SceneParticleRenderSynchronizer() {
    static std::atomic<std::uint64_t> nextConsumerId{1U};
    consumerId_ = nextConsumerId.fetch_add(1U, std::memory_order_relaxed);
}

SceneParticleRenderSynchronizer::~SceneParticleRenderSynchronizer() { Clear(); }

void SceneParticleRenderSynchronizer::Sync(const kb::scene::Scene& scene, RenderScene& renderScene) {
    kb::scene::Scene& mutableScene = const_cast<kb::scene::Scene&>(scene);
    const kb::particles::ParticleRenderCapabilities capabilities{
        .capabilityEpoch = 1U,
        .outputs = kb::particles::ParticleRenderOutputCapability::Billboard |
            kb::particles::ParticleRenderOutputCapability::StretchedBillboard |
            kb::particles::ParticleRenderOutputCapability::PointSprite,
        .gpuDrawing = true,
        .instancing = true,
        .softParticles = true,
        .subtractiveBlend = true,
    };
    if (!kb::particles::ParticlePlayback::PublishRenderCapabilities(
            mutableScene, consumerId_, capabilities).Succeeded()) {
        throw std::logic_error{"21kb Particle System renderer capability consumer conflict"};
    }
    scenes_[scene.Id()] = &mutableScene;
    std::shared_ptr<const kb::particles::ParticleRenderSnapshot> snapshot =
        kb::particles::ParticlePlayback::ReadRenderSnapshot(scene);
    if (!snapshot || snapshot->SceneId() != scene.Id() || snapshot->IsTombstone()) {
        renderScene.SetParticleRenderSnapshot({});
        return;
    }
    renderScene.SetParticleRenderSnapshot(std::move(snapshot));
}

void SceneParticleRenderSynchronizer::Acknowledge(
    const kb::scene::Scene& scene,
    std::uint64_t fixedStepIndex) {
    if (!kb::particles::ParticlePlayback::AcknowledgeRenderedFixedStep(
            const_cast<kb::scene::Scene&>(scene), consumerId_, fixedStepIndex).Succeeded()) {
        throw std::logic_error{"21kb Particle System renderer progress consumer conflict"};
    }
}

void SceneParticleRenderSynchronizer::ReleaseScene(
    const kb::scene::Scene& scene,
    RenderScene* renderScene) noexcept {
    if (renderScene != nullptr) {
        renderScene->SetParticleRenderSnapshot({});
    }
    const auto tracked = scenes_.find(scene.Id());
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
    for (const auto& [sceneId, scene] : scenes_) {
        static_cast<void>(sceneId);
        if (!kb::particles::ParticlePlayback::ClearRenderCapabilities(*scene, consumerId_).Succeeded()) {
            std::terminate();
        }
    }
    scenes_.clear();
}

} // namespace kb::render
