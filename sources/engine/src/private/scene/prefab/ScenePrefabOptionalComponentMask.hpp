#pragma once

#include "engine/ecs/ComponentId.hpp"
#include "engine/ecs/NativeArchetypeStorage.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/ScenePrefabNode.hpp"
#include "scene/SceneState.hpp"
#include "scene/components/SceneComponentRegistry.hpp"

#include <array>

namespace kb::scene {

struct ScenePrefabOptionalComponentMaskMatch {
    bool available = false;
    bool matches = false;
};

struct ScenePrefabOptionalComponentExpectation {
    kb::ecs::ComponentId componentId = 0U;
    bool expectedPresent = false;
};

[[nodiscard]] inline std::array<ScenePrefabOptionalComponentExpectation, 37U>
ScenePrefabOptionalComponentExpectations(
    const ScenePrefabNodeComponents& components,
    const SceneComponentRegistry& registry) noexcept {
    return {{
        { registry.CameraComponentId(), components.camera.has_value() },
        { registry.MeshRendererComponentId(), components.meshRenderer.has_value() },
        { registry.LightComponentId(), components.light.has_value() },
        { registry.InputComponentId(), components.input.has_value() },
        { registry.RigidbodyComponentId(), components.rigidbody.has_value() },
        { registry.ColliderComponentId(), components.collider.has_value() },
        { registry.CharacterControllerComponentId(), components.characterController.has_value() },
        { registry.JointComponentId(), components.joint.has_value() },
        { registry.TagsComponentId(), components.tags.has_value() },
        { registry.RegionShapeComponentId(), components.regionShape.has_value() },
        { registry.GuideCurveComponentId(), components.guideCurve.has_value() },
        { registry.ContentInstanceComponentId(), components.contentInstance.has_value() },
        { registry.StreamFocusComponentId(), components.streamFocus.has_value() },
        { registry.WorldBackdropComponentId(), components.worldBackdrop.has_value() },
        { registry.AmbientRadianceComponentId(), components.ambientRadiance.has_value() },
        { registry.DetailSwitchComponentId(), components.detailSwitch.has_value() },
        { registry.VisibilityBlockerComponentId(), components.visibilityBlocker.has_value() },
        { registry.VisibilityCellComponentId(), components.visibilityCell.has_value() },
        { registry.RegionPortalComponentId(), components.regionPortal.has_value() },
        { registry.AuxFrameComponentId(), components.auxFrame.has_value() },
        { registry.GeometrySwarmComponentId(), components.geometrySwarm.has_value() },
        { registry.SurfaceCastComponentId(), components.surfaceCast.has_value() },
        { registry.FacingPanelComponentId(), components.facingPanel.has_value() },
        { registry.SpaceStrokeComponentId(), components.spaceStroke.has_value() },
        { registry.HistoryRibbonComponentId(), components.historyRibbon.has_value() },
        { registry.ParticleEffectComponentId(), components.particleEffect.has_value() },
        { registry.LensEchoComponentId(), components.lensEcho.has_value() },
        { registry.BehaviourComponentId(), components.behaviour.has_value() },
        { registry.AudioSourceComponentId(), components.audioSource.has_value() },
        { registry.AudioListenerComponentId(), components.audioListener.has_value() },
        { registry.AnimatorComponentId(), components.animator.has_value() },
        { registry.SkeletonBindingComponentId(), components.skeletonBinding.has_value() },
        { registry.MotionSkeletonRuleComponentId(), components.motionSkeletonRule.has_value() },
        { registry.DeformedGeometryComponentId(), components.deformedGeometry.has_value() },
        { registry.UIDocumentComponentId(), components.uiDocument.has_value() },
        { registry.NavAgentComponentId(), components.navAgent.has_value() },
        { registry.NavObstacleComponentId(), components.navObstacle.has_value() },
    }};
}

[[nodiscard]] inline ScenePrefabOptionalComponentMaskMatch ScenePrefabOptionalComponentMaskMatches(
    const SceneState& state,
    SceneEntity entity,
    const ScenePrefabNodeComponents& expected) noexcept {
    const kb::ecs::NativeArchetypeStorage& storage = state.world.NativeStorage();
    if (!storage.IsAlive(entity)) return {};

    const auto expectations = ScenePrefabOptionalComponentExpectations(expected, state.components);
    for (const ScenePrefabOptionalComponentExpectation& expectation : expectations) {
        if (expectation.componentId == 0U) return {};
        if (storage.HasComponent(entity, expectation.componentId) != expectation.expectedPresent) {
            return { .available = true, .matches = false };
        }
    }
    return { .available = true, .matches = true };
}

} // namespace kb::scene
