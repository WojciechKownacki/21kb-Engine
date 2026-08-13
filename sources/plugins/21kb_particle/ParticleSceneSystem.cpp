#include "ParticleSceneSystem.hpp"

#include "engine/particles/ParticlePlayback.hpp"
#include "engine/scene/SceneSystemContext.hpp"

namespace kb::particle_plugin {

void ParticleSceneSystem::OnCreate(kb::scene::SceneSystemContext& context) {
    static_cast<void>(context);
}

void ParticleSceneSystem::OnDestroy(kb::scene::SceneSystemContext& context) {
    static_cast<void>(context);
}

} // namespace kb::particle_plugin
