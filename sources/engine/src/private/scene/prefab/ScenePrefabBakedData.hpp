#pragma once

#include "engine/scene/ScenePrefab.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace kb::scene {

enum class ScenePrefabBakedComponentMask : std::uint64_t {
    Camera = 1ULL << 0U,
    MeshRenderer = 1ULL << 1U,
    Light = 1ULL << 2U,
    Input = 1ULL << 3U,
    Rigidbody = 1ULL << 4U,
    Collider = 1ULL << 5U,
    Tags = 1ULL << 6U,
    Behaviour = 1ULL << 7U,
    AudioSource = 1ULL << 8U,
    AudioListener = 1ULL << 9U,
    CharacterController = 1ULL << 10U,
    Joint = 1ULL << 11U,
    Animator = 1ULL << 12U,
    UIDocument = 1ULL << 13U,
    NavAgent = 1ULL << 14U,
    NavObstacle = 1ULL << 15U,
    RegionShape = 1ULL << 16U,
    GuideCurve = 1ULL << 17U,
    ContentInstance = 1ULL << 18U,
    StreamFocus = 1ULL << 19U,
    WorldBackdrop = 1ULL << 20U,
    AmbientRadiance = 1ULL << 21U,
    DetailSwitch = 1ULL << 22U,
    VisibilityBlocker = 1ULL << 23U,
    VisibilityCell = 1ULL << 24U,
    RegionPortal = 1ULL << 25U,
    AuxFrame = 1ULL << 26U,
    GeometrySwarm = 1ULL << 27U,
    SurfaceCast = 1ULL << 28U,
    FacingPanel = 1ULL << 29U,
    SpaceStroke = 1ULL << 30U,
    HistoryRibbon = 1ULL << 31U,
    SkeletonBinding = 1ULL << 32U,
    DeformedGeometry = 1ULL << 33U,
    MotionSkeletonRule = 1ULL << 34U,
    ParticleEffect = 1ULL << 35U,
};

[[nodiscard]] constexpr std::uint64_t ScenePrefabBakedMask(ScenePrefabBakedComponentMask mask) noexcept {
    return static_cast<std::uint64_t>(mask);
}

[[nodiscard]] constexpr bool ScenePrefabBakedMaskHas(std::uint64_t mask, ScenePrefabBakedComponentMask component) noexcept {
    return (mask & ScenePrefabBakedMask(component)) != 0U;
}

struct ScenePrefabBakedArchetype {
    std::uint64_t componentMask = 0U;
    std::vector<std::uint32_t> nodeIndices;
    std::vector<TransformComponent> transforms;
    std::vector<VisibilityComponent> visibility;
    std::vector<CameraComponent> cameras;
    std::vector<MeshRendererComponent> meshRenderers;
    std::vector<LightComponent> lights;
    std::vector<InputComponent> inputs;
    std::vector<RigidbodyComponent> rigidbodies;
    std::vector<ColliderComponent> colliders;
    std::vector<CharacterControllerComponent> characterControllers;
    std::vector<JointComponent> joints;
    std::vector<TagsComponent> tags;
    std::vector<RegionShapeComponent> regionShapes;
    std::vector<GuideCurveComponent> guideCurves;
    std::vector<ContentInstanceComponent> contentInstances;
    std::vector<StreamFocusComponent> streamFocuses;
    std::vector<WorldBackdropComponent> worldBackdrops;
    std::vector<AmbientRadianceComponent> ambientRadiances;
    std::vector<SceneDetailSwitchComponent> detailSwitches;
    std::vector<SceneVisibilityBlockerComponent> visibilityBlockers;
    std::vector<VisibilityCellComponent> visibilityCells;
    std::vector<SceneRegionPortalComponent> regionPortals;
    std::vector<AuxFrameComponent> auxFrames;
    std::vector<GeometrySwarmComponent> geometrySwarms;
    std::vector<SurfaceCastComponent> surfaceCasts;
    std::vector<FacingPanelComponent> facingPanels;
    std::vector<SpaceStrokeComponent> spaceStrokes;
    std::vector<HistoryRibbonComponent> historyRibbons;
    std::vector<ParticleEffectComponent> particleEffects;
    std::vector<BehaviourComponent> behaviours;
    std::vector<AudioSourceComponent> audioSources;
    std::vector<AudioListenerComponent> audioListeners;
    std::vector<Animator> animators;
    std::vector<SkeletonBindingComponent> skeletonBindings;
    std::vector<MotionSkeletonRuleComponent> motionSkeletonRules;
    std::vector<DrawD3DeformedGeometryComponent> deformedGeometries;
    std::vector<UIDocumentComponent> uiDocuments;
    std::vector<NavAgent> navAgents;
    std::vector<NavObstacle> navObstacles;
};

class ScenePrefabBakedData {
public:
    [[nodiscard]] static ScenePrefabBakedData Bake(std::span<const ScenePrefabNodeDesc> nodes);

    [[nodiscard]] std::size_t NodeCount() const noexcept;
    [[nodiscard]] std::span<const ScenePrefabBakedArchetype> Archetypes() const noexcept;

private:
    std::size_t nodeCount_ = 0;
    std::vector<ScenePrefabBakedArchetype> archetypes_;
};

} // namespace kb::scene
