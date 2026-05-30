#include "scene/components/SceneComponentStorage.hpp"

#include "scene/components/SceneComponentRegistry.hpp"

namespace kb::scene {

SceneComponentStorage::SceneComponentStorage(ecs_world_t* world, const SceneComponentRegistry& components) noexcept
    : transforms_(world, components.TransformComponentId())
    , visibility_(world, components.VisibilityComponentId())
    , cameras_(world, components.CameraComponentId())
    , meshRenderers_(world, components.MeshRendererComponentId())
    , lights_(world, components.LightComponentId()) {}

void SceneComponentStorage::SetDefaults(SceneEntity entity, const TransformComponent& transform, const VisibilityComponent& visibility) {
    transforms_.Set(entity, transform);
    visibility_.Set(entity, visibility);
}

const SceneTransformComponentStore& SceneComponentStorage::Transforms() const noexcept {
    return transforms_;
}

SceneTransformComponentStore& SceneComponentStorage::Transforms() noexcept {
    return transforms_;
}

const SceneVisibilityComponentStore& SceneComponentStorage::Visibility() const noexcept {
    return visibility_;
}

SceneVisibilityComponentStore& SceneComponentStorage::Visibility() noexcept {
    return visibility_;
}

const SceneCameraComponentStore& SceneComponentStorage::Cameras() const noexcept {
    return cameras_;
}

SceneCameraComponentStore& SceneComponentStorage::Cameras() noexcept {
    return cameras_;
}

const SceneMeshRendererComponentStore& SceneComponentStorage::MeshRenderers() const noexcept {
    return meshRenderers_;
}

SceneMeshRendererComponentStore& SceneComponentStorage::MeshRenderers() noexcept {
    return meshRenderers_;
}

const SceneLightComponentStore& SceneComponentStorage::Lights() const noexcept {
    return lights_;
}

SceneLightComponentStore& SceneComponentStorage::Lights() noexcept {
    return lights_;
}

} // namespace kb::scene
