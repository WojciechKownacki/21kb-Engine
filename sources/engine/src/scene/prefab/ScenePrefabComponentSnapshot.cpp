#include "scene/prefab/ScenePrefabComponentSnapshot.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneObject.hpp"

namespace kb::scene {

ScenePrefabNodeComponents ScenePrefabComponentSnapshot::Capture(Scene& scene, SceneObject object) {
    ScenePrefabNodeComponents components;
    const SceneEntity entity = object.Entity();
    SceneComponents sceneComponents = scene.Components();

    if (const CameraComponent* camera = sceneComponents.Cameras().TryGet(entity)) {
        components.camera = *camera;
    }

    if (const MeshRendererComponent* meshRenderer = sceneComponents.MeshRenderers().TryGet(entity)) {
        components.meshRenderer = *meshRenderer;
    }

    if (const LightComponent* light = sceneComponents.Lights().TryGet(entity)) {
        components.light = *light;
    }

    if (const InputComponent* input = sceneComponents.Inputs().TryGet(entity)) {
        components.input = *input;
    }

    if (const RigidbodyComponent* rigidbody = sceneComponents.Rigidbodies().TryGet(entity)) {
        components.rigidbody = *rigidbody;
    }

    if (const ColliderComponent* collider = sceneComponents.Colliders().TryGet(entity)) {
        components.collider = *collider;
    }

    if (const CharacterControllerComponent* characterController = sceneComponents.CharacterControllers().TryGet(entity)) {
        components.characterController = *characterController;
    }

    if (const JointComponent* joint = sceneComponents.Joints().TryGet(entity)) {
        // CaptureService resolves this transient entity id to a stable prefab-node
        // id before the prefab can leave capture. It is never serialized.
        components.joint = ScenePrefabJointComponent{
            .type = joint->type,
            .connectedNodeStableId = joint->connectedEntity.Id(),
            .anchor = joint->anchor,
            .connectedAnchor = joint->connectedAnchor,
            .axis = joint->axis,
            .minLimit = joint->minLimit,
            .maxLimit = joint->maxLimit,
            .enableLimit = joint->enableLimit,
        };
    }

    if (const TagsComponent* tags = sceneComponents.Tags().TryGet(entity)) {
        components.tags = *tags;
    }
    if (const RegionShapeComponent* regionShape = sceneComponents.RegionShapes().TryGet(entity)) {
        components.regionShape = *regionShape;
    }
    if (const GuideCurveComponent* guideCurve = sceneComponents.GuideCurves().TryGet(entity)) {
        components.guideCurve = *guideCurve;
    }
    if (const ContentInstanceComponent* content = sceneComponents.ContentInstances().TryGet(entity)) {
        components.contentInstance = *content;
    }
    if (const StreamFocusComponent* focus = sceneComponents.StreamFocuses().TryGet(entity)) {
        components.streamFocus = *focus;
    }
    if (const WorldBackdropComponent* backdrop = sceneComponents.WorldBackdrops().TryGet(entity)) {
        components.worldBackdrop = *backdrop;
    }
    if (const AmbientRadianceComponent* ambient = sceneComponents.AmbientRadiances().TryGet(entity)) {
        components.ambientRadiance = *ambient;
    }
    if (const SceneDetailSwitchComponent* detail = sceneComponents.DetailSwitches().TryGet(entity)) {
        components.detailSwitch = *detail;
    }
    if (const SceneVisibilityBlockerComponent* blocker = sceneComponents.VisibilityBlockers().TryGet(entity)) components.visibilityBlocker = *blocker;
    if (const VisibilityCellComponent* cell = sceneComponents.VisibilityCells().TryGet(entity)) components.visibilityCell = *cell;
    if (const SceneRegionPortalComponent* portal = sceneComponents.RegionPortals().TryGet(entity)) {
        components.regionPortal = ScenePrefabRegionPortalComponent{
            .sourceCellNodeStableId = portal->sourceCell.Id(), .targetCellNodeStableId = portal->targetCell.Id(),
            .purposes = portal->purposes, .enabled = portal->enabled,
        };
    }
    if (const AuxFrameComponent* auxFrame = sceneComponents.AuxFrames().TryGet(entity)) {
        components.auxFrame = *auxFrame;
    }
    if (const GeometrySwarmComponent* swarm = sceneComponents.GeometrySwarms().TryGet(entity)) {
        components.geometrySwarm = *swarm;
    }
    if (const SurfaceCastComponent* surfaceCast = sceneComponents.SurfaceCasts().TryGet(entity)) {
        components.surfaceCast = *surfaceCast;
    }
    if (const FacingPanelComponent* facingPanel = sceneComponents.FacingPanels().TryGet(entity)) {
        components.facingPanel = *facingPanel;
    }
    if (const SpaceStrokeComponent* spaceStroke = sceneComponents.SpaceStrokes().TryGet(entity)) {
        components.spaceStroke = *spaceStroke;
    }
    if (const HistoryRibbonComponent* historyRibbon = sceneComponents.HistoryRibbons().TryGet(entity)) {
        components.historyRibbon = *historyRibbon;
    }
    if (const LensEchoComponent* lensEcho = sceneComponents.LensEchoes().TryGet(entity)) {
        components.lensEcho = ScenePrefabLensEchoComponent{
            .sourceNodeStableId = lensEcho->sourceEntityId,
            .profileMaterialAssetId = lensEcho->profileMaterialAssetId,
            .intensity = lensEcho->intensity,
            .size = lensEcho->size,
            .layer = lensEcho->layer,
            .occlusionRule = lensEcho->occlusionRule,
            .enabled = lensEcho->enabled,
        };
    }

    if (const BehaviourComponent* behaviour = sceneComponents.Behaviours().TryGet(entity)) {
        components.behaviour = *behaviour;
    }

    if (const AudioSourceComponent* audioSource = sceneComponents.AudioSources().TryGet(entity)) {
        components.audioSource = *audioSource;
    }

    if (const AudioListenerComponent* audioListener = sceneComponents.AudioListeners().TryGet(entity)) {
        components.audioListener = *audioListener;
    }
    if (const Animator* animator = sceneComponents.Animators().TryGet(entity)) {
        components.animator = *animator;
    }
    if (const UIDocumentComponent* uiDocument = sceneComponents.UIDocuments().TryGet(entity)) {
        components.uiDocument = *uiDocument;
    }
    if (const NavAgent* navAgent = sceneComponents.NavAgents().TryGet(entity)) {
        components.navAgent = *navAgent;
    }
    if (const NavObstacle* navObstacle = sceneComponents.NavObstacles().TryGet(entity)) {
        components.navObstacle = *navObstacle;
    }

    return components;
}

} // namespace kb::scene
