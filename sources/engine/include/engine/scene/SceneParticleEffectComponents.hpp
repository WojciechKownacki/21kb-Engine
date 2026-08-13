#pragma once

#include "engine/scene/ParticleEffectComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;
using ParticleEffectVisitor = void (*)(SceneEntity entity, const ParticleEffectComponent& component, void* context);

class SceneParticleEffectComponentQueries {
  public:
    explicit SceneParticleEffectComponentQueries(const Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const ParticleEffectComponent* TryGet(SceneEntity entity) const noexcept;
    void ForEach(ParticleEffectVisitor visitor, void* context) const;

  private:
    const Scene& scene_;
};

class SceneParticleEffectComponents {
  public:
    explicit SceneParticleEffectComponents(Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const ParticleEffectComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] ParticleEffectComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const ParticleEffectComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

  private:
    Scene& scene_;
};

} // namespace kb::scene
