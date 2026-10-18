#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/CombatantComponent.hpp"

namespace kb::scene {

class Scene;

class SceneCombatantComponentQueries {
public:
    explicit SceneCombatantComponentQueries(const Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const CombatantComponent* TryGet(SceneEntity entity) const noexcept;

private:
    const Scene& scene_;
};

class SceneCombatantComponents {
public:
    explicit SceneCombatantComponents(Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const CombatantComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] CombatantComponent* TryGet(SceneEntity entity) noexcept;
    [[nodiscard]] bool Set(SceneEntity entity, const CombatantComponent& combatant);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    Scene& scene_;
};

} // namespace kb::scene
