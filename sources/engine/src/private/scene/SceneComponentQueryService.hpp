#pragma once

#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/VisibilityComponent.hpp"

namespace kb::scene {

class Scene;

class SceneComponentQueryService {
public:
    SceneComponentQueryService() = delete;

    [[nodiscard]] static VisibilityComponent Visibility(const Scene& scene, SceneEntity entity);
    [[nodiscard]] static const VisibilityComponent* TryGetVisibility(const Scene& scene, SceneEntity entity) noexcept;

    [[nodiscard]] static bool HasBehaviour(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static const BehaviourComponent* TryGetBehaviour(const Scene& scene, SceneEntity entity) noexcept;

    [[nodiscard]] static bool HasCamera(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static const CameraComponent* TryGetCamera(const Scene& scene, SceneEntity entity) noexcept;

    [[nodiscard]] static bool HasMeshRenderer(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static const MeshRendererComponent* TryGetMeshRenderer(const Scene& scene, SceneEntity entity) noexcept;

    [[nodiscard]] static bool HasLight(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static const LightComponent* TryGetLight(const Scene& scene, SceneEntity entity) noexcept;
};

} // namespace kb::scene
