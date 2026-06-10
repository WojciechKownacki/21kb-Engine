#pragma once

#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/InputComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/VisibilityComponent.hpp"

namespace kb::scene {

class Scene;

class SceneComponentMutationService {
public:
    SceneComponentMutationService() = delete;

    [[nodiscard]] static VisibilityComponent* TryGetVisibility(Scene& scene, SceneEntity entity) noexcept;
    static void SetVisibility(Scene& scene, SceneEntity entity, const VisibilityComponent& visibility);
    static void MarkVisibilityModified(Scene& scene, SceneEntity entity) noexcept;

    [[nodiscard]] static BehaviourComponent* TryGetBehaviour(Scene& scene, SceneEntity entity) noexcept;
    static void SetBehaviour(Scene& scene, SceneEntity entity, const BehaviourComponent& behaviour);
    static void RemoveBehaviour(Scene& scene, SceneEntity entity) noexcept;
    static void MarkBehaviourModified(Scene& scene, SceneEntity entity) noexcept;

    [[nodiscard]] static CameraComponent* TryGetCamera(Scene& scene, SceneEntity entity) noexcept;
    static void SetCamera(Scene& scene, SceneEntity entity, const CameraComponent& camera);
    static void RemoveCamera(Scene& scene, SceneEntity entity) noexcept;
    static void MarkCameraModified(Scene& scene, SceneEntity entity) noexcept;

    [[nodiscard]] static MeshRendererComponent* TryGetMeshRenderer(Scene& scene, SceneEntity entity) noexcept;
    static void SetMeshRenderer(Scene& scene, SceneEntity entity, const MeshRendererComponent& renderer);
    static void RemoveMeshRenderer(Scene& scene, SceneEntity entity) noexcept;
    static void MarkMeshRendererModified(Scene& scene, SceneEntity entity) noexcept;

    [[nodiscard]] static LightComponent* TryGetLight(Scene& scene, SceneEntity entity) noexcept;
    static void SetLight(Scene& scene, SceneEntity entity, const LightComponent& light);
    static void RemoveLight(Scene& scene, SceneEntity entity) noexcept;
    static void MarkLightModified(Scene& scene, SceneEntity entity) noexcept;

    [[nodiscard]] static InputComponent* TryGetInput(Scene& scene, SceneEntity entity) noexcept;
    static void SetInput(Scene& scene, SceneEntity entity, const InputComponent& input);
    static void RemoveInput(Scene& scene, SceneEntity entity) noexcept;
    static void MarkInputModified(Scene& scene, SceneEntity entity) noexcept;
};

} // namespace kb::scene
