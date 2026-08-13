#pragma once

#include "engine/scene/SceneSystem.hpp"

namespace kb::particle_plugin {

class ParticleSceneSystem final : public kb::scene::SceneSystem {
public:
    void OnCreate(kb::scene::SceneSystemContext& context) override;
    void OnDestroy(kb::scene::SceneSystemContext& context) override;
};

} // namespace kb::particle_plugin
