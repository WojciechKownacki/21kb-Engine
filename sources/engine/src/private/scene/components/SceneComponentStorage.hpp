#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "engine/scene/VisibilityComponent.hpp"
#include "scene/components/SceneRegionShapeComponentStore.hpp"
#include "scene/components/SceneGuideCurveComponentStore.hpp"
#include "scene/components/SceneContentInstanceComponentStore.hpp"
#include "scene/components/SceneStreamFocusComponentStore.hpp"
#include "scene/components/SceneWorldBackdropComponentStore.hpp"
#include "scene/components/SceneAmbientRadianceComponentStore.hpp"
#include "scene/components/SceneDetailSwitchComponentStore.hpp"
#include "scene/components/SceneVisibilityBlockerComponentStore.hpp"
#include "scene/components/SceneVisibilityCellComponentStore.hpp"
#include "scene/components/SceneRegionPortalComponentStore.hpp"
#include "scene/components/SceneAudioListenerComponentStore.hpp"
#include "scene/components/SceneAudioSourceComponentStore.hpp"
#include "scene/components/SceneAnimatorComponentStore.hpp"
#include "scene/components/SceneBehaviourComponentStore.hpp"
#include "scene/components/SceneCameraComponentStore.hpp"
#include "scene/components/SceneCharacterControllerComponentStore.hpp"
#include "scene/components/SceneColliderComponentStore.hpp"
#include "scene/components/SceneInputComponentStore.hpp"
#include "scene/components/SceneJointComponentStore.hpp"
#include "scene/components/SceneLightComponentStore.hpp"
#include "scene/components/SceneMeshRendererComponentStore.hpp"
#include "scene/components/SceneNavigationComponentStore.hpp"
#include "scene/components/SceneRigidbodyComponentStore.hpp"
#include "scene/components/SceneTagsComponentStore.hpp"
#include "scene/components/SceneTransformComponentStore.hpp"
#include "scene/components/SceneVisibilityComponentStore.hpp"
#include "scene/components/SceneUIDocumentComponentStore.hpp"

namespace kb::ecs {

class World;

} // namespace kb::ecs

namespace kb::scene {

class SceneComponentRegistry;

class SceneComponentStorage {
public:
    SceneComponentStorage(kb::ecs::World& world, const SceneComponentRegistry& components) noexcept;

    void SetDefaults(SceneEntity entity, const TransformComponent& transform, const VisibilityComponent& visibility);

