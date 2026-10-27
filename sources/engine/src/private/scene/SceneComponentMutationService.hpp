#pragma once

#include "engine/scene/AudioListenerComponent.hpp"
#include "engine/scene/AudioSourceComponent.hpp"
#include "engine/scene/AnimationAssets.hpp"
#include "engine/scene/SkeletonBindingComponent.hpp"
#include "engine/scene/SocketAttachmentComponent.hpp"
#include "engine/scene/CombatantComponent.hpp"
#include "engine/scene/AIBrainComponent.hpp"
#include "engine/scene/MotionSkeletonRuleComponent.hpp"
#include "engine/scene/DrawD3DeformedGeometryComponent.hpp"
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
#include "engine/scene/GridComponent.hpp"
#include "engine/scene/TilemapComponent.hpp"
#include "engine/scene/TilemapRendererComponent.hpp"
#include "engine/scene/TilemapColliderComponent.hpp"
#include "engine/scene/CharacterController2DComponent.hpp"
#include "engine/scene/Collider2DComponent.hpp"
#include "engine/scene/Body2DComponent.hpp"
#include "engine/scene/Sprite2DComponent.hpp"
#include "engine/scene/StreamFocusComponent.hpp"
#include "engine/scene/WorldBackdropComponent.hpp"
#include "engine/scene/AmbientRadianceComponent.hpp"
#include "engine/scene/DetailSwitchComponent.hpp"
#include "engine/scene/VisibilityBlockerComponent.hpp"
#include "engine/scene/SurfaceCastComponent.hpp"
#include "engine/scene/FacingPanelComponent.hpp"
#include "engine/scene/SpaceStrokeComponent.hpp"
#include "engine/scene/HistoryRibbonComponent.hpp"
#include "engine/scene/ParticleEffectComponent.hpp"
#include "engine/scene/LensEchoComponent.hpp"
#include "engine/scene/VisibilityCellComponent.hpp"
#include "engine/scene/RegionPortalComponent.hpp"
#include "engine/scene/AuxFrameComponent.hpp"
#include "engine/scene/GeometrySwarmComponent.hpp"
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

    [[nodiscard]] static RigidbodyComponent* TryGetRigidbody(Scene& scene, SceneEntity entity) noexcept;
    static void SetRigidbody(Scene& scene, SceneEntity entity, const RigidbodyComponent& rigidbody);
    static void RemoveRigidbody(Scene& scene, SceneEntity entity) noexcept;
    static void MarkRigidbodyModified(Scene& scene, SceneEntity entity) noexcept;

    [[nodiscard]] static ColliderComponent* TryGetCollider(Scene& scene, SceneEntity entity) noexcept;
    static void SetCollider(Scene& scene, SceneEntity entity, const ColliderComponent& collider);
    static void RemoveCollider(Scene& scene, SceneEntity entity) noexcept;
    static void MarkColliderModified(Scene& scene, SceneEntity entity) noexcept;

    [[nodiscard]] static CharacterControllerComponent* TryGetCharacterController(Scene& scene, SceneEntity entity) noexcept;
    static void SetCharacterController(Scene& scene, SceneEntity entity, const CharacterControllerComponent& characterController);
    static void RemoveCharacterController(Scene& scene, SceneEntity entity) noexcept;
    static void MarkCharacterControllerModified(Scene& scene, SceneEntity entity) noexcept;

    [[nodiscard]] static JointComponent* TryGetJoint(Scene& scene, SceneEntity entity) noexcept;
    static void SetJoint(Scene& scene, SceneEntity entity, const JointComponent& joint);
    static void RemoveJoint(Scene& scene, SceneEntity entity) noexcept;
    static void MarkJointModified(Scene& scene, SceneEntity entity) noexcept;

    [[nodiscard]] static TagsComponent* TryGetTags(Scene& scene, SceneEntity entity) noexcept;
    static void SetTags(Scene& scene, SceneEntity entity, const TagsComponent& tags);
    static void RemoveTags(Scene& scene, SceneEntity entity) noexcept;
    static void MarkTagsModified(Scene& scene, SceneEntity entity) noexcept;

    [[nodiscard]] static RegionShapeComponent* TryGetRegionShape(Scene& scene, SceneEntity entity) noexcept;
    static void SetRegionShape(Scene& scene, SceneEntity entity, const RegionShapeComponent& shape);
    static void RemoveRegionShape(Scene& scene, SceneEntity entity) noexcept;
    static void MarkRegionShapeModified(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static GuideCurveComponent* TryGetGuideCurve(Scene& scene, SceneEntity entity) noexcept;
    static void SetGuideCurve(Scene& scene, SceneEntity entity, const GuideCurveComponent& curve);
    static void RemoveGuideCurve(Scene& scene, SceneEntity entity) noexcept;
    static void MarkGuideCurveModified(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static ContentInstanceComponent* TryGetContentInstance(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static GridComponent* TryGetGrid(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static TilemapComponent* TryGetTilemap(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static TilemapRendererComponent* TryGetTilemapRenderer(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static TilemapColliderComponent* TryGetTilemapCollider(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static CharacterController2DComponent* TryGetCharacterController2D(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static Collider2DComponent* TryGetCollider2D(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static Body2DComponent* TryGetBody2D(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static Sprite2DComponent* TryGetSprite2D(Scene& scene, SceneEntity entity) noexcept;
    static void SetContentInstance(Scene& scene, SceneEntity entity, const ContentInstanceComponent& component);
    static void SetGrid(Scene& scene, SceneEntity entity, const GridComponent& component);
    static void SetTilemap(Scene& scene, SceneEntity entity, const TilemapComponent& component);
    static void SetTilemapRenderer(Scene& scene, SceneEntity entity, const TilemapRendererComponent& component);
    static void SetTilemapCollider(Scene& scene, SceneEntity entity, const TilemapColliderComponent& component);
    static void SetCharacterController2D(Scene& scene, SceneEntity entity, const CharacterController2DComponent& component);
    static void SetCollider2D(Scene& scene, SceneEntity entity, const Collider2DComponent& component);
    static void SetBody2D(Scene& scene, SceneEntity entity, const Body2DComponent& component);
    static void SetSprite2D(Scene& scene, SceneEntity entity, const Sprite2DComponent& component);
    static void RemoveContentInstance(Scene& scene, SceneEntity entity) noexcept;
    static void RemoveGrid(Scene& scene, SceneEntity entity) noexcept;
    static void RemoveTilemap(Scene& scene, SceneEntity entity) noexcept;
    static void RemoveTilemapRenderer(Scene& scene, SceneEntity entity) noexcept;
    static void RemoveTilemapCollider(Scene& scene, SceneEntity entity) noexcept;
    static void RemoveCharacterController2D(Scene& scene, SceneEntity entity) noexcept;
    static void RemoveCollider2D(Scene& scene, SceneEntity entity) noexcept;
    static void RemoveBody2D(Scene& scene, SceneEntity entity) noexcept;
    static void RemoveSprite2D(Scene& scene, SceneEntity entity) noexcept;
    static void MarkContentInstanceModified(Scene& scene, SceneEntity entity) noexcept;
    static void MarkGridModified(Scene& scene, SceneEntity entity) noexcept;
    static void MarkTilemapModified(Scene& scene, SceneEntity entity) noexcept;
    static void MarkTilemapRendererModified(Scene& scene, SceneEntity entity) noexcept;
    static void MarkTilemapColliderModified(Scene& scene, SceneEntity entity) noexcept;
    static void MarkCharacterController2DModified(Scene& scene, SceneEntity entity) noexcept;
    static void MarkCollider2DModified(Scene& scene, SceneEntity entity) noexcept;
    static void MarkBody2DModified(Scene& scene, SceneEntity entity) noexcept;
    static void MarkSprite2DModified(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static StreamFocusComponent* TryGetStreamFocus(Scene& scene, SceneEntity entity) noexcept;
    static void SetStreamFocus(Scene& scene, SceneEntity entity, const StreamFocusComponent& component);
    static void RemoveStreamFocus(Scene& scene, SceneEntity entity) noexcept;
    static void MarkStreamFocusModified(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static WorldBackdropComponent* TryGetWorldBackdrop(Scene& scene, SceneEntity entity) noexcept;
    static void SetWorldBackdrop(Scene& scene, SceneEntity entity, const WorldBackdropComponent& component);
    static void RemoveWorldBackdrop(Scene& scene, SceneEntity entity) noexcept;
    static void MarkWorldBackdropModified(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static AmbientRadianceComponent* TryGetAmbientRadiance(Scene& scene, SceneEntity entity) noexcept;
    static void SetAmbientRadiance(Scene& scene, SceneEntity entity, const AmbientRadianceComponent& component);
    static void RemoveAmbientRadiance(Scene& scene, SceneEntity entity) noexcept;
    static void MarkAmbientRadianceModified(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static SceneDetailSwitchComponent* TryGetDetailSwitch(Scene& scene, SceneEntity entity) noexcept;
    static void SetDetailSwitch(Scene& scene, SceneEntity entity, const SceneDetailSwitchComponent& component);
    static void RemoveDetailSwitch(Scene& scene, SceneEntity entity) noexcept;
    static void MarkDetailSwitchModified(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static SceneVisibilityBlockerComponent* TryGetVisibilityBlocker(Scene& scene, SceneEntity entity) noexcept;
    static void SetVisibilityBlocker(Scene& scene, SceneEntity entity, const SceneVisibilityBlockerComponent& component);
    static void RemoveVisibilityBlocker(Scene& scene, SceneEntity entity) noexcept;
    static void MarkVisibilityBlockerModified(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static VisibilityCellComponent* TryGetVisibilityCell(Scene& scene, SceneEntity entity) noexcept;
    static void SetVisibilityCell(Scene& scene, SceneEntity entity, const VisibilityCellComponent& component);
    static void RemoveVisibilityCell(Scene& scene, SceneEntity entity) noexcept;
    static void MarkVisibilityCellModified(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static SceneRegionPortalComponent* TryGetRegionPortal(Scene& scene, SceneEntity entity) noexcept;
    static void SetRegionPortal(Scene& scene, SceneEntity entity, const SceneRegionPortalComponent& component);
    static void RemoveRegionPortal(Scene& scene, SceneEntity entity) noexcept;
    static void MarkRegionPortalModified(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static AuxFrameComponent* TryGetAuxFrame(Scene& scene, SceneEntity entity) noexcept;
    static void SetAuxFrame(Scene& scene, SceneEntity entity, const AuxFrameComponent& component);
    static void RemoveAuxFrame(Scene& scene, SceneEntity entity) noexcept;
    static void MarkAuxFrameModified(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static GeometrySwarmComponent* TryGetGeometrySwarm(Scene& scene, SceneEntity entity) noexcept;
    static void SetGeometrySwarm(Scene& scene, SceneEntity entity, const GeometrySwarmComponent& component);
    static void RemoveGeometrySwarm(Scene& scene, SceneEntity entity) noexcept;
    static void MarkGeometrySwarmModified(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static SurfaceCastComponent* TryGetSurfaceCast(Scene& scene, SceneEntity entity) noexcept;
    static void SetSurfaceCast(Scene& scene, SceneEntity entity, const SurfaceCastComponent& component);
    static void RemoveSurfaceCast(Scene& scene, SceneEntity entity) noexcept;
    static void MarkSurfaceCastModified(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static FacingPanelComponent* TryGetFacingPanel(Scene& scene, SceneEntity entity) noexcept;
    static void SetFacingPanel(Scene& scene, SceneEntity entity, const FacingPanelComponent& component);
    static void RemoveFacingPanel(Scene& scene, SceneEntity entity) noexcept;
    static void MarkFacingPanelModified(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static SpaceStrokeComponent* TryGetSpaceStroke(Scene& scene, SceneEntity entity) noexcept;
    static void SetSpaceStroke(Scene& scene, SceneEntity entity, const SpaceStrokeComponent& component);
    static void RemoveSpaceStroke(Scene& scene, SceneEntity entity) noexcept;
    static void MarkSpaceStrokeModified(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static HistoryRibbonComponent* TryGetHistoryRibbon(Scene& scene, SceneEntity entity) noexcept;
    static void SetHistoryRibbon(Scene& scene, SceneEntity entity, const HistoryRibbonComponent& component);
    static void RemoveHistoryRibbon(Scene& scene, SceneEntity entity) noexcept;
    static void MarkHistoryRibbonModified(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static ParticleEffectComponent* TryGetParticleEffect(Scene& scene, SceneEntity entity) noexcept;
    static void SetParticleEffect(Scene& scene, SceneEntity entity, const ParticleEffectComponent& component);
    static void RemoveParticleEffect(Scene& scene, SceneEntity entity) noexcept;
    static void MarkParticleEffectModified(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static LensEchoComponent* TryGetLensEcho(Scene& scene, SceneEntity entity) noexcept;
    static void SetLensEcho(Scene& scene, SceneEntity entity, const LensEchoComponent& component);
    static void RemoveLensEcho(Scene& scene, SceneEntity entity) noexcept;
    static void MarkLensEchoModified(Scene& scene, SceneEntity entity) noexcept;

    [[nodiscard]] static AudioSourceComponent* TryGetAudioSource(Scene& scene, SceneEntity entity) noexcept;
    static void SetAudioSource(Scene& scene, SceneEntity entity, const AudioSourceComponent& audioSource);
    static void RemoveAudioSource(Scene& scene, SceneEntity entity) noexcept;
    static void MarkAudioSourceModified(Scene& scene, SceneEntity entity) noexcept;

    [[nodiscard]] static AudioListenerComponent* TryGetAudioListener(Scene& scene, SceneEntity entity) noexcept;
    static void SetAudioListener(Scene& scene, SceneEntity entity, const AudioListenerComponent& audioListener);
    static void RemoveAudioListener(Scene& scene, SceneEntity entity) noexcept;
    static void MarkAudioListenerModified(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static Animator* TryGetAnimator(Scene& scene, SceneEntity entity) noexcept;
    static void SetAnimator(Scene& scene, SceneEntity entity, const Animator& animator);
    static void RemoveAnimator(Scene& scene, SceneEntity entity) noexcept;
    static void MarkAnimatorModified(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static SkeletonBindingComponent* TryGetSkeletonBinding(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static bool SetSkeletonBinding(Scene& scene, SceneEntity entity, const SkeletonBindingComponent& binding);
    static void RemoveSkeletonBinding(Scene& scene, SceneEntity entity) noexcept;
    static void MarkSkeletonBindingModified(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static SocketAttachmentComponent* TryGetSocketAttachment(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static bool SetSocketAttachment(Scene& scene, SceneEntity entity, const SocketAttachmentComponent& attachment);
    static void RemoveSocketAttachment(Scene& scene, SceneEntity entity) noexcept;
    static void MarkSocketAttachmentModified(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static CombatantComponent* TryGetCombatant(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static bool SetCombatant(Scene& scene, SceneEntity entity, const CombatantComponent& combatant);
    static void RemoveCombatant(Scene& scene, SceneEntity entity) noexcept;
    static void MarkCombatantModified(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static AIBrainComponent* TryGetAIBrain(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static bool SetAIBrain(Scene& scene, SceneEntity entity, const AIBrainComponent& aiBrain);
    static void RemoveAIBrain(Scene& scene, SceneEntity entity) noexcept;
    static void MarkAIBrainModified(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static MotionSkeletonRuleComponent* TryGetMotionSkeletonRule(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static bool SetMotionSkeletonRule(Scene& scene, SceneEntity entity, const MotionSkeletonRuleComponent& rule);
    static void RemoveMotionSkeletonRule(Scene& scene, SceneEntity entity) noexcept;
    static void MarkMotionSkeletonRuleModified(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static DrawD3DeformedGeometryComponent* TryGetDeformedGeometry(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static bool SetDeformedGeometry(Scene& scene, SceneEntity entity, const DrawD3DeformedGeometryComponent& geometry);
    static void RemoveDeformedGeometry(Scene& scene, SceneEntity entity) noexcept;
    static void MarkDeformedGeometryModified(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static NavAgent* TryGetNavAgent(Scene& scene, SceneEntity entity) noexcept;
    static void SetNavAgent(Scene& scene, SceneEntity entity, const NavAgent& component);
    static void RemoveNavAgent(Scene& scene, SceneEntity entity) noexcept;
    static void MarkNavAgentModified(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static NavObstacle* TryGetNavObstacle(Scene& scene, SceneEntity entity) noexcept;
    static void SetNavObstacle(Scene& scene, SceneEntity entity, const NavObstacle& component);
    static void RemoveNavObstacle(Scene& scene, SceneEntity entity) noexcept;
    static void MarkNavObstacleModified(Scene& scene, SceneEntity entity) noexcept;
    [[nodiscard]] static NavLink* TryGetNavLink(Scene& scene, SceneEntity entity) noexcept;
    static void SetNavLink(Scene& scene, SceneEntity entity, const NavLink& component);
    static void RemoveNavLink(Scene& scene, SceneEntity entity) noexcept;
    static void MarkNavLinkModified(Scene& scene, SceneEntity entity) noexcept;
};

} // namespace kb::scene
