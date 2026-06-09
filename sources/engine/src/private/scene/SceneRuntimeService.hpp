#pragma once

#include "engine/ecs/World.hpp"

#include <memory>

namespace kb::ecs {

class System;

} // namespace kb::ecs

namespace kb::scene {

class Scene;
class SceneSystem;

class SceneRuntimeService {
public:
    SceneRuntimeService() = delete;

    static void AddSystem(Scene& scene, std::unique_ptr<kb::ecs::System> system);
    static void AddSceneSystem(Scene& scene, std::unique_ptr<SceneSystem> system);
    static void SynchronizeTransforms(Scene& scene);
    [[nodiscard]] static bool Update(Scene& scene, float deltaSeconds);
    static void RequestQuit(Scene& scene) noexcept;
    [[nodiscard]] static bool ShouldQuit(const Scene& scene) noexcept;
    [[nodiscard]] static kb::ecs::World& EcsWorld(Scene& scene) noexcept;
    [[nodiscard]] static const kb::ecs::World& EcsWorld(const Scene& scene) noexcept;
};

} // namespace kb::scene
