#include "scene/prefab/ScenePrefabBulkInstantiationService.hpp"

#include "engine/ecs/CommandBuffer.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/entities/SceneEntityNaming.hpp"
#include "scene/hierarchy/SceneHierarchyCache.hpp"
#include "scene/prefab/ScenePrefabBakedData.hpp"
#include "scene/prefab/ScenePrefabNameResolver.hpp"
#include "scene/prefab/ScenePrefabValidator.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

namespace kb::scene {
namespace {

using PrefabStatsClock = std::chrono::steady_clock;

[[nodiscard]] std::uint64_t ElapsedNanoseconds(PrefabStatsClock::time_point start, PrefabStatsClock::time_point end) noexcept {
    const std::uint64_t nanoseconds = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    return nanoseconds == 0U ? 1U : nanoseconds;
}

[[nodiscard]] double UnitsPerSecond(std::size_t units, std::uint64_t elapsedNanoseconds) noexcept {
    if (units == 0U || elapsedNanoseconds == 0U) {
        return 0.0;
    }
    return (static_cast<double>(units) * 1'000'000'000.0) / static_cast<double>(elapsedNanoseconds);
}

[[nodiscard]] std::size_t TotalNodeCount(std::size_t instanceCount, std::size_t nodeCount) {
    if (nodeCount != 0 && instanceCount > std::numeric_limits<std::size_t>::max() / nodeCount) {
        throw std::length_error("Scene prefab bulk instantiation node count exceeds addressable size");
    }
    return instanceCount * nodeCount;
}

[[nodiscard]] std::size_t EntityIndex(std::size_t instanceIndex, std::size_t nodeIndex, std::size_t nodeCount) noexcept {
    return instanceIndex * nodeCount + nodeIndex;
}

[[nodiscard]] bool CreatesContiguousPrefabOrderEntityRuns(const ScenePrefabBakedData& baked) noexcept {
    const std::span<const ScenePrefabBakedArchetype> archetypes = baked.Archetypes();
    if (archetypes.size() != 1U || archetypes.front().nodeIndices.size() != baked.NodeCount()) {
        return false;
    }
    const std::vector<std::uint32_t>& nodeIndices = archetypes.front().nodeIndices;
    for (std::size_t index = 0; index < nodeIndices.size(); ++index) {
        if (nodeIndices[index] != index) {
            return false;
        }
    }
    return true;
}

template <typename T>
void RepeatComponents(std::vector<T>& output, std::span<const T> source, std::size_t instanceCount) {
    const std::size_t totalCount = TotalNodeCount(instanceCount, source.size());
    output.clear();
    output.resize(totalCount);
    if (source.empty()) {
        return;
    }

    std::copy(source.begin(), source.end(), output.begin());
    std::size_t filled = source.size();
    while (filled < totalCount) {
        const std::size_t copyCount = std::min(filled, totalCount - filled);
        std::copy_n(output.begin(), copyCount, output.begin() + static_cast<std::ptrdiff_t>(filled));
        filled += copyCount;
    }
}

template <typename T>
void AddCommandComponentView(std::vector<kb::ecs::CommandBuffer::BulkComponentView>& views, std::span<const T> components) {
    views.push_back(kb::ecs::CommandBuffer::MakeBulkComponentView<T>(components));
}

template <typename T>
void AddCommandComponentPatternView(std::vector<kb::ecs::CommandBuffer::BulkComponentView>& views, std::span<const T> components, std::size_t instanceCount) {
    kb::ecs::CommandBuffer::BulkComponentView view = kb::ecs::CommandBuffer::MakeBulkComponentView<T>(components);
    view.componentCount = TotalNodeCount(instanceCount, components.size());
    view.sourceCount = components.size();
    views.push_back(view);
}

template <typename T>
void AddWorldComponentView(std::vector<kb::ecs::World::BulkComponentView>& views, std::span<const T> components) {
    views.push_back(kb::ecs::World::MakeBulkComponentView<T>(components));
}

template <typename T>
void AddWorldComponentPatternView(std::vector<kb::ecs::World::BulkComponentView>& views, std::span<const T> components, std::size_t instanceCount) {
    kb::ecs::World::BulkComponentView view = kb::ecs::World::MakeBulkComponentView<T>(components);
    view.componentCount = TotalNodeCount(instanceCount, components.size());
    view.sourceCount = components.size();
    views.push_back(view);
}

template <typename T>
void AddComponentViews(
    std::vector<kb::ecs::CommandBuffer::BulkComponentView>& commandViews,
    std::vector<kb::ecs::World::BulkComponentView>& worldViews,
    std::span<const T> components) {
    AddCommandComponentView(commandViews, components);
    AddWorldComponentView(worldViews, components);
}

[[nodiscard]] std::vector<std::string> BuildNodeNames(
    std::span<const ScenePrefabNodeDesc> nodes,
    const ScenePrefabInstantiationSettings& settings) {
    std::vector<std::string> names;
    names.reserve(nodes.size());
    for (const ScenePrefabNodeDesc& node : nodes) {
        names.push_back(ScenePrefabNameResolver::Resolve(node, settings));
    }
    return names;
}

struct ScenePrefabArchetypeSpawnPayload {
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
    std::vector<BehaviourComponent> behaviours;
    std::vector<AudioSourceComponent> audioSources;
    std::vector<AudioListenerComponent> audioListeners;
    std::vector<Animator> animators;
    std::vector<UIDocumentComponent> uiDocuments;
    std::vector<NavAgent> navAgents;
    std::vector<NavObstacle> navObstacles;
    std::vector<kb::ecs::CommandBuffer::BulkComponentView> views;
    std::vector<kb::ecs::World::BulkComponentView> worldViews;
    std::vector<kb::ecs::Entity> createdEntities;

    void Build(const ScenePrefabBakedArchetype& archetype, std::size_t instanceCount) {
        RepeatComponents(transforms, std::span<const TransformComponent>{ archetype.transforms }, instanceCount);
        RepeatComponents(visibility, std::span<const VisibilityComponent>{ archetype.visibility }, instanceCount);

        views.clear();
        views.reserve(24U);
        worldViews.clear();
        worldViews.reserve(24U);
        AddComponentViews(views, worldViews, std::span<const TransformComponent>{ transforms });
        AddComponentViews(views, worldViews, std::span<const VisibilityComponent>{ visibility });

        const std::uint32_t mask = archetype.componentMask;
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Camera)) {
            RepeatComponents(cameras, std::span<const CameraComponent>{ archetype.cameras }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const CameraComponent>{ cameras });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::MeshRenderer)) {
            RepeatComponents(meshRenderers, std::span<const MeshRendererComponent>{ archetype.meshRenderers }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const MeshRendererComponent>{ meshRenderers });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Light)) {
            RepeatComponents(lights, std::span<const LightComponent>{ archetype.lights }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const LightComponent>{ lights });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Input)) {
            RepeatComponents(inputs, std::span<const InputComponent>{ archetype.inputs }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const InputComponent>{ inputs });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Rigidbody)) {
            RepeatComponents(rigidbodies, std::span<const RigidbodyComponent>{ archetype.rigidbodies }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const RigidbodyComponent>{ rigidbodies });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Collider)) {
            RepeatComponents(colliders, std::span<const ColliderComponent>{ archetype.colliders }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const ColliderComponent>{ colliders });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::CharacterController)) {
            RepeatComponents(characterControllers, std::span<const CharacterControllerComponent>{ archetype.characterControllers }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const CharacterControllerComponent>{ characterControllers });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Joint)) {
            RepeatComponents(joints, std::span<const JointComponent>{ archetype.joints }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const JointComponent>{ joints });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Tags)) {
            RepeatComponents(tags, std::span<const TagsComponent>{ archetype.tags }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const TagsComponent>{ tags });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::RegionShape)) {
            RepeatComponents(regionShapes, std::span<const RegionShapeComponent>{ archetype.regionShapes }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const RegionShapeComponent>{ regionShapes });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::GuideCurve)) {
            RepeatComponents(guideCurves, std::span<const GuideCurveComponent>{ archetype.guideCurves }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const GuideCurveComponent>{ guideCurves });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::ContentInstance)) {
            RepeatComponents(contentInstances, std::span<const ContentInstanceComponent>{ archetype.contentInstances }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const ContentInstanceComponent>{ contentInstances });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::StreamFocus)) {
            RepeatComponents(streamFocuses, std::span<const StreamFocusComponent>{ archetype.streamFocuses }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const StreamFocusComponent>{ streamFocuses });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::WorldBackdrop)) {
            RepeatComponents(worldBackdrops, std::span<const WorldBackdropComponent>{ archetype.worldBackdrops }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const WorldBackdropComponent>{ worldBackdrops });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::AmbientRadiance)) {
            RepeatComponents(ambientRadiances, std::span<const AmbientRadianceComponent>{ archetype.ambientRadiances }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const AmbientRadianceComponent>{ ambientRadiances });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::DetailSwitch)) {
            RepeatComponents(detailSwitches, std::span<const SceneDetailSwitchComponent>{ archetype.detailSwitches }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const SceneDetailSwitchComponent>{ detailSwitches });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::VisibilityBlocker)) {
            RepeatComponents(visibilityBlockers, std::span<const SceneVisibilityBlockerComponent>{ archetype.visibilityBlockers }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const SceneVisibilityBlockerComponent>{ visibilityBlockers });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::VisibilityCell)) {
            RepeatComponents(visibilityCells, std::span<const VisibilityCellComponent>{ archetype.visibilityCells }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const VisibilityCellComponent>{ visibilityCells });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::RegionPortal)) {
            RepeatComponents(regionPortals, std::span<const SceneRegionPortalComponent>{ archetype.regionPortals }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const SceneRegionPortalComponent>{ regionPortals });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::AuxFrame)) {
            RepeatComponents(auxFrames, std::span<const AuxFrameComponent>{ archetype.auxFrames }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const AuxFrameComponent>{ auxFrames });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::GeometrySwarm)) {
            RepeatComponents(geometrySwarms, std::span<const GeometrySwarmComponent>{ archetype.geometrySwarms }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const GeometrySwarmComponent>{ geometrySwarms });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::SurfaceCast)) {
            RepeatComponents(surfaceCasts, std::span<const SurfaceCastComponent>{ archetype.surfaceCasts }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const SurfaceCastComponent>{ surfaceCasts });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::FacingPanel)) {
            RepeatComponents(facingPanels, std::span<const FacingPanelComponent>{ archetype.facingPanels }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const FacingPanelComponent>{ facingPanels });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::SpaceStroke)) {
            RepeatComponents(spaceStrokes, std::span<const SpaceStrokeComponent>{ archetype.spaceStrokes }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const SpaceStrokeComponent>{ spaceStrokes });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::HistoryRibbon)) {
            RepeatComponents(historyRibbons, std::span<const HistoryRibbonComponent>{ archetype.historyRibbons }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const HistoryRibbonComponent>{ historyRibbons });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Behaviour)) {
            RepeatComponents(behaviours, std::span<const BehaviourComponent>{ archetype.behaviours }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const BehaviourComponent>{ behaviours });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::AudioSource)) {
            RepeatComponents(audioSources, std::span<const AudioSourceComponent>{ archetype.audioSources }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const AudioSourceComponent>{ audioSources });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::AudioListener)) {
            RepeatComponents(audioListeners, std::span<const AudioListenerComponent>{ archetype.audioListeners }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const AudioListenerComponent>{ audioListeners });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Animator)) {
            RepeatComponents(animators, std::span<const Animator>{ archetype.animators }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const Animator>{ animators });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::UIDocument)) {
            RepeatComponents(uiDocuments, std::span<const UIDocumentComponent>{ archetype.uiDocuments }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const UIDocumentComponent>{ uiDocuments });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::NavAgent)) {
            RepeatComponents(navAgents, std::span<const NavAgent>{ archetype.navAgents }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const NavAgent>{ navAgents });
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::NavObstacle)) {
            RepeatComponents(navObstacles, std::span<const NavObstacle>{ archetype.navObstacles }, instanceCount);
            AddComponentViews(views, worldViews, std::span<const NavObstacle>{ navObstacles });
        }
    }

    void BuildPattern(const ScenePrefabBakedArchetype& archetype, std::size_t instanceCount) {
        views.clear();
        views.reserve(24U);
        worldViews.clear();
        worldViews.reserve(24U);
        AddCommandComponentPatternView(views, std::span<const TransformComponent>{ archetype.transforms }, instanceCount);
        AddWorldComponentPatternView(worldViews, std::span<const TransformComponent>{ archetype.transforms }, instanceCount);
        AddCommandComponentPatternView(views, std::span<const VisibilityComponent>{ archetype.visibility }, instanceCount);
        AddWorldComponentPatternView(worldViews, std::span<const VisibilityComponent>{ archetype.visibility }, instanceCount);

        const std::uint32_t mask = archetype.componentMask;
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Camera)) {
            AddCommandComponentPatternView(views, std::span<const CameraComponent>{ archetype.cameras }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const CameraComponent>{ archetype.cameras }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::MeshRenderer)) {
            AddCommandComponentPatternView(views, std::span<const MeshRendererComponent>{ archetype.meshRenderers }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const MeshRendererComponent>{ archetype.meshRenderers }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Light)) {
            AddCommandComponentPatternView(views, std::span<const LightComponent>{ archetype.lights }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const LightComponent>{ archetype.lights }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Input)) {
            AddCommandComponentPatternView(views, std::span<const InputComponent>{ archetype.inputs }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const InputComponent>{ archetype.inputs }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Rigidbody)) {
            AddCommandComponentPatternView(views, std::span<const RigidbodyComponent>{ archetype.rigidbodies }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const RigidbodyComponent>{ archetype.rigidbodies }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Collider)) {
            AddCommandComponentPatternView(views, std::span<const ColliderComponent>{ archetype.colliders }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const ColliderComponent>{ archetype.colliders }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::CharacterController)) {
            AddCommandComponentPatternView(views, std::span<const CharacterControllerComponent>{ archetype.characterControllers }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const CharacterControllerComponent>{ archetype.characterControllers }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Joint)) {
            AddCommandComponentPatternView(views, std::span<const JointComponent>{ archetype.joints }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const JointComponent>{ archetype.joints }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Tags)) {
            AddCommandComponentPatternView(views, std::span<const TagsComponent>{ archetype.tags }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const TagsComponent>{ archetype.tags }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::RegionShape)) {
            AddCommandComponentPatternView(views, std::span<const RegionShapeComponent>{ archetype.regionShapes }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const RegionShapeComponent>{ archetype.regionShapes }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::GuideCurve)) {
            AddCommandComponentPatternView(views, std::span<const GuideCurveComponent>{ archetype.guideCurves }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const GuideCurveComponent>{ archetype.guideCurves }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::ContentInstance)) {
            AddCommandComponentPatternView(views, std::span<const ContentInstanceComponent>{ archetype.contentInstances }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const ContentInstanceComponent>{ archetype.contentInstances }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::StreamFocus)) {
            AddCommandComponentPatternView(views, std::span<const StreamFocusComponent>{ archetype.streamFocuses }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const StreamFocusComponent>{ archetype.streamFocuses }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::WorldBackdrop)) {
            AddCommandComponentPatternView(views, std::span<const WorldBackdropComponent>{ archetype.worldBackdrops }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const WorldBackdropComponent>{ archetype.worldBackdrops }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::AmbientRadiance)) {
            AddCommandComponentPatternView(views, std::span<const AmbientRadianceComponent>{ archetype.ambientRadiances }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const AmbientRadianceComponent>{ archetype.ambientRadiances }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::DetailSwitch)) {
            AddCommandComponentPatternView(views, std::span<const SceneDetailSwitchComponent>{ archetype.detailSwitches }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const SceneDetailSwitchComponent>{ archetype.detailSwitches }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::VisibilityBlocker)) {
            AddCommandComponentPatternView(views, std::span<const SceneVisibilityBlockerComponent>{ archetype.visibilityBlockers }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const SceneVisibilityBlockerComponent>{ archetype.visibilityBlockers }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::VisibilityCell)) {
            AddCommandComponentPatternView(views, std::span<const VisibilityCellComponent>{ archetype.visibilityCells }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const VisibilityCellComponent>{ archetype.visibilityCells }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::RegionPortal)) {
            AddCommandComponentPatternView(views, std::span<const SceneRegionPortalComponent>{ archetype.regionPortals }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const SceneRegionPortalComponent>{ archetype.regionPortals }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::AuxFrame)) {
            AddCommandComponentPatternView(views, std::span<const AuxFrameComponent>{ archetype.auxFrames }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const AuxFrameComponent>{ archetype.auxFrames }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::GeometrySwarm)) {
            AddCommandComponentPatternView(views, std::span<const GeometrySwarmComponent>{ archetype.geometrySwarms }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const GeometrySwarmComponent>{ archetype.geometrySwarms }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::SurfaceCast)) {
            AddCommandComponentPatternView(views, std::span<const SurfaceCastComponent>{ archetype.surfaceCasts }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const SurfaceCastComponent>{ archetype.surfaceCasts }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::FacingPanel)) {
            AddCommandComponentPatternView(views, std::span<const FacingPanelComponent>{ archetype.facingPanels }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const FacingPanelComponent>{ archetype.facingPanels }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::SpaceStroke)) {
            AddCommandComponentPatternView(views, std::span<const SpaceStrokeComponent>{ archetype.spaceStrokes }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const SpaceStrokeComponent>{ archetype.spaceStrokes }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::HistoryRibbon)) {
            AddCommandComponentPatternView(views, std::span<const HistoryRibbonComponent>{ archetype.historyRibbons }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const HistoryRibbonComponent>{ archetype.historyRibbons }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Behaviour)) {
            AddCommandComponentPatternView(views, std::span<const BehaviourComponent>{ archetype.behaviours }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const BehaviourComponent>{ archetype.behaviours }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::AudioSource)) {
            AddCommandComponentPatternView(views, std::span<const AudioSourceComponent>{ archetype.audioSources }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const AudioSourceComponent>{ archetype.audioSources }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::AudioListener)) {
            AddCommandComponentPatternView(views, std::span<const AudioListenerComponent>{ archetype.audioListeners }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const AudioListenerComponent>{ archetype.audioListeners }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::Animator)) {
            AddCommandComponentPatternView(views, std::span<const Animator>{ archetype.animators }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const Animator>{ archetype.animators }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::UIDocument)) {
            AddCommandComponentPatternView(views, std::span<const UIDocumentComponent>{ archetype.uiDocuments }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const UIDocumentComponent>{ archetype.uiDocuments }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::NavAgent)) {
            AddCommandComponentPatternView(views, std::span<const NavAgent>{ archetype.navAgents }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const NavAgent>{ archetype.navAgents }, instanceCount);
        }
        if (ScenePrefabBakedMaskHas(mask, ScenePrefabBakedComponentMask::NavObstacle)) {
            AddCommandComponentPatternView(views, std::span<const NavObstacle>{ archetype.navObstacles }, instanceCount);
            AddWorldComponentPatternView(worldViews, std::span<const NavObstacle>{ archetype.navObstacles }, instanceCount);
        }
    }
};

struct ScenePrefabEntityCreateBreakdown {
    std::uint64_t componentPayloadBuildNanoseconds = 0;
    std::uint64_t entityBulkCreateNanoseconds = 0;
    std::uint64_t entityPrefabOrderMapNanoseconds = 0;
};

void AssignPrefabHierarchyOrderRange(SceneState& state, std::span<const SceneEntity> entities, std::uint32_t knownMaxDenseIndex) {
    const std::uint64_t firstOrder = state.nextHierarchyOrder;
    state.nextHierarchyOrder += entities.size();
    if (entities.empty()) {
        return;
    }
    if (knownMaxDenseIndex != kb::ecs::kInvalidGeneratedEntityIndex) {
        const std::size_t required = static_cast<std::size_t>(knownMaxDenseIndex) + 1U;
        if (state.denseHierarchyOrder.size() < required) {
            state.denseHierarchyOrder.resize(required);
        }
    }

    for (std::size_t index = 0; index < entities.size(); ++index) {
        const SceneEntity entity = entities[index];
        const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(entity);
        if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex) {
            if (denseIndex >= state.denseHierarchyOrder.size()) {
                state.denseHierarchyOrder.resize(static_cast<std::size_t>(denseIndex) + 1U);
            }
            state.denseHierarchyOrder[denseIndex] = firstOrder + index;
        } else {
            state.hierarchyOrder[entity.Id()] = firstOrder + index;
        }
    }
}

