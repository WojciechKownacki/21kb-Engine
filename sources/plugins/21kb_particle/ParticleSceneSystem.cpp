#include "ParticleSceneSystem.hpp"

#include "engine/particles/ParticlePlayback.hpp"
#include "engine/scene/SceneSystemContext.hpp"

#include <stdexcept>

namespace kb::particle_plugin {

void ParticleSceneSystem::OnCreate(kb::scene::SceneSystemContext& context) {
    backend_.Warmup();
    const kb::particles::ParticleRuntimeResult result =
        kb::particles::ParticlePlayback::RegisterBackend(context.GetScene(), backend_);
    if (!result.Succeeded()) {
        throw std::logic_error("particle CPU backend registration conflicted with an existing scene provider");
    }
    registered_ = true;
}

void ParticleSceneSystem::OnDestroy(kb::scene::SceneSystemContext& context) {
    if (!registered_) return;
    const kb::particles::ParticleRuntimeResult result =
        kb::particles::ParticlePlayback::UnregisterBackend(context.GetScene(), backend_);
    if (!result.Succeeded()) {
        throw std::logic_error("particle CPU backend ownership changed before scene detach");
    }
    registered_ = false;
}

void ParticleSceneSystem::OnFixedUpdate(kb::scene::SceneSystemContext& context) {
    const kb::particles::ParticleRuntimeResult result = backend_.Step(context.GetScene(), context.DeltaSeconds());
    if (!result.Succeeded()) {
        throw std::logic_error("particle CPU backend rejected the authoritative fixed step");
    }
}

kb::scene::SceneFixedUpdatePhase ParticleSceneSystem::FixedUpdatePhase() const noexcept {
    return kb::scene::SceneFixedUpdatePhase::PostSimulation;
}

bool ParticleSceneSystem::RequiresFixedStep() const { return true; }

} // namespace kb::particle_plugin
