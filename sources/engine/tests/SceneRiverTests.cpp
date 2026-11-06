// The River scene component: the water the scene system makes of it, its waterfalls and mist, and how it follows changes.
#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/scene/ParticleEffectComponent.hpp"
#include "engine/scene/RiverComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneMeshRendererComponents.hpp"
#include "engine/scene/SceneRivers.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/SceneSurfaceGeometry.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace kb::tests {
namespace {

using kb::scene::SceneEntity;

[[nodiscard]] std::vector<std::string> ChildNames(kb::scene::Scene& scene, SceneEntity owner) {
    std::vector<std::string> names;
    for (const SceneEntity child : scene.Hierarchy().ChildEntities(owner)) names.push_back(scene.Entities().Name(child));
    std::ranges::sort(names);
    return names;
}

[[nodiscard]] SceneEntity Child(kb::scene::Scene& scene, SceneEntity owner, const std::string& name) {
    for (const SceneEntity child : scene.Hierarchy().ChildEntities(owner)) {
        if (scene.Entities().Name(child) == name) return child;
    }
    return {};
}

[[nodiscard]] kb::scene::RiverComponent FallingRiver() {
    kb::scene::RiverComponent river{};
    river.pointCount = 4U;
    river.points[0] = { { 0.0F, 10.0F, 0.0F }, 4.0F, 1.0F, 1.5F };
    river.points[1] = { { 20.0F, 10.0F, 0.0F }, 4.0F, 1.0F, 1.5F };
    river.points[2] = { { 24.0F, 2.0F, 0.0F }, 4.0F, 1.0F, 3.0F };
    river.points[3] = { { 44.0F, 2.0F, 0.0F }, 4.0F, 1.0F, 1.5F };
    river.materialAssetId = 101U;
    river.waterfallMaterialAssetId = 102U;
    river.mistEffectAssetId = 103U;
    return river;
}

void TestRiverWater() {
    kb::scene::Scene scene;
    const SceneEntity owner = scene.Entities().CreateEntity(kb::scene::SceneObjectDesc{ .name = "Brook" });
    scene.Components().Rivers().Set(owner, FallingRiver());
    Require(kb::scene::SynchronizeSceneRivers(scene).empty(), "the river water is built");
    // The water is in children that are never saved: the stream, and an object for the one waterfall.
    Require(ChildNames(scene, owner) == std::vector<std::string>{ "River Water", "River Waterfall 1" }, "a river with one fall gets a stream and a waterfall object");
    const SceneEntity stream = Child(scene, owner, "River Water");
    const SceneEntity fall = Child(scene, owner, "River Waterfall 1");
    const kb::scene::MeshRendererComponent* streamRenderer = scene.Components().MeshRenderers().TryGet(stream);
    const kb::scene::MeshRendererComponent* fallRenderer = scene.Components().MeshRenderers().TryGet(fall);
    Require(streamRenderer != nullptr && streamRenderer->transient && streamRenderer->materialAssetId == 101U && streamRenderer->meshAssetId != 0U && !streamRenderer->castsShadow,
        "the stream is a transient, non shadow casting mesh with the river's water material");
    Require(fallRenderer != nullptr && fallRenderer->transient && fallRenderer->materialAssetId == 102U && fallRenderer->meshAssetId != 0U,
        "the waterfall is a transient mesh with the waterfall material");
    // The waterfall stands at the foot of the fall and carries the mist, a particle effect within a handheld's budget.
    const kb::scene::TransformComponent foot = scene.Transforms().Get(fall);
    Require(std::abs(foot.localPosition.x - 24.0F) < 0.5F && std::abs(foot.localPosition.y - 2.0F) < 0.05F, "the waterfall object stands where the water lands");
    const kb::scene::ParticleEffectComponent* mist = scene.Components().ParticleEffects().TryGet(fall);
    Require(mist != nullptr && mist->effectAssetId == 103U && mist->maxParticlesOverride <= 500U && mist->enabled, "the foot of the fall emits the mist effect within 500 particles");
    Require(scene.Components().ParticleEffects().TryGet(stream) == nullptr, "the stream has no mist");
    std::size_t published = 0U;
    for (const kb::scene::SurfaceGeometry& surface : kb::scene::SceneSurfaceGeometry::All(scene)) {
        if (surface.owner == stream || surface.owner == fall) ++published;
    }
    Require(published == 2U, "both surfaces are published to the renderer");

    // Nothing is rebuilt while the river does not change.
    const std::uint64_t revision = kb::scene::SceneSurfaceGeometry::All(scene).front().revision;
    Require(kb::scene::SynchronizeSceneRivers(scene).empty() && kb::scene::SceneSurfaceGeometry::All(scene).front().revision == revision, "an unchanged river is not built again");

    // A gentler river has no waterfall: its object goes, the stream stays.
    kb::scene::RiverComponent gentle = FallingRiver();
    gentle.points[2].position.y = 9.5F;
    scene.Components().Rivers().Set(owner, gentle);
    Require(kb::scene::SynchronizeSceneRivers(scene).empty(), "the changed river is built");
    Require(ChildNames(scene, owner) == std::vector<std::string>{ "River Water" }, "a river without a steep stretch has no waterfall object");

    // Without the mist effect the waterfall has no mist, and a disabled river draws nothing.
    kb::scene::RiverComponent dry = FallingRiver();
    dry.mistEffectAssetId = 0U;
    scene.Components().Rivers().Set(owner, dry);
    Require(kb::scene::SynchronizeSceneRivers(scene).empty(), "the river with no mist is built");
    Require(scene.Components().ParticleEffects().TryGet(Child(scene, owner, "River Waterfall 1")) == nullptr, "no mist effect means no mist");
    dry.enabled = false;
    scene.Components().Rivers().Set(owner, dry);
    Require(kb::scene::SynchronizeSceneRivers(scene).empty() && ChildNames(scene, owner).empty(), "a disabled river takes its water away");

    // A river that is removed takes its water with it, and the published surfaces go too.
    dry.enabled = true;
    scene.Components().Rivers().Set(owner, dry);
    Require(kb::scene::SynchronizeSceneRivers(scene).empty() && !ChildNames(scene, owner).empty(), "an enabled river draws again");
    scene.Components().Rivers().Remove(owner);
    Require(kb::scene::SynchronizeSceneRivers(scene).empty() && ChildNames(scene, owner).empty() && kb::scene::SceneSurfaceGeometry::All(scene).empty(),
        "a river that is removed leaves no water or surface behind");
}

} // namespace

void RunSceneRiverTests() {
    TestRiverWater();
}

} // namespace kb::tests
