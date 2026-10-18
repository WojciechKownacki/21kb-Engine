#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/CombatantComponent.hpp"

#include <cstdint>

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneCombatantComponentStore {
public:
    SceneCombatantComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const CombatantComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] CombatantComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const CombatantComponent& combatant);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene
