#include "RendererTestSupport.hpp"
#include "scene/lighting/SceneLightGrid.hpp"
#include "shadow/PointShadowPassPlanner.hpp"

#include <algorithm>
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
    const PointShadowSetup setup = PointShadowPassPlanner::Build(scene, config, &camera, ~0U, BGFX_INVALID_HANDLE);
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

void RunSceneLightGridTests() {
    PointShadowSelectionKeepsTheNearestAndCountsTheRest();
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
