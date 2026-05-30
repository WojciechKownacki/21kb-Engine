#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "engine/scene/VisibilityComponent.hpp"
#include "scene/components/SceneCameraComponentStore.hpp"
#include "scene/components/SceneLightComponentStore.hpp"
#include "scene/components/SceneMeshRendererComponentStore.hpp"
#include "scene/components/SceneTransformComponentStore.hpp"
#include "scene/components/SceneVisibilityComponentStore.hpp"

struct ecs_world_t;

namespace kb::scene {

class SceneComponentRegistry;

class SceneComponentStorage {
public:
    SceneComponentStorage(ecs_world_t* world, const SceneComponentRegistry& components) noexcept;

    void SetDefaults(SceneEntity entity, const TransformComponent& transform, const VisibilityComponent& visibility);

    [[nodiscard]] const SceneTransformComponentStore& Transforms() const noexcept;
    [[nodiscard]] SceneTransformComponentStore& Transforms() noexcept;
    [[nodiscard]] const SceneVisibilityComponentStore& Visibility() const noexcept;
    [[nodiscard]] SceneVisibilityComponentStore& Visibility() noexcept;
    [[nodiscard]] const SceneCameraComponentStore& Cameras() const noexcept;
    [[nodiscard]] SceneCameraComponentStore& Cameras() noexcept;
    [[nodiscard]] const SceneMeshRendererComponentStore& MeshRenderers() const noexcept;
    [[nodiscard]] SceneMeshRendererComponentStore& MeshRenderers() noexcept;
    [[nodiscard]] const SceneLightComponentStore& Lights() const noexcept;
    [[nodiscard]] SceneLightComponentStore& Lights() noexcept;

private:
    SceneTransformComponentStore transforms_;
    SceneVisibilityComponentStore visibility_;
    SceneCameraComponentStore cameras_;
    SceneMeshRendererComponentStore meshRenderers_;
    SceneLightComponentStore lights_;
};

} // namespace kb::scene
