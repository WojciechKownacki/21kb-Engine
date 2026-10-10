#include "engine/scene/SceneComponentAuthoring.hpp"

#include "engine/project/ProjectDescriptor.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/ScenePrefabNode.hpp"

#include <algorithm>
#include <array>

namespace kb::scene {
namespace {

using HasFn = bool (*)(const Scene&, SceneEntity);
using AddFn = bool (*)(Scene&, SceneEntity);
using RemoveFn = void (*)(Scene&, SceneEntity);
using InNodeFn = bool (*)(const ScenePrefabNodeComponents&);

struct Entry {
    SceneComponentKind kind;
    HasFn has;
    AddFn add;
    RemoveFn remove;
    InNodeFn inNode; // the same component in a saved scene/prefab node
};

// Store accessor + default value for the components whose store Set() returns void.
#define KB_COMPONENT(store, field, value)                                                                    \
    [](const Scene& scene, SceneEntity entity) { return scene.Components().store().Has(entity); },          \
    [](Scene& scene, SceneEntity entity) { scene.Components().store().Set(entity, value); return true; },  \
    [](Scene& scene, SceneEntity entity) { scene.Components().store().Remove(entity); },                   \
    [](const ScenePrefabNodeComponents& node) { return node.field.has_value(); }

// Same, for stores whose Set() validates and can refuse.
#define KB_CHECKED_COMPONENT(store, field, value)                                                            \
    [](const Scene& scene, SceneEntity entity) { return scene.Components().store().Has(entity); },          \
    [](Scene& scene, SceneEntity entity) { return scene.Components().store().Set(entity, value); },        \
    [](Scene& scene, SceneEntity entity) { scene.Components().store().Remove(entity); },                   \
    [](const ScenePrefabNodeComponents& node) { return node.field.has_value(); }

[[nodiscard]] ParticleEffectComponent DisabledParticleEffect() {
    ParticleEffectComponent component{};
    component.enabled = false; // starts once an effect asset is chosen
    return component;
}

const std::array kEntries{
    Entry{ { "Camera", "Camera", {} }, KB_COMPONENT(Cameras, camera, (CameraComponent{ .primary = true })) },
    Entry{ { "Light", "Light", "Rendering.BasicLighting" }, KB_COMPONENT(Lights, light, LightComponent{}) },
    Entry{ { "MeshRenderer", "Mesh Renderer", {} }, KB_COMPONENT(MeshRenderers, meshRenderer, MeshRendererComponent{}) },
    Entry{ { "Particle Effect", "Particle Effect", "Rendering.21kbParticle" },
        KB_COMPONENT(ParticleEffects, particleEffect, DisabledParticleEffect()) },
    Entry{ { "AudioSource", "Audio Source", "Audio.Miniaudio" }, KB_COMPONENT(AudioSources, audioSource, AudioSourceComponent{}) },
    Entry{ { "AudioListener", "Audio Listener", "Audio.Miniaudio" }, KB_COMPONENT(AudioListeners, audioListener, AudioListenerComponent{}) },
    Entry{ { "Animator", "Animator", {} }, KB_COMPONENT(Animators, animator, Animator{}) },
    Entry{ { "Rigidbody", "Rigidbody", "Physics.Jolt" }, KB_COMPONENT(Rigidbodies, rigidbody, RigidbodyComponent{}) },
    Entry{ { "Collider", "Collider", "Physics.Jolt" }, KB_COMPONENT(Colliders, collider, ColliderComponent{}) },
    Entry{ { "CharacterController", "Character Controller", "Physics.Jolt" },
        KB_COMPONENT(CharacterControllers, characterController, CharacterControllerComponent{}) },
    Entry{ { "Joint", "Joint", "Physics.Jolt" }, KB_COMPONENT(Joints, joint, JointComponent{}) },
    Entry{ { "SkeletonBinding", "Skeleton Binding", {} }, KB_CHECKED_COMPONENT(SkeletonBindings, skeletonBinding, SkeletonBindingComponent{}) },
    Entry{ { "DeformedGeometry", "Deformed Geometry", {} },
        KB_CHECKED_COMPONENT(DeformedGeometries, deformedGeometry, DrawD3DeformedGeometryComponent{}) },
    Entry{ { "Tags", "Object Classification", {} }, KB_COMPONENT(Tags, tags, TagsComponent{}) },
    Entry{ { "RegionShape", "Region Shape", {} }, KB_COMPONENT(RegionShapes, regionShape, RegionShapeComponent{}) },
    Entry{ { "GuideCurve", "Guide Curve", {} }, KB_COMPONENT(GuideCurves, guideCurve, GuideCurveComponent{}) },
    Entry{ { "ContentInstance", "Content Instance", {} }, KB_COMPONENT(ContentInstances, contentInstance, ContentInstanceComponent{}) },
    Entry{ { "StreamFocus", "Stream Focus", {} }, KB_COMPONENT(StreamFocuses, streamFocus, StreamFocusComponent{}) },
    Entry{ { "WorldBackdrop", "World Backdrop", {} }, KB_COMPONENT(WorldBackdrops, worldBackdrop, WorldBackdropComponent{}) },
    Entry{ { "Ambient Radiance", "Ambient Radiance", "Rendering.BasicLighting" }, KB_COMPONENT(AmbientRadiances, ambientRadiance, AmbientRadianceComponent{}) },
    Entry{ { "Detail Switch", "Detail Switch", {} }, KB_COMPONENT(DetailSwitches, detailSwitch, SceneDetailSwitchComponent{}) },
    Entry{ { "Visibility Blocker", "Visibility Blocker", {} },
        KB_COMPONENT(VisibilityBlockers, visibilityBlocker, SceneVisibilityBlockerComponent{}) },
    Entry{ { "Visibility Cell", "Visibility Cell", {} },
        [](const Scene& scene, SceneEntity entity) { return scene.Components().VisibilityCells().Has(entity); },
        [](Scene& scene, SceneEntity entity) {
            if (!scene.Components().RegionShapes().Has(entity)) scene.Components().RegionShapes().Set(entity, RegionShapeComponent{});
            scene.Components().VisibilityCells().Set(entity, VisibilityCellComponent{});
            return true;
        },
        [](Scene& scene, SceneEntity entity) { scene.Components().VisibilityCells().Remove(entity); },
        [](const ScenePrefabNodeComponents& node) { return node.visibilityCell.has_value(); } },
    Entry{ { "Region Portal", "Region Portal", {} },
        [](const Scene& scene, SceneEntity entity) { return scene.Components().RegionPortals().Has(entity); },
        [](Scene& scene, SceneEntity entity) {
            if (!scene.Components().RegionShapes().Has(entity)) scene.Components().RegionShapes().Set(entity, RegionShapeComponent{});
            scene.Components().RegionPortals().Set(entity, SceneRegionPortalComponent{});
            return true;
        },
        [](Scene& scene, SceneEntity entity) { scene.Components().RegionPortals().Remove(entity); },
        [](const ScenePrefabNodeComponents& node) { return node.regionPortal.has_value(); } },
    Entry{ { "Secondary Frame", "Secondary Frame", {} },
        [](const Scene& scene, SceneEntity entity) { return scene.Components().AuxFrames().Has(entity); },
        [](Scene& scene, SceneEntity entity) {
            if (!scene.Components().Cameras().Has(entity)) scene.Components().Cameras().Set(entity, CameraComponent{});
            scene.Components().AuxFrames().Set(entity, AuxFrameComponent{});
            return true;
        },
        [](Scene& scene, SceneEntity entity) { scene.Components().AuxFrames().Remove(entity); },
        [](const ScenePrefabNodeComponents& node) { return node.auxFrame.has_value(); } },
    Entry{ { "Geometry Swarm", "Geometry Swarm", {} }, KB_COMPONENT(GeometrySwarms, geometrySwarm, GeometrySwarmComponent{}) },
    Entry{ { "Surface Cast", "Surface Cast", {} }, KB_COMPONENT(SurfaceCasts, surfaceCast, SurfaceCastComponent{}) },
    Entry{ { "Facing Panel", "Facing Panel", {} }, KB_COMPONENT(FacingPanels, facingPanel, FacingPanelComponent{}) },
    Entry{ { "Line Renderer", "Line Renderer", {} },
        [](const Scene& scene, SceneEntity entity) { return scene.Components().SpaceStrokes().Has(entity); },
        [](Scene& scene, SceneEntity entity) {
            if (!scene.Components().GuideCurves().Has(entity)) scene.Components().GuideCurves().Set(entity, GuideCurveComponent{});
            scene.Components().SpaceStrokes().Set(entity, SpaceStrokeComponent{});
            return true;
        },
        [](Scene& scene, SceneEntity entity) { scene.Components().SpaceStrokes().Remove(entity); },
        [](const ScenePrefabNodeComponents& node) { return node.spaceStroke.has_value(); } },
    Entry{ { "Trail Renderer", "Trail Renderer", {} }, KB_COMPONENT(HistoryRibbons, historyRibbon, HistoryRibbonComponent{}) },
    Entry{ { "Lens Flare", "Lens Flare", {} }, KB_COMPONENT(LensEchoes, lensEcho, LensEchoComponent{}) },
    Entry{ { "NavAgent", "Nav Agent", {} }, KB_COMPONENT(NavAgents, navAgent, NavAgent{}) },
    Entry{ { "NavObstacle", "Nav Obstacle", {} }, KB_COMPONENT(NavObstacles, navObstacle, NavObstacle{}) },
    Entry{ { "NavLink", "Nav Link", {} }, KB_COMPONENT(NavLinks, navLink, NavLink{}) },
};

#undef KB_COMPONENT
#undef KB_CHECKED_COMPONENT

[[nodiscard]] const Entry* FindEntry(std::string_view id) noexcept {
    const std::string_view canonical = SceneComponentAuthoring::CanonicalName(id);
    const auto found = std::ranges::find_if(kEntries, [canonical](const Entry& entry) { return entry.kind.id == canonical; });
    return found == kEntries.end() ? nullptr : &*found;
}

const std::array<SceneComponentKind, kEntries.size()> kKinds = [] {
    std::array<SceneComponentKind, kEntries.size()> kinds{};
    std::ranges::transform(kEntries, kinds.begin(), [](const Entry& entry) { return entry.kind; });
    return kinds;
}();

} // namespace

std::span<const SceneComponentKind> SceneComponentAuthoring::Kinds() noexcept {
    return kKinds;
}

std::string_view SceneComponentAuthoring::CanonicalName(std::string_view id) noexcept {
    if (id == "3D Radiance Emitter") return "Light";
    if (id == "Kreska przestrzenna") return "Line Renderer";
    if (id == "Wst\xC4\x99" "ga historii") return "Trail Renderer";
    if (id == "Echo soczewki") return "Lens Flare";
    return id;
}

const SceneComponentKind* SceneComponentAuthoring::Find(std::string_view id) noexcept {
    const Entry* entry = FindEntry(id);
    if (entry == nullptr) {
        return nullptr;
    }
    return &kKinds[static_cast<std::size_t>(entry - kEntries.data())];
}

bool SceneComponentAuthoring::Has(const Scene& scene, SceneEntity entity, std::string_view id) {
    const Entry* entry = FindEntry(id);
    return entry != nullptr && entry->has(scene, entity);
}

bool SceneComponentAuthoring::InPrefabNode(const ScenePrefabNodeComponents& node, std::string_view id) {
    const Entry* entry = FindEntry(id);
    return entry != nullptr && entry->inNode(node);
}

bool SceneComponentAuthoring::PluginEnabled(const kb::project::ProjectDescriptor& project, const SceneComponentKind& kind) {
    if (kind.requiredPlugin.empty()) {
        return true;
    }
    const auto plugin = std::ranges::find_if(project.plugins,
        [&kind](const kb::project::ProjectPluginReference& reference) { return reference.name == kind.requiredPlugin; });
    return plugin != project.plugins.end() && plugin->enabled;
}

SceneComponentAddResult SceneComponentAuthoring::Add(
    Scene& scene, SceneEntity entity, std::string_view id, const kb::project::ProjectDescriptor& project) {
    const Entry* entry = FindEntry(id);
    if (entry == nullptr) {
        return {};
    }
    const SceneComponentKind* kind = Find(id);
    if (entry->has(scene, entity)) {
        return { SceneComponentAddStatus::AlreadyPresent, kind };
    }
    if (!PluginEnabled(project, *kind)) {
        return { SceneComponentAddStatus::PluginDisabled, kind };
    }
    return { entry->add(scene, entity) ? SceneComponentAddStatus::Added : SceneComponentAddStatus::Rejected, kind };
}

bool SceneComponentAuthoring::Remove(Scene& scene, SceneEntity entity, std::string_view id) {
    const Entry* entry = FindEntry(id);
    if (entry == nullptr || !entry->has(scene, entity)) {
        return false;
    }
    entry->remove(scene, entity);
    return true;
}

} // namespace kb::scene
