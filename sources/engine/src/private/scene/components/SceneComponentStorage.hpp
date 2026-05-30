#pragma once

#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "engine/scene/VisibilityComponent.hpp"

#include <cstdint>

struct ecs_world_t;

namespace kb::scene {

class SceneComponentRegistry;

class SceneComponentStorage {
public:
    SceneComponentStorage(ecs_world_t* world, const SceneComponentRegistry& components) noexcept;

    void SetDefaults(SceneEntity entity, const TransformComponent& transform, const VisibilityComponent& visibility);

    [[nodiscard]] const TransformComponent* TryGetTransform(SceneEntity entity) const noexcept;
    [[nodiscard]] TransformComponent* TryGetTransform(SceneEntity entity) noexcept;
    void SetTransform(SceneEntity entity, const TransformComponent& transform);
    void MarkTransformModified(SceneEntity entity) noexcept;

    [[nodiscard]] const VisibilityComponent* TryGetVisibility(SceneEntity entity) const noexcept;
    [[nodiscard]] VisibilityComponent* TryGetVisibility(SceneEntity entity) noexcept;
    void SetVisibility(SceneEntity entity, const VisibilityComponent& visibility);
    void MarkVisibilityModified(SceneEntity entity) noexcept;

    [[nodiscard]] bool HasCamera(SceneEntity entity) const noexcept;
    [[nodiscard]] const CameraComponent* TryGetCamera(SceneEntity entity) const noexcept;
    [[nodiscard]] CameraComponent* TryGetCamera(SceneEntity entity) noexcept;
    void SetCamera(SceneEntity entity, const CameraComponent& camera);
    void RemoveCamera(SceneEntity entity) noexcept;
    void MarkCameraModified(SceneEntity entity) noexcept;

    [[nodiscard]] bool HasMeshRenderer(SceneEntity entity) const noexcept;
    [[nodiscard]] const MeshRendererComponent* TryGetMeshRenderer(SceneEntity entity) const noexcept;
    [[nodiscard]] MeshRendererComponent* TryGetMeshRenderer(SceneEntity entity) noexcept;
    void SetMeshRenderer(SceneEntity entity, const MeshRendererComponent& renderer);
    void RemoveMeshRenderer(SceneEntity entity) noexcept;
    void MarkMeshRendererModified(SceneEntity entity) noexcept;

    [[nodiscard]] bool HasLight(SceneEntity entity) const noexcept;
    [[nodiscard]] const LightComponent* TryGetLight(SceneEntity entity) const noexcept;
    [[nodiscard]] LightComponent* TryGetLight(SceneEntity entity) noexcept;
    void SetLight(SceneEntity entity, const LightComponent& light);
    void RemoveLight(SceneEntity entity) noexcept;
    void MarkLightModified(SceneEntity entity) noexcept;

private:
    ecs_world_t* world_ = nullptr;
    const SceneComponentRegistry& components_;
};

} // namespace kb::scene