    [[nodiscard]] const SceneTransformComponentStore& Transforms() const noexcept;
    [[nodiscard]] SceneTransformComponentStore& Transforms() noexcept;
    [[nodiscard]] const SceneVisibilityComponentStore& Visibility() const noexcept;
    [[nodiscard]] SceneVisibilityComponentStore& Visibility() noexcept;
    [[nodiscard]] const SceneBehaviourComponentStore& Behaviours() const noexcept;
    [[nodiscard]] SceneBehaviourComponentStore& Behaviours() noexcept;
    [[nodiscard]] const SceneCameraComponentStore& Cameras() const noexcept;
    [[nodiscard]] SceneCameraComponentStore& Cameras() noexcept;
    [[nodiscard]] const SceneMeshRendererComponentStore& MeshRenderers() const noexcept;
    [[nodiscard]] SceneMeshRendererComponentStore& MeshRenderers() noexcept;
    [[nodiscard]] const SceneLightComponentStore& Lights() const noexcept;
    [[nodiscard]] SceneLightComponentStore& Lights() noexcept;
    [[nodiscard]] const SceneInputComponentStore& Inputs() const noexcept;
    [[nodiscard]] SceneInputComponentStore& Inputs() noexcept;
    [[nodiscard]] const SceneRigidbodyComponentStore& Rigidbodies() const noexcept;
    [[nodiscard]] SceneRigidbodyComponentStore& Rigidbodies() noexcept;
    [[nodiscard]] const SceneColliderComponentStore& Colliders() const noexcept;
    [[nodiscard]] SceneColliderComponentStore& Colliders() noexcept;
    [[nodiscard]] const SceneCharacterControllerComponentStore& CharacterControllers() const noexcept;
    [[nodiscard]] SceneCharacterControllerComponentStore& CharacterControllers() noexcept;
    [[nodiscard]] const SceneJointComponentStore& Joints() const noexcept;
    [[nodiscard]] SceneJointComponentStore& Joints() noexcept;
    [[nodiscard]] const SceneTagsComponentStore& Tags() const noexcept;
    [[nodiscard]] SceneTagsComponentStore& Tags() noexcept;
    [[nodiscard]] const SceneRegionShapeComponentStore& RegionShapes() const noexcept;
    [[nodiscard]] SceneRegionShapeComponentStore& RegionShapes() noexcept;
    [[nodiscard]] const SceneGuideCurveComponentStore& GuideCurves() const noexcept;
    [[nodiscard]] SceneGuideCurveComponentStore& GuideCurves() noexcept;
    [[nodiscard]] const SceneContentInstanceComponentStore& ContentInstances() const noexcept;
    [[nodiscard]] SceneContentInstanceComponentStore& ContentInstances() noexcept;
    [[nodiscard]] const SceneStreamFocusComponentStore& StreamFocuses() const noexcept;
    [[nodiscard]] SceneStreamFocusComponentStore& StreamFocuses() noexcept;
    [[nodiscard]] const SceneWorldBackdropComponentStore& WorldBackdrops() const noexcept;
    [[nodiscard]] SceneWorldBackdropComponentStore& WorldBackdrops() noexcept;
    [[nodiscard]] const SceneAmbientRadianceComponentStore& AmbientRadiances() const noexcept;
    [[nodiscard]] SceneAmbientRadianceComponentStore& AmbientRadiances() noexcept;
    [[nodiscard]] const SceneDetailSwitchComponentStore& DetailSwitches() const noexcept;
    [[nodiscard]] SceneDetailSwitchComponentStore& DetailSwitches() noexcept;
    [[nodiscard]] const SceneVisibilityBlockerComponentStore& VisibilityBlockers() const noexcept;
    [[nodiscard]] SceneVisibilityBlockerComponentStore& VisibilityBlockers() noexcept;
    [[nodiscard]] const SceneVisibilityCellComponentStore& VisibilityCells() const noexcept;
    [[nodiscard]] SceneVisibilityCellComponentStore& VisibilityCells() noexcept;
    [[nodiscard]] const SceneRegionPortalComponentStore& RegionPortals() const noexcept;
    [[nodiscard]] SceneRegionPortalComponentStore& RegionPortals() noexcept;
    [[nodiscard]] const SceneAudioSourceComponentStore& AudioSources() const noexcept;
    [[nodiscard]] SceneAudioSourceComponentStore& AudioSources() noexcept;
    [[nodiscard]] const SceneAudioListenerComponentStore& AudioListeners() const noexcept;
    [[nodiscard]] SceneAudioListenerComponentStore& AudioListeners() noexcept;
    [[nodiscard]] const SceneAnimatorComponentStore& Animators() const noexcept;
    [[nodiscard]] SceneAnimatorComponentStore& Animators() noexcept;
    [[nodiscard]] const SceneUIDocumentComponentStore& UIDocuments() const noexcept;
    [[nodiscard]] SceneUIDocumentComponentStore& UIDocuments() noexcept;
    [[nodiscard]] const SceneNavigationComponentStore& Navigation() const noexcept;
    [[nodiscard]] SceneNavigationComponentStore& Navigation() noexcept;

private:
    SceneTransformComponentStore transforms_;
    SceneVisibilityComponentStore visibility_;
    SceneBehaviourComponentStore behaviours_;
    SceneCameraComponentStore cameras_;
    SceneMeshRendererComponentStore meshRenderers_;
    SceneLightComponentStore lights_;
    SceneInputComponentStore inputs_;
    SceneRigidbodyComponentStore rigidbodies_;
    SceneColliderComponentStore colliders_;
    SceneCharacterControllerComponentStore characterControllers_;
    SceneJointComponentStore joints_;
    SceneTagsComponentStore tags_;
    SceneRegionShapeComponentStore regionShapes_;
    SceneGuideCurveComponentStore guideCurves_;
    SceneContentInstanceComponentStore contentInstances_;
    SceneStreamFocusComponentStore streamFocuses_;
    SceneWorldBackdropComponentStore worldBackdrops_;
    SceneAmbientRadianceComponentStore ambientRadiances_;
    SceneDetailSwitchComponentStore detailSwitches_;
    SceneVisibilityBlockerComponentStore visibilityBlockers_;
    SceneVisibilityCellComponentStore visibilityCells_;
    SceneRegionPortalComponentStore regionPortals_;
    SceneAudioSourceComponentStore audioSources_;
    SceneAudioListenerComponentStore audioListeners_;
    SceneAnimatorComponentStore animators_;
    SceneUIDocumentComponentStore uiDocuments_;
    SceneNavigationComponentStore navigation_;
};

} // namespace kb::scene
