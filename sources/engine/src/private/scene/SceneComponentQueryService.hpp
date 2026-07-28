#pragma once

#include "engine/scene/AudioListenerComponent.hpp"
#include "engine/scene/AudioSourceComponent.hpp"
#include "engine/scene/AnimationAssets.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/CharacterControllerComponent.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/InputComponent.hpp"
#include "engine/scene/JointComponent.hpp"
#include "engine/scene/LightComponent.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/RigidbodyComponent.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TagsComponent.hpp"
#include "engine/scene/VisibilityComponent.hpp"
#include "engine/scene/UIAssets.hpp"

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

    [[nodiscard]] static bool HasInput(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static const InputComponent* TryGetInput(const Scene& scene, SceneEntity entity) noexcept;

    [[nodiscard]] static bool HasRigidbody(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static const RigidbodyComponent* TryGetRigidbody(const Scene& scene, SceneEntity entity) noexcept;

    [[nodiscard]] static bool HasCollider(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static const ColliderComponent* TryGetCollider(const Scene& scene, SceneEntity entity) noexcept;

    [[nodiscard]] static bool HasCharacterController(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static const CharacterControllerComponent* TryGetCharacterController(const Scene& scene, SceneEntity entity) noexcept;

    [[nodiscard]] static bool HasJoint(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static const JointComponent* TryGetJoint(const Scene& scene, SceneEntity entity) noexcept;

    [[nodiscard]] static bool HasTags(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static const TagsComponent* TryGetTags(const Scene& scene, SceneEntity entity) noexcept;

    [[nodiscard]] static bool HasAudioSource(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static const AudioSourceComponent* TryGetAudioSource(const Scene& scene, SceneEntity entity) noexcept;

    [[nodiscard]] static bool HasAudioListener(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static const AudioListenerComponent* TryGetAudioListener(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static bool HasAnimator(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static const Animator* TryGetAnimator(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static bool HasUIDocument(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static const UIDocumentComponent* TryGetUIDocument(const Scene& scene, SceneEntity entity) noexcept;
};

} // namespace kb::scene
