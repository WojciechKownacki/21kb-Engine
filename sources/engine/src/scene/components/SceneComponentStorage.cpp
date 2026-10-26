#include "scene/components/SceneComponentStorage.hpp"

#include "scene/components/SceneComponentRegistry.hpp"

namespace kb::scene {

SceneComponentStorage::SceneComponentStorage(kb::ecs::World& world, const SceneComponentRegistry& components) noexcept
    : transforms_(world, components.TransformComponentId())
    , visibility_(world, components.VisibilityComponentId())
    , behaviours_(world, components.BehaviourComponentId())
    , cameras_(world, components.CameraComponentId())
    , meshRenderers_(world, components.MeshRendererComponentId())
    , lights_(world, components.LightComponentId())
    , inputs_(world, components.InputComponentId())
    , rigidbodies_(world, components.RigidbodyComponentId())
    , colliders_(world, components.ColliderComponentId())
    , characterControllers_(world, components.CharacterControllerComponentId())
    , joints_(world, components.JointComponentId())
    , tags_(world, components.TagsComponentId())
    , regionShapes_(world, components.RegionShapeComponentId())
    , guideCurves_(world, components.GuideCurveComponentId())
    , contentInstances_(world, components.ContentInstanceComponentId())
    , grids_(world, components.GridComponentId())
    , tilemaps_(world, components.TilemapComponentId())
    , tilemapRenderers_(world, components.TilemapRendererComponentId())
    , tilemapColliders_(world, components.TilemapColliderComponentId())
    , characterControllers2D_(world, components.CharacterController2DComponentId())
    , colliders2D_(world, components.Collider2DComponentId())
    , bodies2D_(world, components.Body2DComponentId())
    , sprites2D_(world, components.Sprite2DComponentId())
    , streamFocuses_(world, components.StreamFocusComponentId())
    , worldBackdrops_(world, components.WorldBackdropComponentId())
    , ambientRadiances_(world, components.AmbientRadianceComponentId())
    , detailSwitches_(world, components.DetailSwitchComponentId())
    , visibilityBlockers_(world, components.VisibilityBlockerComponentId())
    , visibilityCells_(world, components.VisibilityCellComponentId())
    , regionPortals_(world, components.RegionPortalComponentId())
    , auxFrames_(world, components.AuxFrameComponentId())
    , geometrySwarms_(world, components.GeometrySwarmComponentId())
    , surfaceCasts_(world, components.SurfaceCastComponentId())
    , facingPanels_(world, components.FacingPanelComponentId())
    , spaceStrokes_(world, components.SpaceStrokeComponentId())
    , historyRibbons_(world, components.HistoryRibbonComponentId())
    , particleEffects_(world, components.ParticleEffectComponentId())
    , lensEchoes_(world, components.LensEchoComponentId())
    , audioSources_(world, components.AudioSourceComponentId())
    , audioListeners_(world, components.AudioListenerComponentId())
    , animators_(world, components.AnimatorComponentId())
    , skeletonBindings_(world, components.SkeletonBindingComponentId())
    , socketAttachments_(world, components.SocketAttachmentComponentId())
    , combatants_(world, components.CombatantComponentId())
    , aiBrains_(world, components.AIBrainComponentId())
    , motionSkeletonRules_(world, components.MotionSkeletonRuleComponentId())
    , deformedGeometries_(world, components.DeformedGeometryComponentId())
    , navigation_(world) {}

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

const SceneBehaviourComponentStore& SceneComponentStorage::Behaviours() const noexcept {
    return behaviours_;
}

SceneBehaviourComponentStore& SceneComponentStorage::Behaviours() noexcept {
    return behaviours_;
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

const SceneInputComponentStore& SceneComponentStorage::Inputs() const noexcept {
    return inputs_;
}

SceneInputComponentStore& SceneComponentStorage::Inputs() noexcept {
    return inputs_;
}

const SceneRigidbodyComponentStore& SceneComponentStorage::Rigidbodies() const noexcept {
    return rigidbodies_;
}

SceneRigidbodyComponentStore& SceneComponentStorage::Rigidbodies() noexcept {
    return rigidbodies_;
}

const SceneColliderComponentStore& SceneComponentStorage::Colliders() const noexcept {
    return colliders_;
}

SceneColliderComponentStore& SceneComponentStorage::Colliders() noexcept {
    return colliders_;
}

const SceneCharacterControllerComponentStore& SceneComponentStorage::CharacterControllers() const noexcept {
    return characterControllers_;
}

SceneCharacterControllerComponentStore& SceneComponentStorage::CharacterControllers() noexcept {
    return characterControllers_;
}

const SceneJointComponentStore& SceneComponentStorage::Joints() const noexcept {
    return joints_;
}

SceneJointComponentStore& SceneComponentStorage::Joints() noexcept {
    return joints_;
}

const SceneTagsComponentStore& SceneComponentStorage::Tags() const noexcept {
    return tags_;
}

SceneTagsComponentStore& SceneComponentStorage::Tags() noexcept {
    return tags_;
}

const SceneRegionShapeComponentStore& SceneComponentStorage::RegionShapes() const noexcept {
    return regionShapes_;
}

SceneRegionShapeComponentStore& SceneComponentStorage::RegionShapes() noexcept {
    return regionShapes_;
}
const SceneGuideCurveComponentStore& SceneComponentStorage::GuideCurves() const noexcept { return guideCurves_; }
SceneGuideCurveComponentStore& SceneComponentStorage::GuideCurves() noexcept { return guideCurves_; }
const SceneContentInstanceComponentStore& SceneComponentStorage::ContentInstances() const noexcept { return contentInstances_; }
const SceneGridComponentStore& SceneComponentStorage::Grids() const noexcept { return grids_; }
const SceneTilemapComponentStore& SceneComponentStorage::Tilemaps() const noexcept { return tilemaps_; }
const SceneTilemapRendererComponentStore& SceneComponentStorage::TilemapRenderers() const noexcept { return tilemapRenderers_; }
const SceneTilemapColliderComponentStore& SceneComponentStorage::TilemapColliders() const noexcept { return tilemapColliders_; }
const SceneCharacterController2DComponentStore& SceneComponentStorage::CharacterControllers2D() const noexcept { return characterControllers2D_; }
const SceneCollider2DComponentStore& SceneComponentStorage::Colliders2D() const noexcept { return colliders2D_; }
const SceneBody2DComponentStore& SceneComponentStorage::Bodies2D() const noexcept { return bodies2D_; }
const SceneSprite2DComponentStore& SceneComponentStorage::Sprites2D() const noexcept { return sprites2D_; }
SceneContentInstanceComponentStore& SceneComponentStorage::ContentInstances() noexcept { return contentInstances_; }
SceneGridComponentStore& SceneComponentStorage::Grids() noexcept { return grids_; }
SceneTilemapComponentStore& SceneComponentStorage::Tilemaps() noexcept { return tilemaps_; }
SceneTilemapRendererComponentStore& SceneComponentStorage::TilemapRenderers() noexcept { return tilemapRenderers_; }
SceneTilemapColliderComponentStore& SceneComponentStorage::TilemapColliders() noexcept { return tilemapColliders_; }
SceneCharacterController2DComponentStore& SceneComponentStorage::CharacterControllers2D() noexcept { return characterControllers2D_; }
SceneCollider2DComponentStore& SceneComponentStorage::Colliders2D() noexcept { return colliders2D_; }
SceneBody2DComponentStore& SceneComponentStorage::Bodies2D() noexcept { return bodies2D_; }
SceneSprite2DComponentStore& SceneComponentStorage::Sprites2D() noexcept { return sprites2D_; }
const SceneStreamFocusComponentStore& SceneComponentStorage::StreamFocuses() const noexcept { return streamFocuses_; }
SceneStreamFocusComponentStore& SceneComponentStorage::StreamFocuses() noexcept { return streamFocuses_; }
const SceneWorldBackdropComponentStore& SceneComponentStorage::WorldBackdrops() const noexcept { return worldBackdrops_; }
SceneWorldBackdropComponentStore& SceneComponentStorage::WorldBackdrops() noexcept { return worldBackdrops_; }
const SceneAmbientRadianceComponentStore& SceneComponentStorage::AmbientRadiances() const noexcept { return ambientRadiances_; }
SceneAmbientRadianceComponentStore& SceneComponentStorage::AmbientRadiances() noexcept { return ambientRadiances_; }
const SceneDetailSwitchComponentStore& SceneComponentStorage::DetailSwitches() const noexcept { return detailSwitches_; }
SceneDetailSwitchComponentStore& SceneComponentStorage::DetailSwitches() noexcept { return detailSwitches_; }
const SceneVisibilityBlockerComponentStore& SceneComponentStorage::VisibilityBlockers() const noexcept { return visibilityBlockers_; }
SceneVisibilityBlockerComponentStore& SceneComponentStorage::VisibilityBlockers() noexcept { return visibilityBlockers_; }
const SceneVisibilityCellComponentStore& SceneComponentStorage::VisibilityCells() const noexcept { return visibilityCells_; }
SceneVisibilityCellComponentStore& SceneComponentStorage::VisibilityCells() noexcept { return visibilityCells_; }
const SceneRegionPortalComponentStore& SceneComponentStorage::RegionPortals() const noexcept { return regionPortals_; }
SceneRegionPortalComponentStore& SceneComponentStorage::RegionPortals() noexcept { return regionPortals_; }
const SceneAuxFrameComponentStore& SceneComponentStorage::AuxFrames() const noexcept { return auxFrames_; }
SceneAuxFrameComponentStore& SceneComponentStorage::AuxFrames() noexcept { return auxFrames_; }
const SceneGeometrySwarmComponentStore& SceneComponentStorage::GeometrySwarms() const noexcept { return geometrySwarms_; }
SceneGeometrySwarmComponentStore& SceneComponentStorage::GeometrySwarms() noexcept { return geometrySwarms_; }
const SceneSurfaceCastComponentStore& SceneComponentStorage::SurfaceCasts() const noexcept { return surfaceCasts_; }
SceneSurfaceCastComponentStore& SceneComponentStorage::SurfaceCasts() noexcept { return surfaceCasts_; }
const SceneFacingPanelComponentStore& SceneComponentStorage::FacingPanels() const noexcept { return facingPanels_; }
SceneFacingPanelComponentStore& SceneComponentStorage::FacingPanels() noexcept { return facingPanels_; }
const SceneSpaceStrokeComponentStore& SceneComponentStorage::SpaceStrokes() const noexcept { return spaceStrokes_; }
SceneSpaceStrokeComponentStore& SceneComponentStorage::SpaceStrokes() noexcept { return spaceStrokes_; }
const SceneHistoryRibbonComponentStore& SceneComponentStorage::HistoryRibbons() const noexcept { return historyRibbons_; }
SceneHistoryRibbonComponentStore& SceneComponentStorage::HistoryRibbons() noexcept { return historyRibbons_; }
const SceneParticleEffectComponentStore& SceneComponentStorage::ParticleEffects() const noexcept {
    return particleEffects_;
}
SceneParticleEffectComponentStore& SceneComponentStorage::ParticleEffects() noexcept { return particleEffects_; }
const SceneLensEchoComponentStore& SceneComponentStorage::LensEchoes() const noexcept { return lensEchoes_; }
SceneLensEchoComponentStore& SceneComponentStorage::LensEchoes() noexcept { return lensEchoes_; }

const SceneAudioSourceComponentStore& SceneComponentStorage::AudioSources() const noexcept {
    return audioSources_;
}

SceneAudioSourceComponentStore& SceneComponentStorage::AudioSources() noexcept {
    return audioSources_;
}

const SceneAudioListenerComponentStore& SceneComponentStorage::AudioListeners() const noexcept {
    return audioListeners_;
}

SceneAudioListenerComponentStore& SceneComponentStorage::AudioListeners() noexcept {
    return audioListeners_;
}

const SceneAnimatorComponentStore& SceneComponentStorage::Animators() const noexcept { return animators_; }
SceneAnimatorComponentStore& SceneComponentStorage::Animators() noexcept { return animators_; }
const SceneSkeletonBindingComponentStore& SceneComponentStorage::SkeletonBindings() const noexcept { return skeletonBindings_; }
SceneSkeletonBindingComponentStore& SceneComponentStorage::SkeletonBindings() noexcept { return skeletonBindings_; }
const SceneSocketAttachmentComponentStore& SceneComponentStorage::SocketAttachments() const noexcept { return socketAttachments_; }
SceneSocketAttachmentComponentStore& SceneComponentStorage::SocketAttachments() noexcept { return socketAttachments_; }
const SceneCombatantComponentStore& SceneComponentStorage::Combatants() const noexcept { return combatants_; }
SceneCombatantComponentStore& SceneComponentStorage::Combatants() noexcept { return combatants_; }
const SceneAIBrainComponentStore& SceneComponentStorage::AIBrains() const noexcept { return aiBrains_; }
SceneAIBrainComponentStore& SceneComponentStorage::AIBrains() noexcept { return aiBrains_; }
const SceneMotionSkeletonRuleComponentStore& SceneComponentStorage::MotionSkeletonRules() const noexcept { return motionSkeletonRules_; }
SceneMotionSkeletonRuleComponentStore& SceneComponentStorage::MotionSkeletonRules() noexcept { return motionSkeletonRules_; }
const SceneDeformedGeometryComponentStore& SceneComponentStorage::DeformedGeometries() const noexcept { return deformedGeometries_; }
SceneDeformedGeometryComponentStore& SceneComponentStorage::DeformedGeometries() noexcept { return deformedGeometries_; }
const SceneNavigationComponentStore& SceneComponentStorage::Navigation() const noexcept { return navigation_; }
SceneNavigationComponentStore& SceneComponentStorage::Navigation() noexcept { return navigation_; }

} // namespace kb::scene
