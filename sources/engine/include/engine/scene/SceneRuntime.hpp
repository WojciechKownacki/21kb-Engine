#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <span>

namespace kb::ecs {

class System;
struct SystemSchedulerTrace;
class World;

} // namespace kb::ecs

namespace kb::scene {

class Scene;
class SceneSystem;

struct SceneRuntimeFixedStepSettings {
    float fixedDeltaSeconds = 1.0F / 60.0F;
    float maxFrameDeltaSeconds = 0.25F;
    std::size_t maxFixedStepsPerFrame = 8U;
};

class SceneRuntimeQueries {
public:
    explicit SceneRuntimeQueries(const Scene& scene) noexcept;

    [[nodiscard]] bool ShouldQuit() const noexcept;
    [[nodiscard]] const kb::ecs::World& EcsWorld() const noexcept;
    [[nodiscard]] SceneRuntimeFixedStepSettings FixedStepSettings() const noexcept;
    [[nodiscard]] float FixedInterpolationAlpha() const noexcept;
    [[nodiscard]] std::size_t LastFixedStepCount() const noexcept;
    [[nodiscard]] bool EcsProfilerEnabled() const noexcept;
    [[nodiscard]] const kb::ecs::SystemSchedulerTrace& LastEcsProfilerTrace() const noexcept;
    [[nodiscard]] std::optional<TransformComponent> InterpolatedTransform(SceneEntity entity) const noexcept;
    [[nodiscard]] std::span<const SceneEntity> TransformRenderProxyUpdateEntities() const noexcept;

private:
    const Scene& scene_;
};

class SceneRuntime {
public:
    explicit SceneRuntime(Scene& scene) noexcept;

    void AddSystem(std::unique_ptr<kb::ecs::System> system);
    void AddSceneSystem(std::unique_ptr<SceneSystem> system);
    void SynchronizeTransforms();
    void SetFixedStepSettings(SceneRuntimeFixedStepSettings settings) noexcept;
    [[nodiscard]] SceneRuntimeFixedStepSettings FixedStepSettings() const noexcept;
    [[nodiscard]] float FixedInterpolationAlpha() const noexcept;
    [[nodiscard]] std::size_t LastFixedStepCount() const noexcept;
    void SetEcsProfilerEnabled(bool enabled) noexcept;
    [[nodiscard]] bool EcsProfilerEnabled() const noexcept;
    [[nodiscard]] const kb::ecs::SystemSchedulerTrace& LastEcsProfilerTrace() const noexcept;
    [[nodiscard]] std::optional<TransformComponent> InterpolatedTransform(SceneEntity entity) const noexcept;
    [[nodiscard]] std::span<const SceneEntity> TransformRenderProxyUpdateEntities() const noexcept;
    [[nodiscard]] bool Update(float deltaSeconds);
    void RequestQuit() noexcept;
    [[nodiscard]] bool ShouldQuit() const noexcept;
    [[nodiscard]] kb::ecs::World& EcsWorld() noexcept;

private:
    Scene& scene_;
};

} // namespace kb::scene
