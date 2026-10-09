#pragma once

#include "engine/scene/SceneSystem.hpp"

#include <cstdint>

namespace kb::scene {

class Scene;

// Moves NavAgent entities along their paths while the scene plays (see SceneNavigation). It runs before
// the fixed-step simulation, so a character it moves receives its velocity in the same frame's physics.
class NavigationSceneSystem final : public SceneSystem {
public:
    void OnUpdate(SceneSystemContext& context) override;
};

// One navigation step of `deltaSeconds` for every agent of the scene. Exposed for the system and tests.
void StepSceneNavigation(Scene& scene, float deltaSeconds, std::uint32_t steps = 1U);

} // namespace kb::scene