[[nodiscard]] bool AddPrefabHierarchyCreatedDenseFastPath(
    SceneState& state,
    std::span<const ScenePrefabNodeDesc> nodes,
    std::span<const SceneEntity> entities,
    const ScenePrefabInstantiationSettings& settings,
    std::size_t instanceCount,
    std::uint32_t maxEntityIndex) {
    if (maxEntityIndex == kb::ecs::kInvalidGeneratedEntityIndex || nodes.empty() || entities.empty()) {
        return false;
    }

    const std::size_t required = static_cast<std::size_t>(maxEntityIndex) + 1U;
    if (state.denseHierarchyParents.size() < required) {
        state.denseHierarchyParents.resize(required);
    }
    if (state.denseHierarchyChildren.size() < required) {
        state.denseHierarchyChildren.resize(required);
    }
    if (state.denseHierarchyOrder.size() < required) {
        state.denseHierarchyOrder.resize(required);
    }

    std::vector<std::size_t>& rootNodeIndices = state.prefabHierarchyChildrenPerNodeScratch;
    rootNodeIndices.clear();
    rootNodeIndices.reserve(nodes.size());
    std::vector<std::vector<std::size_t>> childNodesByParentNode(nodes.size());
    for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
        const ScenePrefabNodeDesc& node = nodes[nodeIndex];
        if (node.parentNode == ScenePrefabNodeDesc::NoParent) {
            rootNodeIndices.push_back(nodeIndex);
        } else if (node.parentNode < childNodesByParentNode.size()) {
            childNodesByParentNode[node.parentNode].push_back(nodeIndex);
        }
    }

    AssignPrefabHierarchyOrderRange(state, entities, maxEntityIndex);

    std::vector<SceneEntity>* rootAppendTarget = nullptr;
    std::size_t rootWriteCursor = 0U;
    const std::size_t rootAppendCount = rootNodeIndices.size() * instanceCount;
    if (!settings.parent.Entity().IsValid()) {
        const std::size_t oldSize = state.hierarchyRoots.size();
        state.hierarchyRoots.resize(oldSize + rootAppendCount);
        rootAppendTarget = &state.hierarchyRoots;
        rootWriteCursor = oldSize;
    } else {
        const SceneEntity rootParent = settings.parent.Entity();
        const std::uint32_t rootParentIndex = kb::ecs::GeneratedEntityIndex(rootParent);
        if (rootParentIndex != kb::ecs::kInvalidGeneratedEntityIndex) {
            if (state.denseHierarchyChildren.size() <= rootParentIndex) {
                state.denseHierarchyChildren.resize(static_cast<std::size_t>(rootParentIndex) + 1U);
            }
            std::vector<SceneEntity>& children = state.denseHierarchyChildren[rootParentIndex];
            const std::size_t oldSize = children.size();
            children.resize(oldSize + rootAppendCount);
            rootAppendTarget = &children;
            rootWriteCursor = oldSize;
        } else {
            std::vector<SceneEntity>& children = state.hierarchyChildren[rootParent.Id()];
            const std::size_t oldSize = children.size();
            children.resize(oldSize + rootAppendCount);
            rootAppendTarget = &children;
            rootWriteCursor = oldSize;
        }
    }

    for (std::size_t instanceIndex = 0; instanceIndex < instanceCount; ++instanceIndex) {
        for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
            const std::size_t entityOffset = EntityIndex(instanceIndex, nodeIndex, nodes.size());
            const SceneEntity entity = entities[entityOffset];
            const std::uint32_t entityIndex = kb::ecs::GeneratedEntityIndex(entity);
            if (entityIndex == kb::ecs::kInvalidGeneratedEntityIndex || entityIndex > maxEntityIndex) {
                return false;
            }
            state.denseHierarchyParents[entityIndex] = nodes[nodeIndex].parentNode == ScenePrefabNodeDesc::NoParent
                ? settings.parent.Entity()
                : entities[EntityIndex(instanceIndex, nodes[nodeIndex].parentNode, nodes.size())];
        }

        for (std::size_t rootNodeIndex : rootNodeIndices) {
            (*rootAppendTarget)[rootWriteCursor++] = entities[EntityIndex(instanceIndex, rootNodeIndex, nodes.size())];
        }

        for (std::size_t parentNodeIndex = 0; parentNodeIndex < childNodesByParentNode.size(); ++parentNodeIndex) {
            const std::vector<std::size_t>& childNodes = childNodesByParentNode[parentNodeIndex];
            if (childNodes.empty()) {
                continue;
            }
            const SceneEntity parentEntity = entities[EntityIndex(instanceIndex, parentNodeIndex, nodes.size())];
            const std::uint32_t parentDenseIndex = kb::ecs::GeneratedEntityIndex(parentEntity);
            if (parentDenseIndex == kb::ecs::kInvalidGeneratedEntityIndex || parentDenseIndex > maxEntityIndex) {
                return false;
            }
            std::vector<SceneEntity>& children = state.denseHierarchyChildren[parentDenseIndex];
            const std::size_t oldSize = children.size();
            children.resize(oldSize + childNodes.size());
            for (std::size_t childOffset = 0; childOffset < childNodes.size(); ++childOffset) {
                children[oldSize + childOffset] = entities[EntityIndex(instanceIndex, childNodes[childOffset], nodes.size())];
            }
        }
    }

    ++state.hierarchyTopologyVersion;
    return true;
}

