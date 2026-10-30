#pragma once

#include "engine/scene/SceneSystem.hpp"
#include "engine/scene/StatelessSceneSystem.hpp"

namespace kb::scene {

// Regeneration, ability cooldowns and senses of every Combatant, after animation.
class GameplaySceneSystem final : public SceneSystem, public IStatelessSceneSystem {
public:
    void OnUpdate(SceneSystemContext& context) override;
};

} // namespace kb::scene
