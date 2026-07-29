#include "engine/scene/SceneRuntime.hpp"

#include "engine/ecs/System.hpp"
#include "engine/scene/SceneSystem.hpp"
#include "scene/SceneRuntimeService.hpp"

#include <utility>

namespace kb::scene {

SceneRuntimeQueries::SceneRuntimeQueries(const Scene& scene) noexcept
    : scene_(scene) {}

bool SceneRuntimeQueries::ShouldQuit() const noexcept {
    return SceneRuntimeService::ShouldQuit(scene_);
}

const kb::ecs::World& SceneRuntimeQueries::EcsWorld() const noexcept {
    return SceneRuntimeService::EcsWorld(scene_);
}

SceneRuntimeFixedStepSettings SceneRuntimeQueries::FixedStepSettings() const noexcept {
    return SceneRuntimeService::FixedStepSettings(scene_);
}

float SceneRuntimeQueries::ScriptFixedDeltaSeconds() const noexcept {
    return SceneRuntimeService::ScriptFixedDeltaSeconds(scene_);
}

SceneTransformPropagationBudget SceneRuntimeQueries::TransformPropagationBudget() const noexcept {
    return SceneRuntimeService::TransformPropagationBudget(scene_);
}

float SceneRuntimeQueries::FixedInterpolationAlpha() const noexcept {
    return SceneRuntimeService::FixedInterpolationAlpha(scene_);
}

std::size_t SceneRuntimeQueries::LastFixedStepCount() const noexcept {
    return SceneRuntimeService::LastFixedStepCount(scene_);
}

bool SceneRuntimeQueries::EcsProfilerEnabled() const noexcept {
    return SceneRuntimeService::EcsProfilerEnabled(scene_);
}

const kb::ecs::SystemSchedulerTrace& SceneRuntimeQueries::LastEcsProfilerTrace() const noexcept {
    return SceneRuntimeService::LastEcsProfilerTrace(scene_);
}

SceneRuntimeHotPathReport SceneRuntimeQueries::HotPathReport() const noexcept {
    return SceneRuntimeService::HotPathReport(scene_);
}

std::optional<TransformComponent> SceneRuntimeQueries::InterpolatedTransform(SceneEntity entity) const noexcept {
    return SceneRuntimeService::InterpolatedTransform(scene_, entity);
}

std::span<const SceneEntity> SceneRuntimeQueries::TransformRenderProxyUpdateEntities() const noexcept {
    return SceneRuntimeService::TransformRenderProxyUpdateEntities(scene_);
}

std::span<const WorldTransformAffine3x4> SceneRuntimeQueries::TransformRenderProxyWorldAffine3x4() const noexcept {
    return SceneRuntimeService::TransformRenderProxyWorldAffine3x4(scene_);
}

std::span<const SceneEntity> SceneRuntimeQueries::MeshRendererRenderProxyUpdateEntities() const noexcept {
    return SceneRuntimeService::MeshRendererRenderProxyUpdateEntities(scene_);
}

std::uint64_t SceneRuntimeQueries::FrameIndex() const noexcept {
    return SceneRuntimeService::FrameIndex(scene_);
}

std::uint64_t SceneRuntimeQueries::FixedStepIndex() const noexcept {
    return SceneRuntimeService::FixedStepIndex(scene_);
}

double SceneRuntimeQueries::ElapsedSeconds() const noexcept {
    return SceneRuntimeService::ElapsedSeconds(scene_);
}

bool SceneRuntimeQueries::IsPlaying() const noexcept {
    return SceneRuntimeService::IsPlaying(scene_);
}

float SceneRuntimeQueries::TimeScale() const noexcept {
    return SceneRuntimeService::TimeScale(scene_);
}

std::shared_ptr<const SceneRuntimeReadSnapshot> SceneRuntimeQueries::ReadSnapshot() const {
    return SceneRuntimeService::ReadSnapshot(scene_);
}

SceneRuntime::SceneRuntime(Scene& scene) noexcept
    : scene_(scene) {}

void SceneRuntime::AddSystem(std::unique_ptr<kb::ecs::System> system) {
    SceneRuntimeService::AddSystem(scene_, std::move(system));
}

void SceneRuntime::AddSceneSystem(std::unique_ptr<SceneSystem> system) {
    SceneRuntimeService::AddSceneSystem(scene_, std::move(system));
}

std::vector<std::string> SceneRuntime::DrainSceneSystemErrors() {
    return SceneRuntimeService::DrainSceneSystemErrors(scene_);
}

void SceneRuntime::SynchronizeTransforms() {
    SceneRuntimeService::SynchronizeTransforms(scene_);
}

void SceneRuntime::SetFixedStepSettings(SceneRuntimeFixedStepSettings settings) noexcept {
    SceneRuntimeService::SetFixedStepSettings(scene_, settings);
}

void SceneRuntime::SetScriptFixedDeltaSeconds(float seconds) noexcept {
    SceneRuntimeService::SetScriptFixedDeltaSeconds(scene_, seconds);
}

void SceneRuntime::SetTransformPropagationBudget(SceneTransformPropagationBudget budget) noexcept {
    SceneRuntimeService::SetTransformPropagationBudget(scene_, budget);
}

SceneRuntimeFixedStepSettings SceneRuntime::FixedStepSettings() const noexcept {
    return SceneRuntimeService::FixedStepSettings(scene_);
}

float SceneRuntime::ScriptFixedDeltaSeconds() const noexcept {
    return SceneRuntimeService::ScriptFixedDeltaSeconds(scene_);
}

SceneTransformPropagationBudget SceneRuntime::TransformPropagationBudget() const noexcept {
    return SceneRuntimeService::TransformPropagationBudget(scene_);
}

float SceneRuntime::FixedInterpolationAlpha() const noexcept {
    return SceneRuntimeService::FixedInterpolationAlpha(scene_);
}

std::size_t SceneRuntime::LastFixedStepCount() const noexcept {
    return SceneRuntimeService::LastFixedStepCount(scene_);
}

void SceneRuntime::SetEcsProfilerEnabled(bool enabled) noexcept {
    SceneRuntimeService::SetEcsProfilerEnabled(scene_, enabled);
}

bool SceneRuntime::EcsProfilerEnabled() const noexcept {
    return SceneRuntimeService::EcsProfilerEnabled(scene_);
}

const kb::ecs::SystemSchedulerTrace& SceneRuntime::LastEcsProfilerTrace() const noexcept {
    return SceneRuntimeService::LastEcsProfilerTrace(scene_);
}

SceneRuntimeHotPathReport SceneRuntime::HotPathReport() const noexcept {
    return SceneRuntimeService::HotPathReport(scene_);
}

std::optional<TransformComponent> SceneRuntime::InterpolatedTransform(SceneEntity entity) const noexcept {
    return SceneRuntimeService::InterpolatedTransform(scene_, entity);
}

std::span<const SceneEntity> SceneRuntime::TransformRenderProxyUpdateEntities() const noexcept {
    return SceneRuntimeService::TransformRenderProxyUpdateEntities(scene_);
}

std::span<const WorldTransformAffine3x4> SceneRuntime::TransformRenderProxyWorldAffine3x4() const noexcept {
    return SceneRuntimeService::TransformRenderProxyWorldAffine3x4(scene_);
}

std::span<const SceneEntity> SceneRuntime::MeshRendererRenderProxyUpdateEntities() const noexcept {
    return SceneRuntimeService::MeshRendererRenderProxyUpdateEntities(scene_);
}

bool SceneRuntime::Update(float deltaSeconds) {
    return SceneRuntimeService::Update(scene_, deltaSeconds);
}

void SceneRuntime::RequestQuit() noexcept {
    SceneRuntimeService::RequestQuit(scene_);
}

bool SceneRuntime::ShouldQuit() const noexcept {
    return SceneRuntimeService::ShouldQuit(scene_);
}

kb::ecs::World& SceneRuntime::EcsWorld() noexcept {
    return SceneRuntimeService::EcsWorld(scene_);
}

std::uint64_t SceneRuntime::FrameIndex() const noexcept {
    return SceneRuntimeService::FrameIndex(scene_);
}

std::uint64_t SceneRuntime::FixedStepIndex() const noexcept {
    return SceneRuntimeService::FixedStepIndex(scene_);
}

double SceneRuntime::ElapsedSeconds() const noexcept {
    return SceneRuntimeService::ElapsedSeconds(scene_);
}

bool SceneRuntime::IsPlaying() const noexcept {
    return SceneRuntimeService::IsPlaying(scene_);
}

void SceneRuntime::SetPlaying(bool playing) noexcept {
    SceneRuntimeService::SetPlaying(scene_, playing);
}

float SceneRuntime::TimeScale() const noexcept {
    return SceneRuntimeService::TimeScale(scene_);
}

void SceneRuntime::SetTimeScale(float scale) noexcept {
    SceneRuntimeService::SetTimeScale(scene_, scale);
}

bool SceneRuntime::EnqueueCommand(SceneRuntimeCommand command) {
    return SceneRuntimeService::EnqueueCommand(scene_, command);
}

} // namespace kb::scene
