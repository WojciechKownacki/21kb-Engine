#pragma once

#include "engine/scene/SceneSystem.hpp"

namespace kb::scene {

// Regeneration, ability cooldowns and senses of every Combatant, after animation.
class GameplaySceneSystem final : public SceneSystem {
public:
    void OnUpdate(SceneSystemContext& context) override;
};

} // namespace kb::scene
