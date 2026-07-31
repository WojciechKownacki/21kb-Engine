#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"

namespace kb::scene {

SceneComponentQueries::SceneComponentQueries(const Scene& scene) noexcept
    : scene_(scene) {}

SceneComponents::SceneComponents(Scene& scene) noexcept
    : scene_(scene) {}

SceneVisibilityComponentQueries SceneComponentQueries::Visibility() const noexcept {
    return SceneVisibilityComponentQueries{ scene_ };
}

SceneBehaviourComponentQueries SceneComponentQueries::Behaviours() const noexcept {
    return SceneBehaviourComponentQueries{ scene_ };
}

SceneCameraComponentQueries SceneComponentQueries::Cameras() const noexcept {
    return SceneCameraComponentQueries{ scene_ };
}

SceneMeshRendererComponentQueries SceneComponentQueries::MeshRenderers() const noexcept {
    return SceneMeshRendererComponentQueries{ scene_ };
}

SceneLightComponentQueries SceneComponentQueries::Lights() const noexcept {
    return SceneLightComponentQueries{ scene_ };
}

SceneInputComponentQueries SceneComponentQueries::Inputs() const noexcept {
    return SceneInputComponentQueries{ scene_ };
}

SceneRigidbodyComponentQueries SceneComponentQueries::Rigidbodies() const noexcept {
    return SceneRigidbodyComponentQueries{ scene_ };
}

SceneColliderComponentQueries SceneComponentQueries::Colliders() const noexcept {
    return SceneColliderComponentQueries{ scene_ };
}

SceneCharacterControllerComponentQueries SceneComponentQueries::CharacterControllers() const noexcept {
    return SceneCharacterControllerComponentQueries{ scene_ };
}

SceneJointComponentQueries SceneComponentQueries::Joints() const noexcept {
    return SceneJointComponentQueries{ scene_ };
}

SceneTagsComponentQueries SceneComponentQueries::Tags() const noexcept {
    return SceneTagsComponentQueries{ scene_ };
}

SceneRegionShapeComponentQueries SceneComponentQueries::RegionShapes() const noexcept {
    return SceneRegionShapeComponentQueries{ scene_ };
}
SceneGuideCurveComponentQueries SceneComponentQueries::GuideCurves() const noexcept { return SceneGuideCurveComponentQueries{ scene_ }; }
SceneContentInstanceComponentQueries SceneComponentQueries::ContentInstances() const noexcept { return SceneContentInstanceComponentQueries{ scene_ }; }
SceneStreamFocusComponentQueries SceneComponentQueries::StreamFocuses() const noexcept { return SceneStreamFocusComponentQueries{ scene_ }; }
SceneWorldBackdropComponentQueries SceneComponentQueries::WorldBackdrops() const noexcept { return SceneWorldBackdropComponentQueries{ scene_ }; }
SceneAmbientRadianceComponentQueries SceneComponentQueries::AmbientRadiances() const noexcept { return SceneAmbientRadianceComponentQueries{ scene_ }; }
SceneDetailSwitchComponentQueries SceneComponentQueries::DetailSwitches() const noexcept { return SceneDetailSwitchComponentQueries{ scene_ }; }
SceneVisibilityBlockerComponentQueries SceneComponentQueries::VisibilityBlockers() const noexcept { return SceneVisibilityBlockerComponentQueries{ scene_ }; }
SceneVisibilityCellComponentQueries SceneComponentQueries::VisibilityCells() const noexcept { return SceneVisibilityCellComponentQueries{ scene_ }; }
SceneRegionPortalComponentQueries SceneComponentQueries::RegionPortals() const noexcept { return SceneRegionPortalComponentQueries{ scene_ }; }

SceneAudioSourceComponentQueries SceneComponentQueries::AudioSources() const noexcept {
    return SceneAudioSourceComponentQueries{ scene_ };
}

SceneAudioListenerComponentQueries SceneComponentQueries::AudioListeners() const noexcept {
    return SceneAudioListenerComponentQueries{ scene_ };
}

SceneAnimatorComponentQueries SceneComponentQueries::Animators() const noexcept {
    return SceneAnimatorComponentQueries{ scene_ };
}
SceneUIDocumentComponentQueries SceneComponentQueries::UIDocuments() const noexcept {
    return SceneUIDocumentComponentQueries{ scene_ };
}
SceneNavAgentComponentQueries SceneComponentQueries::NavAgents() const noexcept { return SceneNavAgentComponentQueries{ scene_ }; }
SceneNavObstacleComponentQueries SceneComponentQueries::NavObstacles() const noexcept { return SceneNavObstacleComponentQueries{ scene_ }; }

SceneComponentVisitors SceneComponentQueries::Visitors() const noexcept {
    return SceneComponentVisitors{ scene_ };
}

SceneVisibilityComponents SceneComponents::Visibility() const noexcept {
    return SceneVisibilityComponents{ scene_ };
}

SceneBehaviourComponents SceneComponents::Behaviours() const noexcept {
    return SceneBehaviourComponents{ scene_ };
}

SceneCameraComponents SceneComponents::Cameras() const noexcept {
    return SceneCameraComponents{ scene_ };
}

SceneMeshRendererComponents SceneComponents::MeshRenderers() const noexcept {
    return SceneMeshRendererComponents{ scene_ };
}

SceneLightComponents SceneComponents::Lights() const noexcept {
    return SceneLightComponents{ scene_ };
}

SceneInputComponents SceneComponents::Inputs() const noexcept {
    return SceneInputComponents{ scene_ };
}

SceneRigidbodyComponents SceneComponents::Rigidbodies() const noexcept {
    return SceneRigidbodyComponents{ scene_ };
}

SceneColliderComponents SceneComponents::Colliders() const noexcept {
    return SceneColliderComponents{ scene_ };
}

SceneCharacterControllerComponents SceneComponents::CharacterControllers() const noexcept {
    return SceneCharacterControllerComponents{ scene_ };
}

SceneJointComponents SceneComponents::Joints() const noexcept {
    return SceneJointComponents{ scene_ };
}

SceneTagsComponents SceneComponents::Tags() const noexcept {
    return SceneTagsComponents{ scene_ };
}

SceneRegionShapeComponents SceneComponents::RegionShapes() const noexcept {
    return SceneRegionShapeComponents{ scene_ };
}
SceneGuideCurveComponents SceneComponents::GuideCurves() const noexcept { return SceneGuideCurveComponents{ scene_ }; }
SceneContentInstanceComponents SceneComponents::ContentInstances() const noexcept { return SceneContentInstanceComponents{ scene_ }; }
SceneStreamFocusComponents SceneComponents::StreamFocuses() const noexcept { return SceneStreamFocusComponents{ scene_ }; }
SceneWorldBackdropComponents SceneComponents::WorldBackdrops() const noexcept { return SceneWorldBackdropComponents{ scene_ }; }
SceneAmbientRadianceComponents SceneComponents::AmbientRadiances() const noexcept { return SceneAmbientRadianceComponents{ scene_ }; }
SceneDetailSwitchComponents SceneComponents::DetailSwitches() const noexcept { return SceneDetailSwitchComponents{ scene_ }; }
SceneVisibilityBlockerComponents SceneComponents::VisibilityBlockers() const noexcept { return SceneVisibilityBlockerComponents{ scene_ }; }
SceneVisibilityCellComponents SceneComponents::VisibilityCells() const noexcept { return SceneVisibilityCellComponents{ scene_ }; }
SceneRegionPortalComponents SceneComponents::RegionPortals() const noexcept { return SceneRegionPortalComponents{ scene_ }; }

SceneAudioSourceComponents SceneComponents::AudioSources() const noexcept {
    return SceneAudioSourceComponents{ scene_ };
}

SceneAudioListenerComponents SceneComponents::AudioListeners() const noexcept {
    return SceneAudioListenerComponents{ scene_ };
}

SceneAnimatorComponents SceneComponents::Animators() const noexcept {
    return SceneAnimatorComponents{ scene_ };
}
SceneUIDocumentComponents SceneComponents::UIDocuments() const noexcept {
    return SceneUIDocumentComponents{ scene_ };
}
SceneNavAgentComponents SceneComponents::NavAgents() const noexcept { return SceneNavAgentComponents{ scene_ }; }
SceneNavObstacleComponents SceneComponents::NavObstacles() const noexcept { return SceneNavObstacleComponents{ scene_ }; }

SceneComponentVisitors SceneComponents::Visitors() const noexcept {
    return SceneComponentVisitors{ scene_ };
}

} // namespace kb::scene