[[nodiscard]] std::uint32_t AddPrefabHierarchyDense(
    SceneState& state,
    std::span<const ScenePrefabNodeDesc> nodes,
    std::span<const SceneEntity> entities,
    const ScenePrefabInstantiationSettings& settings,
    std::size_t instanceCount,
    std::uint32_t knownMaxDenseIndex = kb::ecs::kInvalidGeneratedEntityIndex) {
    if (nodes.empty() || entities.empty()) {
        return kb::ecs::kInvalidGeneratedEntityIndex;
    }

    if (AddPrefabHierarchyCreatedDenseFastPath(state, nodes, entities, settings, instanceCount, knownMaxDenseIndex)) {
        return knownMaxDenseIndex;
    }

    std::uint32_t maxEntityIndex = knownMaxDenseIndex;
    bool hasDenseEntity = knownMaxDenseIndex != kb::ecs::kInvalidGeneratedEntityIndex;
    if (!hasDenseEntity) {
        for (SceneEntity entity : entities) {
            const std::uint32_t index = kb::ecs::GeneratedEntityIndex(entity);
            if (index != kb::ecs::kInvalidGeneratedEntityIndex) {
                maxEntityIndex = hasDenseEntity ? std::max(maxEntityIndex, index) : index;
                hasDenseEntity = true;
            }
        }
    }
    if (hasDenseEntity) {
        const std::size_t required = static_cast<std::size_t>(maxEntityIndex) + 1U;
        if (state.denseHierarchyParents.size() < required) {
            state.denseHierarchyParents.resize(required);
        }
        if (state.denseHierarchyChildren.size() < required) {
            state.denseHierarchyChildren.resize(required);
        }
        if (state.denseHierarchyOrder.size() < required) {
            state.denseHierarchyOrder.resize(required);
        }
    }

    std::vector<std::size_t>& childrenPerNode = state.prefabHierarchyChildrenPerNodeScratch;
    childrenPerNode.assign(nodes.size(), 0U);
    std::size_t rootCount = 0;
    for (const ScenePrefabNodeDesc& node : nodes) {
        if (node.parentNode == ScenePrefabNodeDesc::NoParent) {
            ++rootCount;
        } else if (node.parentNode < childrenPerNode.size()) {
            ++childrenPerNode[node.parentNode];
        }
    }
    state.hierarchyRoots.reserve(state.hierarchyRoots.size() + (settings.parent.Entity().IsValid() ? 0U : rootCount * instanceCount));
    if (settings.parent.Entity().IsValid()) {
        const std::uint32_t parentIndex = kb::ecs::GeneratedEntityIndex(settings.parent.Entity());
        if (parentIndex != kb::ecs::kInvalidGeneratedEntityIndex) {
            if (state.denseHierarchyChildren.size() <= parentIndex) {
                state.denseHierarchyChildren.resize(static_cast<std::size_t>(parentIndex) + 1U);
            }
            state.denseHierarchyChildren[parentIndex].reserve(state.denseHierarchyChildren[parentIndex].size() + rootCount * instanceCount);
        } else {
            state.hierarchyChildren[settings.parent.Entity().Id()].reserve(state.hierarchyChildren[settings.parent.Entity().Id()].size() + rootCount * instanceCount);
        }
    }

    AssignPrefabHierarchyOrderRange(state, entities, maxEntityIndex);
    for (std::size_t instanceIndex = 0; instanceIndex < instanceCount; ++instanceIndex) {
        for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
            const SceneEntity entity = entities[EntityIndex(instanceIndex, nodeIndex, nodes.size())];
            if (childrenPerNode[nodeIndex] != 0U) {
                const std::uint32_t parentIndex = kb::ecs::GeneratedEntityIndex(entity);
                if (parentIndex != kb::ecs::kInvalidGeneratedEntityIndex) {
                    state.denseHierarchyChildren[parentIndex].reserve(childrenPerNode[nodeIndex]);
                }
            }
            const SceneEntity parent = nodes[nodeIndex].parentNode == ScenePrefabNodeDesc::NoParent
                ? settings.parent.Entity()
                : entities[EntityIndex(instanceIndex, nodes[nodeIndex].parentNode, nodes.size())];
            const std::uint32_t entityIndex = kb::ecs::GeneratedEntityIndex(entity);
            if (entityIndex != kb::ecs::kInvalidGeneratedEntityIndex) {
                state.denseHierarchyParents[entityIndex] = parent;
            } else {
                state.hierarchyParents.emplace(entity.Id(), parent);
            }

            if (!parent.IsValid()) {
                state.hierarchyRoots.push_back(entity);
                continue;
            }
            const std::uint32_t parentIndex = kb::ecs::GeneratedEntityIndex(parent);
            if (parentIndex != kb::ecs::kInvalidGeneratedEntityIndex) {
                state.denseHierarchyChildren[parentIndex].push_back(entity);
            } else {
                state.hierarchyChildren[parent.Id()].push_back(entity);
            }
        }
    }
    ++state.hierarchyTopologyVersion;
    return hasDenseEntity ? maxEntityIndex : kb::ecs::kInvalidGeneratedEntityIndex;
}

void QueueHierarchy(
    kb::ecs::CommandBuffer::WorkerBuffer& worker,
    std::span<const kb::ecs::CommandEntity> entities,
    std::span<const ScenePrefabNodeDesc> nodes,
    std::size_t instanceCount,
    SceneObject rootParent) {
    std::vector<kb::ecs::CommandEntity> children;
    std::vector<kb::ecs::CommandEntity> parents;
    children.reserve(TotalNodeCount(instanceCount, nodes.size()));
    parents.reserve(TotalNodeCount(instanceCount, nodes.size()));

    for (std::size_t instanceIndex = 0; instanceIndex < instanceCount; ++instanceIndex) {
        for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
            const ScenePrefabNodeDesc& node = nodes[nodeIndex];
            const std::size_t childIndex = EntityIndex(instanceIndex, nodeIndex, nodes.size());
            if (node.parentNode == ScenePrefabNodeDesc::NoParent) {
                if (rootParent.EntityHandle().IsValid()) {
                    children.push_back(entities[childIndex]);
                    parents.push_back(kb::ecs::CommandEntity::Existing(rootParent.Entity()));
                }
                continue;
            }
            children.push_back(entities[childIndex]);
            parents.push_back(entities[EntityIndex(instanceIndex, node.parentNode, nodes.size())]);
        }
    }

    worker.SetParentsForNewEntitiesKnownAcyclic(std::move(children), std::move(parents));
}

