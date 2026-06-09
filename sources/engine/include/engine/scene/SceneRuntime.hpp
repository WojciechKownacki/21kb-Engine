#pragma once

#include <memory>

namespace kb::ecs {

class System;
class World;

} // namespace kb::ecs

namespace kb::scene {

class Scene;
class SceneSystem;

class SceneRuntimeQueries {
public:
    explicit SceneRuntimeQueries(const Scene& scene) noexcept;

    [[nodiscard]] bool ShouldQuit() const noexcept;
    [[nodiscard]] const kb::ecs::World& EcsWorld() const noexcept;

private:
    const Scene& scene_;
};

class SceneRuntime {
public:
    explicit SceneRuntime(Scene& scene) noexcept;

    void AddSystem(std::unique_ptr<kb::ecs::System> system);
    void AddSceneSystem(std::unique_ptr<SceneSystem> system);
    void SynchronizeTransforms();
    [[nodiscard]] bool Update(float deltaSeconds);
    void RequestQuit() noexcept;
    [[nodiscard]] bool ShouldQuit() const noexcept;
    [[nodiscard]] kb::ecs::World& EcsWorld() noexcept;

private:
    Scene& scene_;
};

} // namespace kb::scene
