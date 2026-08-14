#pragma once

#include "CpuParticleBackend.hpp"
#include "engine/ecs/Query.hpp"
#include "engine/ecs/UnsafeHotQuery.hpp"
#include "engine/scene/ParticleEffectComponent.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneSystem.hpp"

#include <array>

namespace kb::particle_plugin {

class ParticleSceneSystem final : public kb::scene::SceneSystem {
public:
    void OnCreate(kb::scene::SceneSystemContext& context) override;
    void OnFixedUpdate(kb::scene::SceneSystemContext& context) override;
    void OnDestroy(kb::scene::SceneSystemContext& context) override;
    [[nodiscard]] kb::scene::SceneFixedUpdatePhase FixedUpdatePhase() const noexcept override;
    [[nodiscard]] bool RequiresFixedStep() const override;

private:
    struct ComponentInstance {
        kb::scene::SceneEntity owner{};
        std::uint64_t instanceId = 0U;
        kb::scene::ParticleEffectComponent component{};
        bool ownerWasActive = false;
        bool seen = false;
    };

    void ReconcileComponents(kb::scene::Scene& scene);
    void CreateComponentInstance(kb::scene::Scene& scene, ComponentInstance& binding);

    CpuParticleBackend backend_;
    kb::ecs::Query<kb::scene::ParticleEffectComponent> componentQuery_;
    kb::ecs::UnsafeHotReadQuery<kb::scene::ParticleEffectComponent> componentHotQuery_;
    std::array<ComponentInstance, kb::scene::kParticleEffectMaxInstancesPerScene> componentInstances_{};
    std::size_t componentInstanceCount_ = 0U;
    bool registered_ = false;
};

} // namespace kb::particle_plugin