[[nodiscard]] std::vector<kb::ecs::CommandEntity> CreateBakedEntities(
    kb::ecs::CommandBuffer& commandBuffer,
    const ScenePrefabBakedData& baked,
    std::size_t instanceCount,
    std::vector<ScenePrefabArchetypeSpawnPayload>& payloads,
    ScenePrefabEntityCreateBreakdown& breakdown) {
    std::vector<kb::ecs::CommandEntity> entities(TotalNodeCount(instanceCount, baked.NodeCount()));
    payloads.clear();
    payloads.reserve(baked.Archetypes().size());
    const bool contiguousPrefabOrder = CreatesContiguousPrefabOrderEntityRuns(baked);

    std::size_t archetypeIndex = 0U;
    for (const ScenePrefabBakedArchetype& archetype : baked.Archetypes()) {
        ScenePrefabArchetypeSpawnPayload& payload = payloads.emplace_back();
        kb::ecs::CommandBuffer::WorkerBuffer worker = commandBuffer.Worker(archetypeIndex);
        const std::size_t archetypeNodeCount = archetype.nodeIndices.size();
        const std::size_t archetypeEntityCount = TotalNodeCount(instanceCount, archetypeNodeCount);
        const auto payloadStart = PrefabStatsClock::now();
        payload.BuildPattern(archetype, instanceCount);
        breakdown.componentPayloadBuildNanoseconds += ElapsedNanoseconds(payloadStart, PrefabStatsClock::now());

        const auto createStart = PrefabStatsClock::now();
        std::vector<kb::ecs::CommandEntity> created = worker.CreateEntitiesBorrowed(
            archetypeEntityCount,
            std::span<const kb::ecs::CommandBuffer::BulkComponentView>{ payload.views });
        breakdown.entityBulkCreateNanoseconds += ElapsedNanoseconds(createStart, PrefabStatsClock::now());

        const auto mapStart = PrefabStatsClock::now();
        if (contiguousPrefabOrder) {
            entities = std::move(created);
        } else {
            for (std::size_t instanceIndex = 0; instanceIndex < instanceCount; ++instanceIndex) {
                for (std::size_t archetypeNodeIndex = 0; archetypeNodeIndex < archetypeNodeCount; ++archetypeNodeIndex) {
                    const std::size_t createdIndex = EntityIndex(instanceIndex, archetypeNodeIndex, archetypeNodeCount);
                    const std::size_t prefabIndex = EntityIndex(instanceIndex, archetype.nodeIndices[archetypeNodeIndex], baked.NodeCount());
                    entities[prefabIndex] = created[createdIndex];
                }
            }
        }
        breakdown.entityPrefabOrderMapNanoseconds += ElapsedNanoseconds(mapStart, PrefabStatsClock::now());
        ++archetypeIndex;
    }

    return entities;
}

[[nodiscard]] std::size_t ComponentBytesCopied(std::span<const ScenePrefabArchetypeSpawnPayload> payloads) noexcept {
    std::size_t bytes = 0;
    for (const ScenePrefabArchetypeSpawnPayload& payload : payloads) {
        for (const kb::ecs::World::BulkComponentView& component : payload.worldViews) {
            bytes += component.componentSize * component.componentCount;
        }
    }
    return bytes;
}

[[nodiscard]] std::size_t ComponentSourceBytesRead(std::span<const ScenePrefabArchetypeSpawnPayload> payloads) noexcept {
    std::size_t bytes = 0;
    for (const ScenePrefabArchetypeSpawnPayload& payload : payloads) {
        for (const kb::ecs::World::BulkComponentView& component : payload.worldViews) {
            const std::size_t sourceCount = component.sourceCount == 0U ? component.componentCount : component.sourceCount;
            bytes += component.componentSize * sourceCount;
        }
    }
    return bytes;
}

[[nodiscard]] std::vector<SceneEntity> CreateBakedEntitiesDirect(
    kb::ecs::World& world,
    const ScenePrefabBakedData& baked,
    std::size_t instanceCount,
    std::vector<ScenePrefabArchetypeSpawnPayload>& payloads,
    bool nativeOnly,
    ScenePrefabEntityCreateBreakdown& breakdown) {
    payloads.clear();
    payloads.reserve(baked.Archetypes().size());
    const std::span<const ScenePrefabBakedArchetype> archetypes = baked.Archetypes();
    std::vector<SceneEntity> entities(TotalNodeCount(instanceCount, baked.NodeCount()));
    const bool contiguousPrefabOrder = CreatesContiguousPrefabOrderEntityRuns(baked);

    for (const ScenePrefabBakedArchetype& archetype : archetypes) {
        ScenePrefabArchetypeSpawnPayload& payload = payloads.emplace_back();
        const std::size_t archetypeNodeCount = archetype.nodeIndices.size();
        const std::size_t archetypeEntityCount = TotalNodeCount(instanceCount, archetypeNodeCount);
        const auto payloadStart = PrefabStatsClock::now();
        payload.BuildPattern(archetype, instanceCount);
        breakdown.componentPayloadBuildNanoseconds += ElapsedNanoseconds(payloadStart, PrefabStatsClock::now());

        const auto createStart = PrefabStatsClock::now();
        if (nativeOnly) {
            world.CreateEntitiesNativeOnlyInto(
                payload.createdEntities,
                archetypeEntityCount,
                std::span<const kb::ecs::World::BulkComponentView>{ payload.worldViews });
        } else {
            world.CreateEntitiesInto(
                payload.createdEntities,
                archetypeEntityCount,
                std::span<const kb::ecs::World::BulkComponentView>{ payload.worldViews });
        }
        breakdown.entityBulkCreateNanoseconds += ElapsedNanoseconds(createStart, PrefabStatsClock::now());

        if (payload.createdEntities.size() != archetypeEntityCount) {
            throw std::runtime_error("Scene prefab direct bulk spawn created an unexpected entity count");
        }

        const auto mapStart = PrefabStatsClock::now();
        if (contiguousPrefabOrder) {
            entities = std::move(payload.createdEntities);
        } else {
            const std::vector<kb::ecs::Entity>& created = payload.createdEntities;
            for (std::size_t instanceIndex = 0; instanceIndex < instanceCount; ++instanceIndex) {
                for (std::size_t archetypeNodeIndex = 0; archetypeNodeIndex < archetypeNodeCount; ++archetypeNodeIndex) {
                    const std::size_t createdIndex = EntityIndex(instanceIndex, archetypeNodeIndex, archetypeNodeCount);
                    const std::size_t prefabIndex = EntityIndex(instanceIndex, archetype.nodeIndices[archetypeNodeIndex], baked.NodeCount());
                    entities[prefabIndex] = created[createdIndex];
                }
            }
        }
        breakdown.entityPrefabOrderMapNanoseconds += ElapsedNanoseconds(mapStart, PrefabStatsClock::now());
    }

    return entities;
}

void ResolvePrefabJointReferences(
    Scene& scene,
    std::span<const ScenePrefabNodeDesc> nodes,
    std::span<const SceneEntity> entities,
    std::size_t instanceCount) {
    if (nodes.empty()) {
        return;
    }

    std::vector<std::size_t> jointNodeIndices;
    jointNodeIndices.reserve(nodes.size());
    for (std::size_t nodeIndex = 0U; nodeIndex < nodes.size(); ++nodeIndex) {
        if (nodes[nodeIndex].components.joint.has_value()) {
            jointNodeIndices.push_back(nodeIndex);
        }
    }
    if (jointNodeIndices.empty()) {
        return;
    }

    std::unordered_map<std::uint64_t, std::size_t> nodeIndexByStableId;
    nodeIndexByStableId.reserve(nodes.size());
    for (std::size_t nodeIndex = 0U; nodeIndex < nodes.size(); ++nodeIndex) {
        nodeIndexByStableId.emplace(nodes[nodeIndex].stableId, nodeIndex);
    }

    std::vector<std::size_t> connectedNodeIndices;
    connectedNodeIndices.reserve(jointNodeIndices.size());
    for (const std::size_t jointNodeIndex : jointNodeIndices) {
        const ScenePrefabJointComponent& prefabJoint = *nodes[jointNodeIndex].components.joint;
        if (prefabJoint.connectedNodeStableId == ScenePrefabJointComponent::InvalidConnectedNodeStableId) {
            connectedNodeIndices.push_back(nodes.size());
            continue;
        }
        const auto connectedNode = nodeIndexByStableId.find(prefabJoint.connectedNodeStableId);
        if (connectedNode == nodeIndexByStableId.end()) {
            throw std::invalid_argument("Scene prefab joint references a missing stable node id");
        }
        connectedNodeIndices.push_back(connectedNode->second);
    }

    for (std::size_t instanceIndex = 0U; instanceIndex < instanceCount; ++instanceIndex) {
        for (std::size_t jointIndex = 0U; jointIndex < jointNodeIndices.size(); ++jointIndex) {
            const std::size_t connectedNodeIndex = connectedNodeIndices[jointIndex];
            if (connectedNodeIndex == nodes.size()) {
                continue;
            }

            const SceneEntity owner = entities[EntityIndex(instanceIndex, jointNodeIndices[jointIndex], nodes.size())];
            JointComponent* joint = scene.Components().Joints().TryGet(owner);
            if (joint == nullptr) {
                throw std::runtime_error("Scene prefab joint component was not created");
            }
            joint->connectedEntity = entities[EntityIndex(instanceIndex, connectedNodeIndex, nodes.size())];
        }
    }
}

