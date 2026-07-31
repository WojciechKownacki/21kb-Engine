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
#include "engine/scene/Navigation.hpp"
#include "engine/scene/RigidbodyComponent.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TagsComponent.hpp"
#include "engine/scene/RegionShapeComponent.hpp"
#include "engine/scene/GuideCurveComponent.hpp"
#include "engine/scene/ContentInstanceComponent.hpp"
#include "engine/scene/StreamFocusComponent.hpp"
#include "engine/scene/WorldBackdropComponent.hpp"
#include "engine/scene/AmbientRadianceComponent.hpp"
#include "engine/scene/DetailSwitchComponent.hpp"
#include "engine/scene/VisibilityBlockerComponent.hpp"
#include "engine/scene/VisibilityCellComponent.hpp"
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

    [[nodiscard]] static bool HasRegionShape(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static const RegionShapeComponent* TryGetRegionShape(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static bool HasGuideCurve(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static const GuideCurveComponent* TryGetGuideCurve(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static bool HasContentInstance(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static const ContentInstanceComponent* TryGetContentInstance(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static bool HasStreamFocus(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static const StreamFocusComponent* TryGetStreamFocus(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static bool HasWorldBackdrop(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static const WorldBackdropComponent* TryGetWorldBackdrop(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static bool HasAmbientRadiance(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static const AmbientRadianceComponent* TryGetAmbientRadiance(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static bool HasDetailSwitch(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static const SceneDetailSwitchComponent* TryGetDetailSwitch(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static bool HasVisibilityBlocker(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static const SceneVisibilityBlockerComponent* TryGetVisibilityBlocker(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static bool HasVisibilityCell(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static const VisibilityCellComponent* TryGetVisibilityCell(const Scene& scene, SceneEntity entity) noexcept;

    [[nodiscard]] static bool HasAudioSource(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static const AudioSourceComponent* TryGetAudioSource(const Scene& scene, SceneEntity entity) noexcept;

    [[nodiscard]] static bool HasAudioListener(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static const AudioListenerComponent* TryGetAudioListener(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static bool HasAnimator(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static const Animator* TryGetAnimator(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static bool HasUIDocument(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static const UIDocumentComponent* TryGetUIDocument(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static bool HasNavAgent(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static const NavAgent* TryGetNavAgent(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static bool HasNavObstacle(const Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static const NavObstacle* TryGetNavObstacle(const Scene& scene, SceneEntity entity) noexcept;
};

} // namespace kb::scene
