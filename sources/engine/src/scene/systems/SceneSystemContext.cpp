#include "engine/scene/SceneSystemContext.hpp"

#include "scene/SceneRuntimeService.hpp"

namespace kb::scene {

SceneSystemContext::SceneSystemContext(Scene& scene, float deltaSeconds) noexcept
    : scene_(scene)
    , transforms_(scene)
    , queries_(scene)
    , deltaSeconds_(deltaSeconds) {}

Scene& SceneSystemContext::GetScene() noexcept {
    return scene_;
}

const Scene& SceneSystemContext::GetScene() const noexcept {
    return scene_;
}

kb::ecs::World& SceneSystemContext::EcsWorld() noexcept {
    return SceneRuntimeService::EcsWorld(scene_);
}

const kb::ecs::World& SceneSystemContext::EcsWorld() const noexcept {
    return SceneRuntimeService::EcsWorld(scene_);
}

float SceneSystemContext::DeltaSeconds() const noexcept {
    return deltaSeconds_;
}

SceneSystemTransformAccess& SceneSystemContext::Transforms() noexcept {
    return transforms_;
}

const SceneSystemTransformAccess& SceneSystemContext::Transforms() const noexcept {
    return transforms_;
}

SceneSystemQueryAccess& SceneSystemContext::Queries() noexcept {
    return queries_;
}

const SceneSystemQueryAccess& SceneSystemContext::Queries() const noexcept {
    return queries_;
}

} // namespace kb::scene
