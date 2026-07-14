#pragma once

#include "engine/ecs/World.hpp"
#include "engine/scene/SceneRuntime.hpp"

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
    static void SetFixedStepSettings(Scene& scene, SceneRuntimeFixedStepSettings settings) noexcept;
    static void SetTransformPropagationBudget(Scene& scene, SceneTransformPropagationBudget budget) noexcept;
    [[nodiscard]] static SceneRuntimeFixedStepSettings FixedStepSettings(const Scene& scene) noexcept;
    [[nodiscard]] static SceneTransformPropagationBudget TransformPropagationBudget(const Scene& scene) noexcept;
    [[nodiscard]] static float FixedInterpolationAlpha(const Scene& scene) noexcept;
    [[nodiscard]] static std::size_t LastFixedStepCount(const Scene& scene) noexcept;
    [[nodiscard]] static std::uint64_t FrameIndex(const Scene& scene) noexcept;
    [[nodiscard]] static std::uint64_t FixedStepIndex(const Scene& scene) noexcept;
    [[nodiscard]] static double ElapsedSeconds(const Scene& scene) noexcept;
    [[nodiscard]] static bool IsPlaying(const Scene& scene) noexcept;
    static void SetPlaying(Scene& scene, bool playing) noexcept;
    [[nodiscard]] static float TimeScale(const Scene& scene) noexcept;
    static void SetTimeScale(Scene& scene, float scale) noexcept;
    static void SetEcsProfilerEnabled(Scene& scene, bool enabled) noexcept;
    [[nodiscard]] static bool EcsProfilerEnabled(const Scene& scene) noexcept;
    [[nodiscard]] static const kb::ecs::SystemSchedulerTrace& LastEcsProfilerTrace(const Scene& scene) noexcept;
    [[nodiscard]] static SceneRuntimeHotPathReport HotPathReport(const Scene& scene) noexcept;
    [[nodiscard]] static std::optional<TransformComponent> InterpolatedTransform(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static std::span<const SceneEntity> TransformRenderProxyUpdateEntities(const Scene& scene) noexcept;
    [[nodiscard]] static std::span<const WorldTransformAffine3x4> TransformRenderProxyWorldAffine3x4(const Scene& scene) noexcept;
    [[nodiscard]] static std::span<const SceneEntity> MeshRendererRenderProxyUpdateEntities(const Scene& scene) noexcept;
    [[nodiscard]] static bool Update(Scene& scene, float deltaSeconds);
    static void RequestQuit(Scene& scene) noexcept;
    [[nodiscard]] static bool ShouldQuit(const Scene& scene) noexcept;
    [[nodiscard]] static kb::ecs::World& EcsWorld(Scene& scene) noexcept;
    [[nodiscard]] static const kb::ecs::World& EcsWorld(const Scene& scene) noexcept;
};

} // namespace kb::scene