void ResolvePrefabRegionPortalReferences(
    Scene& scene,
    std::span<const ScenePrefabNodeDesc> nodes,
    std::span<const SceneEntity> entities,
    std::size_t instanceCount) {
    std::unordered_map<std::uint64_t, std::size_t> nodeIndexByStableId;
    nodeIndexByStableId.reserve(nodes.size());
    for (std::size_t index = 0U; index < nodes.size(); ++index) nodeIndexByStableId.emplace(nodes[index].stableId, index);
    for (std::size_t instanceIndex = 0U; instanceIndex < instanceCount; ++instanceIndex) {
        for (std::size_t nodeIndex = 0U; nodeIndex < nodes.size(); ++nodeIndex) {
            const std::optional<ScenePrefabRegionPortalComponent>& prefabPortal = nodes[nodeIndex].components.regionPortal;
            if (!prefabPortal.has_value()) continue;
            if (!prefabPortal->enabled &&
                prefabPortal->sourceCellNodeStableId == ScenePrefabRegionPortalComponent::InvalidCellNodeStableId &&
                prefabPortal->targetCellNodeStableId == ScenePrefabRegionPortalComponent::InvalidCellNodeStableId) {
                continue;
            }
            const auto source = nodeIndexByStableId.find(prefabPortal->sourceCellNodeStableId);
            const auto target = nodeIndexByStableId.find(prefabPortal->targetCellNodeStableId);
            if (source == nodeIndexByStableId.end() || target == nodeIndexByStableId.end()) throw std::invalid_argument("Scene prefab region portal references a missing stable node id");
            const SceneEntity owner = entities[EntityIndex(instanceIndex, nodeIndex, nodes.size())];
            scene.Components().RegionPortals().Set(owner, SceneRegionPortalComponent{
                .sourceCell = entities[EntityIndex(instanceIndex, source->second, nodes.size())],
                .targetCell = entities[EntityIndex(instanceIndex, target->second, nodes.size())],
                .purposes = prefabPortal->purposes,
                .enabled = prefabPortal->enabled,
            });
        }
    }
}

void ResolvePrefabLensEchoReferences(
    Scene& scene,
    std::span<const ScenePrefabNodeDesc> nodes,
    std::span<const SceneEntity> entities,
    std::size_t instanceCount) {
    std::unordered_map<std::uint64_t, std::size_t> nodeIndexByStableId;
    nodeIndexByStableId.reserve(nodes.size());
    for (std::size_t index = 0U; index < nodes.size(); ++index) nodeIndexByStableId.emplace(nodes[index].stableId, index);
    for (std::size_t instanceIndex = 0U; instanceIndex < instanceCount; ++instanceIndex) {
        for (std::size_t nodeIndex = 0U; nodeIndex < nodes.size(); ++nodeIndex) {
            const std::optional<ScenePrefabLensEchoComponent>& prefabEcho = nodes[nodeIndex].components.lensEcho;
            if (!prefabEcho.has_value()) continue;
            const SceneEntity owner = entities[EntityIndex(instanceIndex, nodeIndex, nodes.size())];
            if (!prefabEcho->enabled && prefabEcho->sourceNodeStableId == ScenePrefabLensEchoComponent::InvalidSourceNodeStableId) {
                scene.Components().LensEchoes().Set(owner, LensEchoComponent{
                    .profileMaterialAssetId = prefabEcho->profileMaterialAssetId, .intensity = prefabEcho->intensity,
                    .size = prefabEcho->size, .layer = prefabEcho->layer, .occlusionRule = prefabEcho->occlusionRule, .enabled = false,
                });
                continue;
            }
            const auto source = nodeIndexByStableId.find(prefabEcho->sourceNodeStableId);
            if (source == nodeIndexByStableId.end()) throw std::invalid_argument("Scene prefab lens echo references a missing stable node id");
            scene.Components().LensEchoes().Set(owner, LensEchoComponent{
                .sourceEntityId = entities[EntityIndex(instanceIndex, source->second, nodes.size())].Id(),
                .profileMaterialAssetId = prefabEcho->profileMaterialAssetId, .intensity = prefabEcho->intensity,
                .size = prefabEcho->size, .layer = prefabEcho->layer, .occlusionRule = prefabEcho->occlusionRule, .enabled = prefabEcho->enabled,
            });
        }
    }
}

[[nodiscard]] std::vector<ScenePrefabInstance> BuildInstances(
    Scene& scene,
    std::span<const ScenePrefabNodeDesc> nodes,
    std::span<const SceneEntity> resolvedEntities,
    const ScenePrefabInstantiationSettings& settings,
    std::size_t instanceCount,
    bool collectInstances,
    std::uint64_t& instanceObjectSlabNanoseconds,
    std::uint64_t& hierarchyRecordNanoseconds,
    std::uint64_t& nameAssignmentNanoseconds,
    std::uint32_t& maxGeneratedEntityIndex) {
    SceneState& state = SceneAccess::State(scene);
    if (!collectInstances) {
        const auto hierarchyStart = PrefabStatsClock::now();
        maxGeneratedEntityIndex = AddPrefabHierarchyDense(state, nodes, resolvedEntities, settings, instanceCount);
        hierarchyRecordNanoseconds = ElapsedNanoseconds(hierarchyStart, PrefabStatsClock::now());
        if (settings.assignNames) {
            const std::vector<std::string> nodeNames = BuildNodeNames(nodes, settings);
            const auto nameStart = PrefabStatsClock::now();
            SceneEntityNaming::SetRepeatedNamesForCreatedDenseEntities(state, resolvedEntities, std::span<const std::string>{ nodeNames }, maxGeneratedEntityIndex);
            nameAssignmentNanoseconds = ElapsedNanoseconds(nameStart, PrefabStatsClock::now());
        }
        return {};
    }

    std::vector<ScenePrefabInstance> instances;
    instances.reserve(instanceCount);
    const auto objectSlabStart = PrefabStatsClock::now();
    auto objectSlab = std::make_shared<std::vector<SceneObject>>();
    objectSlab->reserve(TotalNodeCount(instanceCount, nodes.size()));
    std::uint32_t objectSlabMaxGeneratedEntityIndex = kb::ecs::kInvalidGeneratedEntityIndex;

    for (std::size_t instanceIndex = 0; instanceIndex < instanceCount; ++instanceIndex) {
        const std::size_t objectOffset = objectSlab->size();
        for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
            const std::size_t entityIndex = EntityIndex(instanceIndex, nodeIndex, nodes.size());
            const kb::ecs::Entity entity = resolvedEntities[entityIndex];
            const std::uint32_t denseIndex = kb::ecs::GeneratedEntityIndex(entity);
            if (denseIndex != kb::ecs::kInvalidGeneratedEntityIndex) {
                objectSlabMaxGeneratedEntityIndex = objectSlabMaxGeneratedEntityIndex == kb::ecs::kInvalidGeneratedEntityIndex
                    ? denseIndex
                    : std::max(objectSlabMaxGeneratedEntityIndex, denseIndex);
            }
            objectSlab->push_back(SceneAccess::MakeObject(scene, entity));
        }

        instances.emplace_back(objectSlab, objectOffset, nodes.size());
    }
    instanceObjectSlabNanoseconds = ElapsedNanoseconds(objectSlabStart, PrefabStatsClock::now());

    const auto hierarchyStart = PrefabStatsClock::now();
    maxGeneratedEntityIndex = AddPrefabHierarchyDense(state, nodes, resolvedEntities, settings, instanceCount, objectSlabMaxGeneratedEntityIndex);
    hierarchyRecordNanoseconds = ElapsedNanoseconds(hierarchyStart, PrefabStatsClock::now());
    if (settings.assignNames) {
        const std::vector<std::string> nodeNames = BuildNodeNames(nodes, settings);
        const auto nameStart = PrefabStatsClock::now();
        SceneEntityNaming::SetRepeatedNamesForCreatedDenseEntities(state, resolvedEntities, std::span<const std::string>{ nodeNames }, maxGeneratedEntityIndex);
        nameAssignmentNanoseconds = ElapsedNanoseconds(nameStart, PrefabStatsClock::now());
    }
    return instances;
}

