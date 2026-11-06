#include "RendererTestSupport.hpp"
#include "scene/lighting/SceneLightGrid.hpp"
#include "shadow/PointShadowCasterChanges.hpp"
#include "shadow/PointShadowPassPlanner.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <random>
#include <vector>

namespace kb::render::tests {
namespace {

// More shadow-casting lights than the atlas holds: the nearest ones keep their shadow in a stable order, the rest
// are counted as dropped, the tiles shrink in steps and a light that cannot cast (hidden, no range, directional,
// masked out) never counts.
void PointShadowSelectionKeepsTheNearestAndCountsTheRest() {
    RenderScene scene;
    constexpr std::uint32_t kLights = ScenePointShadowBinding::kMaxLights + 9U;
    for (std::uint32_t id = 1U; id <= kLights; ++id) {
        LightRenderProxyDesc light{.entityId = id};
        light.kind = RenderLightKind::Point;
        light.castsShadow = true;
        light.intensity = 1.0F;
        light.range = 10.0F;
        light.position = {static_cast<float>(id), 0.0F, 0.0F};
        static_cast<void>(scene.UpsertLight(light));
    }
    LightRenderProxyDesc hidden{.entityId = 900U};
    hidden.kind = RenderLightKind::Point;
    hidden.castsShadow = true;
    hidden.visible = false;
    hidden.range = 10.0F;
    hidden.intensity = 1.0F;
    static_cast<void>(scene.UpsertLight(hidden));
    LightRenderProxyDesc rangeless = hidden;
    rangeless.entityId = 901U;
    rangeless.visible = true;
    rangeless.range = 0.0F;
    static_cast<void>(scene.UpsertLight(rangeless));
    LightRenderProxyDesc sun = hidden;
    sun.entityId = 902U;
    sun.visible = true;
    sun.kind = RenderLightKind::Directional;
    static_cast<void>(scene.UpsertLight(sun));

    const std::array<float, 3> camera{0.0F, 0.0F, 0.0F};
    const PointShadowLightSelection selection = PointShadowPassPlanner::Select(scene, &camera, ~0U);
    Require(selection.count == ScenePointShadowBinding::kMaxLights && selection.droppedCount == 9U,
        "the planner must keep kMaxLights shadow lights and count the other nine as dropped");
    for (std::uint32_t slot = 0U; slot < selection.count; ++slot) {
        Require(selection.entityIds[slot] == slot + 1U, "the planner must order the kept shadow lights nearest first");
    }
    // The nearest lights stay when the camera is at the far end.
    const std::array<float, 3> far{static_cast<float>(kLights), 0.0F, 0.0F};
    const PointShadowLightSelection reverse = PointShadowPassPlanner::Select(scene, &far, ~0U);
    Require(reverse.entityIds[0] == kLights && reverse.droppedCount == 9U && reverse.entityIds[reverse.count - 1U] == kLights - reverse.count + 1U,
        "the planner must follow the camera when it picks the lights that keep a shadow");
    const PointShadowLightSelection masked = PointShadowPassPlanner::Select(scene, &camera, 0U);
    Require(masked.count == 0U && masked.droppedCount == 0U, "lights outside the camera's culling mask must not count");

    SceneRenderLightingConfig config;
    config.shadowsEnabled = true;
    config.shadowMapSize = 2048U;
    std::array<std::uint64_t, ScenePointShadowBinding::kMaxLights> owners{};
    const PointShadowSetup setup = PointShadowPassPlanner::Build(scene, config, &camera, ~0U, BGFX_INVALID_HANDLE, owners);
    Require(setup.valid && setup.lightCount == ScenePointShadowBinding::kMaxLights && setup.droppedLights == 9U,
        "the shadow setup must report the lights it dropped");
    const std::uint32_t full = PointShadowPassPlanner::TileSizeFor(config, 4U);
    Require(PointShadowPassPlanner::TileSizeFor(config, 5U) == full / 2U && PointShadowPassPlanner::TileSizeFor(config, 9U) == full / 4U &&
            PointShadowPassPlanner::TileSizeFor(config, 32U) == full / 4U,
        "shadow tiles must halve beyond four lights and again beyond eight");
    Require(setup.tileSize * ScenePointShadowBinding::kMaxLights == setup.atlasHeight,
        "the atlas of a full light set must hold one row of tiles per light");
}

} // namespace

namespace {
std::vector<std::uint32_t> CellLights(const SceneLightGrid& grid, std::array<float, 3> position) {
    const auto atlas = grid.Atlas();
    const auto& header = atlas[grid.HeaderOffset() + grid.CellFor(position)];
    std::vector<std::uint32_t> result;
    for (std::uint32_t entry = 0U; entry < static_cast<std::uint32_t>(header[1]); ++entry) {
        const auto address = static_cast<std::uint32_t>(header[0]) + entry;
        result.push_back(static_cast<std::uint32_t>(atlas[grid.IndexOffset() + address / 4U][address % 4U]));
    }
    return result;
}
}

[[nodiscard]] LightRenderProxyDesc ShadowLightAt(std::uint64_t id, float x) {
    LightRenderProxyDesc light{.entityId = id};
    light.kind = RenderLightKind::Point;
    light.castsShadow = true;
    light.intensity = 1.0F;
    light.range = 10.0F;
    light.position = {x, 0.0F, 0.0F};
    return light;
}

[[nodiscard]] MeshRenderProxyDesc CasterAt(std::uint64_t id, float x) {
    MeshRenderProxyDesc mesh{.entityId = id, .meshAssetId = 7U};
    mesh.model[0] = mesh.model[5] = mesh.model[10] = mesh.model[15] = 1.0F;
    mesh.model[12] = x;
    mesh.boundsOverride = {.center = {0.0F, 0.0F, 0.0F}, .radius = 0.5F};
    return mesh;
}

// The lights that get a shadow are the most important ones, not just the nearest: a bright light with a long reach outranks a
// dim or short one that is nearer, a light that holds a row keeps it against an equal newcomer, a reach under 3% of the
// distance is not worth a pass, and a phone draws no cube shadows (its spot shadows stay).
void PointShadowLightsAreChosenByImportance() {
    RenderScene scene;
    LightRenderProxyDesc dimNear = ShadowLightAt(1U, 2.0F);
    dimNear.intensity = 0.1F;
    dimNear.range = 4.0F;
    LightRenderProxyDesc brightFar = ShadowLightAt(2U, 12.0F);
    brightFar.intensity = 40.0F;
    brightFar.range = 30.0F;
    LightRenderProxyDesc tiny = ShadowLightAt(3U, 500.0F);
    tiny.range = 1.0F;
    LightRenderProxyDesc spot = ShadowLightAt(4U, 6.0F);
    spot.kind = RenderLightKind::Spot;
    for (const LightRenderProxyDesc& light : { dimNear, brightFar, tiny, spot }) static_cast<void>(scene.UpsertLight(light));
    const std::array<float, 3> camera{ 0.0F, 0.0F, 0.0F };
    SceneRenderLightingConfig config;
    config.pointShadowMaxLights = 1U;
    PointShadowLightSelection selection = PointShadowPassPlanner::Select(scene, &camera, ~0U, config);
    Require(selection.count == 1U && selection.entityIds[0] == 2U && selection.droppedCount == 2U,
        "one shadow must go to the bright light with the long reach, and the tiny far light is not even counted as dropped");
    config.pointShadowMaxLights = 32U;
    selection = PointShadowPassPlanner::Select(scene, &camera, ~0U, config);
    Require(selection.count == 3U && selection.entityIds[0] == 2U && selection.entityIds[2] == 1U,
        "the lights must come most important first, the dim one last, and the tiny far one not at all");
    config.pointShadowCubeEnabled = false;
    selection = PointShadowPassPlanner::Select(scene, &camera, ~0U, config);
    Require(selection.count == 1U && selection.entityIds[0] == 4U && selection.droppedCount == 0U,
        "without cube shadows only the spot light keeps one, and the lights the policy removes are not reported as dropped");

    RenderScene equals;
    static_cast<void>(equals.UpsertLight(ShadowLightAt(10U, 5.0F)));
    static_cast<void>(equals.UpsertLight(ShadowLightAt(11U, -5.0F)));
    SceneRenderLightingConfig one;
    one.pointShadowMaxLights = 1U;
    std::array<std::uint64_t, ScenePointShadowBinding::kMaxLights> held{};
    held[3] = 11U;
    Require(PointShadowPassPlanner::Select(equals, &camera, ~0U, one).entityIds[0] == 10U &&
            PointShadowPassPlanner::Select(equals, &camera, ~0U, one, &held).entityIds[0] == 11U,
        "a light that holds a row must keep it against an equal newcomer");
}

// A light keeps its row of the atlas while the camera reorders the lights, a row freed by a light that left stays
// empty until a new light takes it, and the rows are dealt out again once empty rows would shrink the tiles.
void PointShadowRowsFollowTheirLights() {
    RenderScene scene;
    for (std::uint64_t id = 1U; id <= 3U; ++id) static_cast<void>(scene.UpsertLight(ShadowLightAt(id, static_cast<float>(id))));
    SceneRenderLightingConfig config;
    config.shadowsEnabled = true;
    std::array<std::uint64_t, ScenePointShadowBinding::kMaxLights> owners{};
    const std::array<float, 3> near{ 0.0F, 0.0F, 0.0F };
    const std::array<float, 3> far{ 10.0F, 0.0F, 0.0F };
    auto setup = PointShadowPassPlanner::Build(scene, config, &near, ~0U, BGFX_INVALID_HANDLE, owners);
    Require(setup.lightCount == 3U && setup.binding.entityId[0] == 1U && setup.binding.entityId[2] == 3U,
        "the nearest light must take the first row");
    setup = PointShadowPassPlanner::Build(scene, config, &far, ~0U, BGFX_INVALID_HANDLE, owners);
    Require(setup.binding.entityId[0] == 1U && setup.binding.entityId[1] == 2U && setup.binding.entityId[2] == 3U,
        "a camera that reorders the lights must not move them between rows");
    Require(scene.RemoveLight(2U), "the light did not leave");
    setup = PointShadowPassPlanner::Build(scene, config, &far, ~0U, BGFX_INVALID_HANDLE, owners);
    Require(setup.lightCount == 3U && setup.selectedLights == 2U && setup.binding.entityId[1] == 0U && setup.faceCount[1] == 0U &&
            setup.binding.entityId[2] == 3U,
        "the row of a light that left must stay empty and the others must keep theirs");
    static_cast<void>(scene.UpsertLight(ShadowLightAt(4U, 4.0F)));
    setup = PointShadowPassPlanner::Build(scene, config, &far, ~0U, BGFX_INVALID_HANDLE, owners);
    Require(setup.selectedLights == 3U && setup.binding.entityId[1] == 4U, "a new light must take the lowest free row");
    for (std::uint64_t id = 5U; id <= 12U; ++id) static_cast<void>(scene.UpsertLight(ShadowLightAt(id, static_cast<float>(id))));
    setup = PointShadowPassPlanner::Build(scene, config, &far, ~0U, BGFX_INVALID_HANDLE, owners);
    for (std::uint64_t id = 5U; id <= 11U; ++id) static_cast<void>(scene.RemoveLight(id));
    static_cast<void>(scene.RemoveLight(1U));
    static_cast<void>(scene.RemoveLight(3U));
    setup = PointShadowPassPlanner::Build(scene, config, &far, ~0U, BGFX_INVALID_HANDLE, owners);
    Require(setup.selectedLights == 2U && setup.lightCount == 2U && setup.tileSize == PointShadowPassPlanner::TileSizeFor(config, 2U),
        "empty rows must not keep the tiles small once their lights are gone");
}

// A cached tile of a light is discarded only by a shadow caster change that reaches it: a caster that moves far away
// leaves it, one that moves in range, appears or leaves takes it, and a frame the cache did not see takes them all.
void PointShadowCasterChangesDiscardOnlyTilesTheyReach() {
    RenderScene scene;
    for (std::uint64_t id = 1U; id <= 3U; ++id) static_cast<void>(scene.UpsertLight(ShadowLightAt(id, 100.0F * static_cast<float>(id - 1U))));
    static_cast<void>(scene.UpsertMesh(CasterAt(10U, 1.0F)));
    static_cast<void>(scene.UpsertMesh(CasterAt(11U, 1000.0F)));
    SceneRenderLightingConfig config;
    config.shadowsEnabled = true;
    RenderResourceRegistry resources;
    SceneRenderResourceMap resourceMap;
    ShadowMapResource map;
    const std::array<float, 3> camera{ 0.0F, 0.0F, 0.0F };
    std::uint64_t rendererFrame = 1U;
    const auto frame = [&](auto&& change) {
        scene.BeginMotionFrame(rendererFrame++);
        change();
        const PointShadowSetup setup = PointShadowPassPlanner::Build(scene, config, &camera, ~0U, BGFX_INVALID_HANDLE, map.RowOwners());
        PointShadowCasterChanges::Apply(scene, resources, resourceMap, setup, map);
        return setup;
    };
    const auto cacheAll = [&map] { for (std::uint32_t row = 0U; row < 3U; ++row) map.SetCachedTiles(row, 100U + row); };
    const auto cached = [&map] { return std::array{ map.CachedTiles(0U) != 0U, map.CachedTiles(1U) != 0U, map.CachedTiles(2U) != 0U }; };
    static_cast<void>(frame([] {}));
    cacheAll();
    static_cast<void>(frame([&] { Require(scene.UpdateMeshTransform(11U, CasterAt(11U, 1001.0F).model), "the far caster did not move"); }));
    Require((cached() == std::array{ true, true, true }), "a caster moving far from every light must keep their tiles");
    static_cast<void>(frame([&] { Require(scene.UpdateMeshTransform(10U, CasterAt(10U, 1.2F).model), "the near caster did not move"); }));
    Require((cached() == std::array{ false, true, true }), "a caster moving in a light's range must discard that light's tiles only");
    cacheAll();
    static_cast<void>(frame([] {}));
    Require((cached() == std::array{ true, true, true }), "a frame without changes must keep every tile");
    static_cast<void>(frame([&] { static_cast<void>(scene.UpsertMesh(CasterAt(12U, 101.0F))); }));
    Require((cached() == std::array{ true, false, true }), "a caster appearing in a light's range must discard its tiles");
    cacheAll();
    static_cast<void>(frame([&] { Require(scene.RemoveMesh(12U), "the caster did not leave"); }));
    Require((cached() == std::array{ false, false, false }), "a removed caster must discard every tile");
    cacheAll();
    scene.BeginMotionFrame(rendererFrame++);
    static_cast<void>(frame([] {}));
    Require((cached() == std::array{ false, false, false }), "a frame the cache did not see must discard every tile");
}

void RunSceneLightGridTests() {
    PointShadowSelectionKeepsTheNearestAndCountsTheRest();
    PointShadowRowsFollowTheirLights();
    PointShadowLightsAreChosenByImportance();
    PointShadowCasterChangesDiscardOnlyTilesTheyReach();
    RenderScene scene;
    SceneRenderLightingConfig config{};
    config.lightingPath = SceneRenderLightingPath::ClusteredForwardPlus;
    config.clusterDimensions = {16U, 9U, 24U};
    std::mt19937 random{713U};
    std::uniform_real_distribution<float> coordinate{-100.0F, 100.0F};
    for (std::uint64_t id = 1U; id <= 640U; ++id) {
        LightRenderProxyDesc light{.entityId = id};
        light.kind = static_cast<RenderLightKind>(id % 6U);
        light.position = {coordinate(random), coordinate(random), coordinate(random)};
        light.range = 5.0F + static_cast<float>(id % 20U);
        light.areaWidth = 4.0F;
        light.areaHeight = 3.0F;
        static_cast<void>(scene.UpsertLight(light));
    }
    SceneLightGrid grid;
    Require(grid.Build(scene.LightProxies(), config, 1U, 640U), "Light grid could not build 640 lights");
    Require(grid.LightCount() == 640U && grid.Minimum()[3] == 0.0F,
        "Light grid lost lights or produced a nonfinite uniform lane");
    const auto atlas = grid.Atlas();
    Require(atlas[1][0] == scene.FindLightByEntity(640U)->desc.position[0], "Light grid did not preserve primary light at index zero");
    // Independent sphere-influence oracle: every influencing light must occur in
    // the queried list. It does not share the grid's box/cell traversal.
    for (std::uint32_t sample = 0U; sample < 1200U; ++sample) {
        std::array<float, 3> position{coordinate(random), coordinate(random), coordinate(random)};
        if (sample < 64U) {
            const auto axis = sample % 3U;
            position[axis] = grid.Minimum()[axis] + static_cast<float>(sample % 8U) / grid.InverseCellSize()[axis];
        }
        const auto list = CellLights(grid, position);
        for (std::uint32_t index = 0U; index < grid.LightCount(); ++index) {
            const auto& direction = atlas[index * 5U];
            const auto& location = atlas[index * 5U + 1U];
            const auto& size = atlas[index * 5U + 3U];
            double squaredDistance = 0.0;
            for (std::size_t axis = 0U; axis < 3U; ++axis) {
                const double delta = static_cast<double>(position[axis]) - location[axis];
                squaredDistance += delta * delta;
            }
            const double radius = location[3] + (direction[3] > 2.5F ? (size[2] + size[3]) * 0.5 : 0.0);
            if (direction[3] == 0.0F || squaredDistance <= radius * radius)
                Require(std::ranges::find(list, index) != list.end(), "Spatial light list omitted an influencing light");
        }
    }
    const auto outside = CellLights(grid, {10000.0F, 10000.0F, 10000.0F});
    Require(outside.size() == 106U, "Outside grid must retain every directional light");
    for (const auto index : outside) Require(atlas[index * 5U][3] == 0.0F, "Outside grid contains bounded lights");
    Require(!grid.Build(scene.LightProxies(), config, 1U, 640U, 20U) && grid.Dimensions()[3] == 0.0F,
        "Light grid allocation failure published an enabled partial grid");

    RenderScene overlap;
    for (std::uint64_t id = 1U; id <= 512U; ++id)
        static_cast<void>(overlap.UpsertLight(LightRenderProxyDesc{.entityId = id, .position = {0.0F, 0.0F, 1.0F}}));
    Require(grid.Build(overlap.LightProxies(), config, 1U, 1U), "Overlapping light grid build failed");
    Require(CellLights(grid, {}).size() == 512U, "Light grid still caps an overlapping cell at 32 lights");
    Require(!grid.Build(overlap.LightProxies(), config, 1U, 1U, 10000U) && grid.Dimensions()[3] == 0.0F,
        "Reference budget overflow did not reject the grid before publication");
    config.editorPreviewKeyLightEnabled = true;
    config.editorPreviewKeyLightIntensity = 1.0F;
    Require(grid.Build(overlap.LightProxies(), config, 1U, 1U) && grid.LightCount() == 513U &&
        CellLights(grid, {1000.0F, 0.0F, 0.0F}).size() == 1U, "Preview light was not retained in the grid");
    config.editorPreviewKeyLightEnabled = false;
    auto hidden = overlap.FindLightByEntity(1U)->desc;
    hidden.visible = false;
    static_cast<void>(overlap.UpsertLight(hidden));
    auto masked = overlap.FindLightByEntity(2U)->desc;
    masked.layer = 2U;
    static_cast<void>(overlap.UpsertLight(masked));
    auto invalid = overlap.FindLightByEntity(3U)->desc;
    invalid.range = std::numeric_limits<float>::quiet_NaN();
    static_cast<void>(overlap.UpsertLight(invalid));
    Require(grid.Build(overlap.LightProxies(), config, 1U, 1U) && grid.LightCount() == 509U,
        "Grid did not filter hidden, masked and nonfinite lights");

    RenderScene lifecycle;
    const auto initial = lifecycle.LightContentRevision();
    const LightRenderProxyDesc light{.entityId = 17U};
    static_cast<void>(lifecycle.UpsertLight(light));
    const auto inserted = lifecycle.LightContentRevision();
    Require(inserted != initial, "Insertion did not invalidate derived lighting");
    lifecycle.ClearDirty();
    static_cast<void>(lifecycle.UpsertLight(light));
    Require(lifecycle.LightContentRevision() == inserted, "Unchanged light invalidated the atlas");
    auto moved = light;
    moved.position[0] = 10.0F;
    static_cast<void>(lifecycle.UpsertLight(moved));
    Require(lifecycle.LightContentRevision() != inserted, "Moved light did not invalidate the atlas");
    const auto changed = lifecycle.LightContentRevision();
    Require(!lifecycle.RemoveLight(18U) && lifecycle.LightContentRevision() == changed,
        "Removing an absent light changed the content revision");
    Require(lifecycle.RemoveLightsNotInSorted({}) == 1U && lifecycle.LightContentRevision() != changed,
        "Pruning a light did not invalidate the atlas");
    const auto removed = lifecycle.LightContentRevision();
    static_cast<void>(lifecycle.UpsertLight(light));
    Require(lifecycle.LightContentRevision() != removed, "Reused entity ID retained a stale light snapshot");
    std::cout << "light_grid_cpu: 640 lights, 1200 influence queries, 512 overlap, lifecycle/filter/budget PASS\n";
}
} // namespace kb::render::tests
