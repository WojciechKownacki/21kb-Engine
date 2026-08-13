#pragma once

#include "CpuParticleBackend.hpp"
#include "engine/scene/SceneSystem.hpp"

namespace kb::particle_plugin {

class ParticleSceneSystem final : public kb::scene::SceneSystem {
public:
    void OnCreate(kb::scene::SceneSystemContext& context) override;
    void OnFixedUpdate(kb::scene::SceneSystemContext& context) override;
    void OnDestroy(kb::scene::SceneSystemContext& context) override;
    [[nodiscard]] kb::scene::SceneFixedUpdatePhase FixedUpdatePhase() const noexcept override;
    [[nodiscard]] bool RequiresFixedStep() const override;

private:
    CpuParticleBackend backend_;
    bool registered_ = false;
};

} // namespace kb::particle_plugin