[[nodiscard]] std::vector<ScenePrefabInstance> BuildInstances(
    Scene& scene,
    std::span<const ScenePrefabNodeDesc> nodes,
    std::span<const kb::ecs::CommandEntity> commandEntities,
    const kb::ecs::CommandBufferPlaybackResult& playback,
    const ScenePrefabInstantiationSettings& settings,
    std::size_t instanceCount,
    bool collectInstances,
    std::uint64_t& instanceObjectSlabNanoseconds,
    std::uint64_t& hierarchyRecordNanoseconds,
    std::uint64_t& nameAssignmentNanoseconds,
    std::uint32_t& maxGeneratedEntityIndex) {
    std::vector<SceneEntity> resolvedEntities(commandEntities.size());
    for (std::size_t index = 0; index < commandEntities.size(); ++index) {
        resolvedEntities[index] = playback.Resolve(commandEntities[index]);
    }
    return BuildInstances(
        scene,
        nodes,
        std::span<const SceneEntity>{ resolvedEntities },
        settings,
        instanceCount,
        collectInstances,
        instanceObjectSlabNanoseconds,
        hierarchyRecordNanoseconds,
        nameAssignmentNanoseconds,
        maxGeneratedEntityIndex);
}

[[nodiscard]] std::vector<ScenePrefabInstance> InstantiateInternal(
    Scene& scene,
    const ScenePrefab& prefab,
    std::size_t count,
    const ScenePrefabInstantiationSettings& settings,
    bool collectInstances,
    const ScenePrefabBakedData* bakedOverride = nullptr) {
    SceneState& state = SceneAccess::State(scene);
    state.lastPrefabInstantiationStats = ScenePrefabInstantiationStats{
        .requestedInstances = count,
    };
    if (count == 0 || !ScenePrefabValidator::IsValid(prefab)) {
        return {};
    }

    const std::span<const ScenePrefabNodeDesc> nodes = prefab.Nodes();
    ScenePrefabBakedData bakedStorage;
    const ScenePrefabBakedData* baked = bakedOverride;
    std::uint64_t prefabBakeNanoseconds = 0;
    if (baked == nullptr) {
        const auto bakeStart = PrefabStatsClock::now();
        bakedStorage = ScenePrefabBakedData::Bake(nodes);
        prefabBakeNanoseconds = ElapsedNanoseconds(bakeStart, PrefabStatsClock::now());
        baked = &bakedStorage;
    }
    if (baked->NodeCount() != nodes.size()) {
        throw std::invalid_argument("Scene prefab baked data does not match prefab node count");
    }
    const std::size_t totalCount = TotalNodeCount(count, baked->NodeCount());
    if (totalCount == 0) {
        return {};
    }

    const kb::ecs::NativeEcsStorageStats beforeStorage = state.world.NativeStorageStats();
    if (!settings.syncWorldHierarchy) {
        std::vector<ScenePrefabArchetypeSpawnPayload> spawnPayloads;
        ScenePrefabEntityCreateBreakdown createBreakdown;
        constexpr bool nativeOnlyBatch = true;
        const auto createStart = PrefabStatsClock::now();
        const std::vector<SceneEntity> entities = CreateBakedEntitiesDirect(state.world, *baked, count, spawnPayloads, nativeOnlyBatch, createBreakdown);
        const std::uint64_t entityCreateNanoseconds = ElapsedNanoseconds(createStart, PrefabStatsClock::now());
        ResolvePrefabJointReferences(scene, nodes, std::span<const SceneEntity>{ entities }, count);
        ResolvePrefabRegionPortalReferences(scene, nodes, std::span<const SceneEntity>{ entities }, count);
        ResolvePrefabLensEchoReferences(scene, nodes, std::span<const SceneEntity>{ entities }, count);
        const kb::ecs::NativeEcsStorageStats afterStorage = state.world.NativeStorageStats();
        std::uint64_t instanceObjectSlabNanoseconds = 0;
        std::uint64_t hierarchyRecordNanoseconds = 0;
        std::uint64_t nameAssignmentNanoseconds = 0;
        std::uint32_t maxGeneratedEntityIndex = kb::ecs::kInvalidGeneratedEntityIndex;
        const std::vector<ScenePrefabInstance> instances =
            BuildInstances(scene, nodes, std::span<const SceneEntity>{ entities }, settings, count, collectInstances, instanceObjectSlabNanoseconds, hierarchyRecordNanoseconds, nameAssignmentNanoseconds, maxGeneratedEntityIndex);
        const std::size_t componentBytesCopied = ComponentBytesCopied(std::span<const ScenePrefabArchetypeSpawnPayload>{ spawnPayloads });
        const std::size_t componentSourceBytesRead = ComponentSourceBytesRead(std::span<const ScenePrefabArchetypeSpawnPayload>{ spawnPayloads });

        state.lastPrefabInstantiationStats = ScenePrefabInstantiationStats{
            .requestedInstances = count,
            .instantiatedInstances = collectInstances ? instances.size() : count,
            .nodesPerInstance = baked->NodeCount(),
            .entitiesCreated = entities.size(),
            .prefabArchetypesTouched = baked->Archetypes().size(),
            .bulkCreateCommands = baked->Archetypes().size(),
            .componentSetCommands = 0,
            .parentCommands = 0,
            .componentBytesCopied = componentBytesCopied,
            .componentSourceBytesRead = componentSourceBytesRead,
            .componentCopyBytesPerSecond = UnitsPerSecond(componentBytesCopied, entityCreateNanoseconds),
            .componentSourceBytesPerSecond = UnitsPerSecond(componentSourceBytesRead, entityCreateNanoseconds),
            .entityCreateEntitiesPerSecond = UnitsPerSecond(entities.size(), entityCreateNanoseconds),
            .chunksAllocatedDelta = afterStorage.chunkPoolAllocated >= beforeStorage.chunkPoolAllocated ? afterStorage.chunkPoolAllocated - beforeStorage.chunkPoolAllocated : 0U,
            .chunksReusedDelta = afterStorage.chunkPoolReuseCount >= beforeStorage.chunkPoolReuseCount ? afterStorage.chunkPoolReuseCount - beforeStorage.chunkPoolReuseCount : 0U,
            .entityCreateNanoseconds = entityCreateNanoseconds,
            .prefabBakeNanoseconds = prefabBakeNanoseconds,
            .componentPayloadBuildNanoseconds = createBreakdown.componentPayloadBuildNanoseconds,
            .entityBulkCreateNanoseconds = createBreakdown.entityBulkCreateNanoseconds,
            .entityPrefabOrderMapNanoseconds = createBreakdown.entityPrefabOrderMapNanoseconds,
            .instanceObjectSlabNanoseconds = instanceObjectSlabNanoseconds,
            .hierarchyRecordNanoseconds = hierarchyRecordNanoseconds,
            .nameAssignmentNanoseconds = nameAssignmentNanoseconds,
            .hasGeneratedEntityIndexRange = maxGeneratedEntityIndex != kb::ecs::kInvalidGeneratedEntityIndex,
            .hasContiguousGeneratedEntityRuns = CreatesContiguousPrefabOrderEntityRuns(*baked),
            .maxGeneratedEntityIndex = maxGeneratedEntityIndex == kb::ecs::kInvalidGeneratedEntityIndex ? 0U : maxGeneratedEntityIndex,
        };
        return instances;
    }

    const std::size_t hierarchyLane = baked->Archetypes().size();
    const std::size_t commandLaneCount = hierarchyLane + 1U;
    kb::ecs::CommandBuffer commandBuffer{ commandLaneCount };
    std::vector<ScenePrefabArchetypeSpawnPayload> spawnPayloads;
    ScenePrefabEntityCreateBreakdown createBreakdown;
    const auto createStart = PrefabStatsClock::now();
    const auto commandBuildStart = PrefabStatsClock::now();
    std::vector<kb::ecs::CommandEntity> entities = CreateBakedEntities(commandBuffer, *baked, count, spawnPayloads, createBreakdown);
    kb::ecs::CommandBuffer::WorkerBuffer hierarchyWorker = commandBuffer.Worker(hierarchyLane);
    QueueHierarchy(hierarchyWorker, std::span<const kb::ecs::CommandEntity>{ entities.data(), entities.size() }, nodes, count, settings.parent);
    const std::uint64_t commandBuildNanoseconds = ElapsedNanoseconds(commandBuildStart, PrefabStatsClock::now());

    const auto playbackStart = PrefabStatsClock::now();
    kb::ecs::CommandBufferPlaybackResult playback = commandBuffer.Playback(state.world);
    const std::uint64_t commandPlaybackNanoseconds = ElapsedNanoseconds(playbackStart, PrefabStatsClock::now());
    const std::uint64_t entityCreateNanoseconds = ElapsedNanoseconds(createStart, PrefabStatsClock::now());
    const kb::ecs::NativeEcsStorageStats afterStorage = state.world.NativeStorageStats();
    std::vector<SceneEntity> resolvedEntities(entities.size());
    for (std::size_t index = 0U; index < entities.size(); ++index) {
        resolvedEntities[index] = playback.Resolve(entities[index]);
    }
    ResolvePrefabJointReferences(scene, nodes, std::span<const SceneEntity>{ resolvedEntities }, count);
    ResolvePrefabRegionPortalReferences(scene, nodes, std::span<const SceneEntity>{ resolvedEntities }, count);
    ResolvePrefabLensEchoReferences(scene, nodes, std::span<const SceneEntity>{ resolvedEntities }, count);
    std::uint64_t instanceObjectSlabNanoseconds = 0;
    std::uint64_t hierarchyRecordNanoseconds = 0;
    std::uint64_t nameAssignmentNanoseconds = 0;
    std::uint32_t maxGeneratedEntityIndex = kb::ecs::kInvalidGeneratedEntityIndex;
    const std::vector<ScenePrefabInstance> instances =
        BuildInstances(scene, nodes, std::span<const SceneEntity>{ resolvedEntities }, settings, count, collectInstances, instanceObjectSlabNanoseconds, hierarchyRecordNanoseconds, nameAssignmentNanoseconds, maxGeneratedEntityIndex);

    const kb::ecs::CommandBufferPlaybackResult::Stats& playbackStats = playback.PlaybackStats();
    const std::size_t componentSourceBytesRead = ComponentSourceBytesRead(std::span<const ScenePrefabArchetypeSpawnPayload>{ spawnPayloads });
    state.lastPrefabInstantiationStats = ScenePrefabInstantiationStats{
        .requestedInstances = count,
        .instantiatedInstances = collectInstances ? instances.size() : count,
        .nodesPerInstance = baked->NodeCount(),
        .entitiesCreated = playback.CreatedCount(),
        .prefabArchetypesTouched = baked->Archetypes().size(),
        .bulkCreateCommands = playbackStats.bulkCreateCommands,
        .componentSetCommands = playbackStats.componentSetCommands,
        .parentCommands = playbackStats.parentCommands,
        .componentBytesCopied = playbackStats.componentBytesCopied,
        .componentSourceBytesRead = componentSourceBytesRead,
        .componentCopyBytesPerSecond = UnitsPerSecond(playbackStats.componentBytesCopied, entityCreateNanoseconds),
        .componentSourceBytesPerSecond = UnitsPerSecond(componentSourceBytesRead, entityCreateNanoseconds),
        .entityCreateEntitiesPerSecond = UnitsPerSecond(playback.CreatedCount(), entityCreateNanoseconds),
        .chunksAllocatedDelta = afterStorage.chunkPoolAllocated >= beforeStorage.chunkPoolAllocated ? afterStorage.chunkPoolAllocated - beforeStorage.chunkPoolAllocated : 0U,
        .chunksReusedDelta = afterStorage.chunkPoolReuseCount >= beforeStorage.chunkPoolReuseCount ? afterStorage.chunkPoolReuseCount - beforeStorage.chunkPoolReuseCount : 0U,
        .entityCreateNanoseconds = entityCreateNanoseconds,
        .prefabBakeNanoseconds = prefabBakeNanoseconds,
        .componentPayloadBuildNanoseconds = createBreakdown.componentPayloadBuildNanoseconds,
        .entityBulkCreateNanoseconds = createBreakdown.entityBulkCreateNanoseconds,
        .entityPrefabOrderMapNanoseconds = createBreakdown.entityPrefabOrderMapNanoseconds,
        .instanceObjectSlabNanoseconds = instanceObjectSlabNanoseconds,
        .commandBuildNanoseconds = commandBuildNanoseconds,
        .commandPlaybackNanoseconds = commandPlaybackNanoseconds,
        .commandPlaybackCreateNanoseconds = playbackStats.createPhaseNanoseconds,
        .commandPlaybackApplyNanoseconds = playbackStats.applyPhaseNanoseconds,
        .commandPlaybackParentNanoseconds = playbackStats.parentApplyNanoseconds,
        .commandPlaybackDestroyNanoseconds = playbackStats.destroyPhaseNanoseconds,
        .hierarchyRecordNanoseconds = hierarchyRecordNanoseconds,
        .nameAssignmentNanoseconds = nameAssignmentNanoseconds,
        .hasGeneratedEntityIndexRange = maxGeneratedEntityIndex != kb::ecs::kInvalidGeneratedEntityIndex,
        .hasContiguousGeneratedEntityRuns = CreatesContiguousPrefabOrderEntityRuns(*baked),
        .maxGeneratedEntityIndex = maxGeneratedEntityIndex == kb::ecs::kInvalidGeneratedEntityIndex ? 0U : maxGeneratedEntityIndex,
    };
    return instances;
}

} // namespace

