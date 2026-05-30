#pragma once

#include "engine/ecs/SystemScheduler.hpp"
#include "engine/ecs/World.hpp"
#include "scene/components/SceneComponentRegistry.hpp"
#include "scene/components/SceneComponentStorage.hpp"
#include "scene/systems/SceneSystemScheduler.hpp"

namespace kb::scene {

class SceneState {
public:
    SceneState();
    ~SceneState();

    SceneState(const SceneState&) = delete;
    SceneState& operator=(const SceneState&) = delete;
    SceneState(SceneState&&) = delete;
    SceneState& operator=(SceneState&&) = delete;

    kb::ecs::World world;
    SceneComponentRegistry components;
    SceneComponentStorage componentStorage;
    kb::ecs::SystemScheduler systemScheduler;
    SceneSystemScheduler sceneSystemScheduler;
};

} // namespace kb::scene
