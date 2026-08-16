#pragma once

#include "engine/scene/SceneParticleEffectComponents.hpp"

namespace kb::ecs {
class World;
}

namespace kb::scene {

class SceneParticleEffectComponentStore {
  public:
    SceneParticleEffectComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const ParticleEffectComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] ParticleEffectComponent* TryGet(SceneEntity entity) noexcept;
    void ForEach(ParticleEffectVisitor visitor, void* context) const;
    void Set(SceneEntity entity, const ParticleEffectComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

  private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene
