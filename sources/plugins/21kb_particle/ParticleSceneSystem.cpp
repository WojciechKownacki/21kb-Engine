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

} // namespace kb::particle_plugin