std::vector<ScenePrefabInstance> ScenePrefabBulkInstantiationService::Instantiate(
    Scene& scene,
    const ScenePrefab& prefab,
    std::size_t count,
    const ScenePrefabInstantiationSettings& settings) {
    return InstantiateInternal(scene, prefab, count, settings, true);
}

std::vector<ScenePrefabInstance> ScenePrefabBulkInstantiationService::InstantiateBaked(
    Scene& scene,
    const ScenePrefab& prefab,
    const ScenePrefabBakedData& baked,
    std::size_t count,
    const ScenePrefabInstantiationSettings& settings) {
    return InstantiateInternal(scene, prefab, count, settings, true, &baked);
}

ScenePrefabInstantiationStats ScenePrefabBulkInstantiationService::InstantiateBatch(
    Scene& scene,
    const ScenePrefab& prefab,
    std::size_t count,
    const ScenePrefabInstantiationSettings& settings) {
    static_cast<void>(InstantiateInternal(scene, prefab, count, settings, false));
    return SceneAccess::State(scene).lastPrefabInstantiationStats;
}

ScenePrefabInstantiationStats ScenePrefabBulkInstantiationService::InstantiateBatchBaked(
    Scene& scene,
    const ScenePrefab& prefab,
    const ScenePrefabBakedData& baked,
    std::size_t count,
    const ScenePrefabInstantiationSettings& settings) {
    static_cast<void>(InstantiateInternal(scene, prefab, count, settings, false, &baked));
    return SceneAccess::State(scene).lastPrefabInstantiationStats;
}

} // namespace kb::scene
