#include "RendererTestSupport.hpp"
#include "kb/render/DisplayConfig.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/Renderer.hpp"
#include "kb/render/SceneDepthPolicy.hpp"

#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/frame/RenderPassKind.hpp"
#include "kb/render/scene/MeshPipeline.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneGpuDrivenFeatureState.hpp"
#include "kb/render/scene/SceneGpuDrivenParityValidator.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"
#include "kb/render/scene/SceneRenderer.hpp"
#include "kb/render/scene/batch/SceneMeshBatchBuilder.hpp"
#include "../src/scene/pipeline/MeshPipelinePassPolicy.hpp"
#include "../src/scene/pipeline/MeshPipelineResourceResolver.hpp"
#include "../src/scene/pipeline/MeshPipelineVisibility.hpp"
#include "../src/scene/cache/SceneMeshCommandReuseGate.hpp"
#include "../src/scene/cache/SceneMeshBatchCommandCache.hpp"
#include "../src/renderer/RendererTemporalJitter.hpp"
#include "../src/scene/SceneMeshVisibilityIndexBuilder.hpp"
#include "../src/scene/SceneRenderVisibilityPublisher.hpp"

#include "engine/ecs/WorkerPool.hpp"
#include "engine/scene/RegionPortalComponent.hpp"
#include "engine/scene/RegionShapeComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/ScenePortalVisibility.hpp"
#include "engine/scene/SceneRegionPortalComponents.hpp"
#include "engine/scene/SceneRegionShapeComponents.hpp"
#include "engine/scene/SceneRenderFeedback.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneVisibilityCellComponents.hpp"
#include "engine/scene/VisibilityCellComponent.hpp"

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::render::tests {
namespace {

[[nodiscard]] std::array<float, 16> IdentityMatrix() noexcept {
    return std::array<float, 16>{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };
}

[[nodiscard]] std::array<float, 16> TranslationMatrix(float x, float y, float z) noexcept {
    std::array<float, 16> matrix = IdentityMatrix();
    matrix[12] = x;
    matrix[13] = y;
    matrix[14] = z;
    return matrix;
}

[[nodiscard]] SceneRenderCamera PerspectiveCamera() {
    RenderScene scene;
    static_cast<void>(scene.UpsertCamera(CameraRenderProxyDesc{
        .entityId = 1U, .verticalFovDegrees = 90.0F, .primary = true,
    }));
    const auto camera = scene.BuildPrimaryCamera(1024U, 1024U);
    Require(camera.has_value(), "Perspective LOD test camera could not be built");
    return *camera;
}

void RunTemporalCullingConservatismTest() {
    const RenderExtent extent{64U, 64U};
    for (const bool homogeneous : {false, true}) {
        auto base = PerspectiveCamera();
        SceneDepthPolicy::MakePerspective(base.projection.data(), 90.0F, 1.0F, 0.01F, 1200.0F, homogeneous);
        std::optional<SceneRenderCamera> previousKey;
        for (std::uint64_t frame = 0U; frame < 8U; ++frame) {
            auto camera = base;
            RendererTemporalJitter::Apply(camera, RendererTemporalJitter::Compute(frame, extent, true), extent);
            const auto key = SceneMeshCullingCamera(&camera);
            Require(!previousKey || key == previousKey, "Temporal samples must share a stable culling key");
            previousKey = key;
            const auto conservative = MeshPipelineVisibility::BuildFrustum(&camera, homogeneous);
            camera.temporalProjectionOffset = {};
            camera.cullingGuardBand = {};
            const auto original = MeshPipelineVisibility::BuildFrustum(&camera, homogeneous);
            for (const float depth : {0.011F, 1.0F, 100.0F, 1100.0F}) {
                for (int x = -102; x <= 102; ++x) {
                    for (const float y : {-1.01F, -1.0F, 0.0F, 1.0F, 1.01F}) {
                        const RenderBoundsSphere bounds{.center = {x * depth * 0.01F, y * depth, depth}, .radius = depth * 0.0001F};
                        Require(!MeshPipelineVisibility::IsInsideFrustum(original, bounds) ||
                            MeshPipelineVisibility::IsInsideFrustum(conservative, bounds),
                            "Stable culling dropped geometry visible in a temporal sample");
                    }
                }
            }
        }
    }
}

void RunSceneMeshBatchCommandRetentionTest() {
    RenderScene scene;
    MeshRenderProxyDesc proxy{.meshAssetId = 42U, .materialAssetId = 7U, .model = IdentityMatrix()};
    for (std::uint64_t id = 1U; id <= 3072U; ++id) {
        proxy.entityId = id;
        static_cast<void>(scene.UpsertMesh(proxy));
    }
    RenderMeshResource mesh{};
    mesh.indexCount = 3U; mesh.vertexCount = 3U;
    mesh.bounds = {.radius = 0.25F};
    RenderMaterialResource material{};
    SceneMeshBatchCommandCache cache;
    MeshPipelineBuildResult result;
    SceneMeshCommandReuseKey key;
    const auto build = [&](MeshPassType pass = MeshPassType::BaseOpaque, SceneRenderCamera* camera = nullptr) {
        key.camera = SceneMeshCullingCamera(camera);
        cache.Prepare(key);
        MeshPipelineBuildDesc desc{.pass = pass, .drawGroups = &scene.DrawGroups(),
            .resolvedMeshResource = &mesh, .resolvedMaterialResource = &material,
            .camera = camera, .maxDrawCommands = key.budget.maxDrawCommands,
            .maxVisibleInstances = key.budget.maxVisibleInstances,
            .resourceValidation = MeshPipelineResourceValidation::Skip, .batchCommandCache = &cache};
        MeshPipelineProcessor::BuildInto(desc, result);
        SceneMeshBatchCommandCache referenceCache;
        referenceCache.Prepare(key);
        desc.batchCommandCache = &referenceCache;
        const auto reference = MeshPipelineProcessor::Build(desc);
        Require(result.stats.visibleMeshCount == reference.stats.visibleMeshCount &&
            result.stats.culledInstanceCount == reference.stats.culledInstanceCount &&
            result.stats.droppedInstanceCount == reference.stats.droppedInstanceCount,
            "Batch retention changed culling or instance budgets");
        std::unordered_map<std::uint64_t, const SceneRenderMeshInstance*> expected;
        for (const auto& command : reference.commands) for (const auto& instance : command.instances) expected.emplace(instance.entityId, &instance);
        for (const auto& command : result.commands) {
            for (const auto& instance : command.instances) {
                const auto found = expected.find(instance.entityId);
                Require(found != expected.end(), "Retained command duplicated or invented an instance");
                const auto& ref = *found->second;
                Require(instance.model == ref.model && instance.depthBucket == ref.depthBucket &&
                    instance.worldBounds.center == ref.worldBounds.center && instance.worldBounds.radius == ref.worldBounds.radius,
                    "Retained command kept obsolete transform, bounds or depth");
                expected.erase(found);
            }
        }
        Require(expected.empty(), "Retained command lost an instance");
        std::unordered_map<std::uint64_t, const SceneGpuDrivenInputRecord*> expectedGpu;
        for (const auto& input : reference.gpuDrivenInputRecords) expectedGpu.emplace(input.entityId, &input);
        for (const auto& input : result.gpuDrivenInputRecords) {
            const auto found = expectedGpu.find(input.entityId);
            Require(found != expectedGpu.end(), "CPU validation retention duplicated or invented an entity");
            const auto& ref = *found->second;
            Require(input.worldBounds == ref.worldBounds && input.lodLevel == ref.lodLevel &&
                input.firstMeshlet == ref.firstMeshlet && input.meshletCount == ref.meshletCount,
                "CPU validation retention kept obsolete bounds or meshlets");
            expectedGpu.erase(found);
        }
        Require(expectedGpu.empty(), "CPU validation retention lost a record");
    };
    build();
    Require(result.commands.size() == 4U, "Native instance pages were not bounded");
    std::unordered_map<std::uint64_t, std::pair<std::uint64_t, const SceneRenderMeshInstance*>> identities;
    for (const auto& command : result.commands) identities.emplace(command.sourceBatchId, std::pair{command.instanceRevision, command.instances.data()});
    Require(scene.UpdateMeshTransform(2050U, TranslationMatrix(0.3F, 0.0F, 0.0F)), "Dynamic mesh did not update");
    build();
    Require(result.stats.meshCommandReuseCount == 3U, "A moving page rebuilt unchanged pages");
    for (const auto& command : result.commands) {
        const bool moving = std::ranges::any_of(command.instances, [](const auto& instance) { return instance.entityId == 2050U; });
        const auto old = identities.at(command.sourceBatchId);
        Require(moving ? command.instanceRevision != old.first :
            command.instanceRevision == old.first && command.instances.data() == old.second,
            "Batch retention did not preserve unchanged GPU identity and owned storage");
    }
    Require(scene.RemoveMesh(2U), "Mesh deletion failed"); build();
    Require(result.stats.meshCommandReuseCount == 3U, "Mesh deletion invalidated unrelated pages");
    proxy.entityId = 2U; static_cast<void>(scene.UpsertMesh(proxy)); build();
    Require(result.stats.meshCommandReuseCount == 3U, "Mesh insertion invalidated unrelated pages");
    ++key.resourceRevision; build(); Require(result.stats.meshCommandReuseCount == 0U, "Resource reload retained old commands");
    ++key.bindingRevision; build(); Require(result.stats.meshCommandReuseCount == 0U, "Binding reload retained old commands");
    ++result.detailSwitchHistoryRevision; build(); Require(result.stats.meshCommandReuseCount == 0U, "LOD history retained old commands");
    key.budget.maxVisibleInstances = 2000U; build(); build();
    key.budget.maxDrawCommands = 1U; build(); build();
    key.budget = {};
    auto camera = PerspectiveCamera();
    build(MeshPassType::BaseOpaque, &camera); build(MeshPassType::BaseOpaque, &camera);
    camera.view[12] = 20.0F; build(MeshPassType::BaseOpaque, &camera);
    Require(result.stats.meshCommandReuseCount == 0U, "Camera movement retained old visibility");
    camera.cullingMask = 0U; build(MeshPassType::BaseOpaque, &camera);
    camera.cullingMask = UINT32_MAX; build(MeshPassType::BaseOpaque, &camera);
    for (const auto pass : {MeshPassType::Depth, MeshPassType::ShadowDepth, MeshPassType::MotionVectors, MeshPassType::GBuffer}) {
        build(pass); build(pass);
        Require(result.stats.meshCommandReuseCount == 4U, "Opaque pass did not retain its valid pages");
    }
    proxy.entityId = 2050U; proxy.currentSkinningPalette = {.frame = 1U, .matrixCount = 1U};
    static_cast<void>(scene.UpsertMesh(proxy));
    build(); build();
    Require(result.stats.meshCommandReuseCount == 3U, "Skinning prevented retention of independent static pages");
    mesh.gpuCullingEnabled = true; ++key.resourceRevision;
    build(); build();
    Require(result.stats.meshCommandReuseCount == 3U && result.stats.gpuDrivenInputInstanceCount == 3072U,
        "Batch retention skipped CPU validation records or rebuilt independent pages");
}

void RunSceneMeshCommandReuseInvalidationTest() {
    RenderMeshResource mesh{};
    std::vector<MeshDrawCommand> commands(2U);
    for (auto& command : commands) command.meshResource = &mesh;
    SceneMeshCommandReuseKey key{.sceneRevision = 1U, .resourceRevision = 2U, .bindingRevision = 3U,
        .camera = PerspectiveCamera()};
    SceneMeshCommandReuseGate gate;
    gate.Commit(key, commands, {});
    Require(gate.Matches(key), "Valid static commands were not retained");
    Require(commands[0].instanceRevision != 0U && commands[0].instanceRevision != commands[1].instanceRevision,
        "Distinct commands must never share a GPU content identity");
    for (int change = 0; change < 7; ++change) {
        auto altered = key;
        switch (change) {
        case 0: ++altered.sceneRevision; break;
        case 1: ++altered.resourceRevision; break;
        case 2: ++altered.bindingRevision; break;
        case 3: altered.camera->view[12] += 1.0F; break;
        case 4: altered.camera->projection[0] *= 2.0F; break;
        case 5: altered.camera->cullingMask = 0U; break;
        case 6: ++altered.budget.maxVisibleInstances; break;
        }
        Require(!gate.Matches(altered), "Changed renderer input reused obsolete commands");
    }
    SceneRenderSubmitStats incomplete{};
    incomplete.droppedInstanceCount = 1U;
    gate.Commit(key, commands, incomplete);
    Require(!gate.Matches(key), "Incomplete commands must rebuild instead of remaining cached");
    mesh.lods.resize(2U);
    gate.Commit(key, commands, {});
    Require(gate.Matches(key), "Static multi-LOD commands should retain valid contents");
    auto advancedHistory = key;
    ++advancedHistory.detailSwitchHistoryRevision;
    Require(!gate.Matches(advancedHistory), "Advanced LOD history must invalidate an earlier shadow pass");
    RenderScene scene;
    auto revision = scene.MeshContentRevision();
    static_cast<void>(scene.UpsertMesh(MeshRenderProxyDesc{.entityId = 1U, .meshAssetId = 7U, .model = IdentityMatrix()}));
    Require(scene.MeshContentRevision() != revision, "Mesh insertion did not publish invalidation");
    revision = scene.MeshContentRevision();
    static_cast<void>(scene.DrawGroups());
    Require(scene.UpdateMeshTransform(1U, TranslationMatrix(1.0F, 0.0F, 0.0F)) && scene.MeshContentRevision() != revision,
        "In-place transform mutation did not invalidate retained commands");
    revision = scene.MeshContentRevision();
    Require(scene.RemoveMesh(1U) && scene.MeshContentRevision() != revision, "Mesh removal did not publish invalidation");
    RenderScene nextScene;
    Require(nextScene.MeshContentRevision() != scene.MeshContentRevision(), "A replacement scene reused another scene's identity");
}

// Only primitives that move on their own are drawn into velocity; the rest keep the camera motion that the
// full-screen pass derives from depth.
void RunMotionVectorPassDrawsOnlyMovingInstancesTest() {
    RenderScene scene;
    for (std::uint64_t id = 1U; id <= 3U; ++id) {
        static_cast<void>(scene.UpsertMesh(MeshRenderProxyDesc{
            .entityId = id, .meshAssetId = 7U, .model = TranslationMatrix(static_cast<float>(id), 0.0F, 5.0F)}));
    }
    static_cast<void>(scene.UpsertMesh(MeshRenderProxyDesc{.entityId = 4U, .meshAssetId = 7U,
        .model = TranslationMatrix(4.0F, 0.0F, 5.0F), .currentSkinningPalette = {.frame = 1U, .matrixCount = 1U}}));
    scene.BeginMotionFrame(1U);
    static_cast<void>(scene.DrawGroups());
    Require(scene.UpdateMeshTransform(2U, TranslationMatrix(2.5F, 0.0F, 5.0F)), "The moving mesh did not update");
    RenderMeshResource mesh{};
    mesh.indexCount = 3U;
    mesh.bounds = {.radius = 0.5F};
    RenderMaterialResource material{};
    const auto camera = PerspectiveCamera();
    const auto drawn = [&](MeshPassType pass, std::uint64_t motionFrame) {
        const auto groups = scene.DrawGroups();
        const auto result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{.pass = pass, .drawGroups = &groups,
            .resolvedMeshResource = &mesh, .resolvedMaterialResource = &material, .camera = &camera,
            .resourceValidation = MeshPipelineResourceValidation::Skip, .motionFrame = motionFrame});
        std::vector<std::uint64_t> ids;
        for (const auto& command : result.commands)
            for (const auto& instance : command.instances) ids.push_back(instance.entityId);
        std::ranges::sort(ids);
        return ids;
    };
    const std::vector<std::uint64_t> moving{2U, 4U};
    Require(drawn(MeshPassType::MotionVectors, scene.MotionFrame()) == moving,
        "The motion pass must draw the moved and the skinned mesh only");
    Require(drawn(MeshPassType::MotionVectors, 0U).size() == 4U, "Without a motion frame the motion pass draws every mesh");
    Require(drawn(MeshPassType::GBuffer, scene.MotionFrame()).size() == 4U, "Other passes must draw every mesh");
    scene.BeginMotionFrame(2U);
    Require(drawn(MeshPassType::MotionVectors, scene.MotionFrame()) == std::vector<std::uint64_t>{4U},
        "A mesh that stopped moving must leave the motion pass");
}

void RunMeshPassTypeNamesResolveTest() {
    Require(MeshPassTypeName(MeshPassType::Depth) == std::string_view("Depth"), "MeshPassTypeName did not resolve Depth");
    Require(MeshPassTypeName(MeshPassType::BaseOpaque) == std::string_view("BaseOpaque"), "MeshPassTypeName did not resolve BaseOpaque");
    Require(MeshPassTypeName(MeshPassType::GBuffer) == std::string_view("GBuffer"), "MeshPassTypeName did not resolve GBuffer");
    Require(MeshPassTypeName(MeshPassType::BaseTransparent) == std::string_view("BaseTransparent"), "MeshPassTypeName did not resolve BaseTransparent");
    Require(MeshPassTypeName(MeshPassType::ShadowDepth) == std::string_view("ShadowDepth"), "MeshPassTypeName did not resolve ShadowDepth");
    Require(MeshPassTypeName(MeshPassType::MotionVectors) == std::string_view("MotionVectors"), "MeshPassTypeName did not resolve MotionVectors");
    Require(MeshPassTypeName(MeshPassType::SelectionId) == std::string_view("SelectionId"), "MeshPassTypeName did not resolve SelectionId");
    Require(MeshPassTypeName(MeshPassType::EditorSelection) == std::string_view("EditorSelection"), "MeshPassTypeName did not resolve EditorSelection");
    Require(MeshPassTypeName(MeshPassType::Gizmo) == std::string_view("Gizmo"), "MeshPassTypeName did not resolve Gizmo");
}

void RunStaticMeshVertexLayoutIsDefinedTest() {
    const bgfx::VertexLayout layout = RenderStaticMeshVertexLayout();
    Require(layout.getStride() == sizeof(RenderStaticMeshVertex), "Static mesh vertex layout stride does not match RenderStaticMeshVertex");
}

void RunFramePassKindsMapToMeshPassesTest() {
    Require(MeshPassForRenderPassKind(RenderPassKind::OpaqueScene).value_or(MeshPassType::Depth) == MeshPassType::BaseOpaque, "OpaqueScene did not map to BaseOpaque mesh pass");
    Require(MeshPassForRenderPassKind(RenderPassKind::GBufferGeometry).value_or(MeshPassType::Depth) == MeshPassType::GBuffer, "GBufferGeometry did not map to GBuffer mesh pass");
    Require(MeshPassForRenderPassKind(RenderPassKind::TransparentScene).value_or(MeshPassType::Depth) == MeshPassType::BaseTransparent, "TransparentScene did not map to BaseTransparent mesh pass");
    Require(MeshPassForRenderPassKind(RenderPassKind::EditorSelectionMask).value_or(MeshPassType::Depth) == MeshPassType::SelectionId, "EditorSelectionMask did not map to SelectionId mesh pass");
    Require(MeshPassForRenderPassKind(RenderPassKind::PostProcessMotionVectors).value_or(MeshPassType::Depth) == MeshPassType::MotionVectors, "PostProcessMotionVectors did not map to MotionVectors mesh pass");
    Require(!MeshPassForRenderPassKind(RenderPassKind::PostProcessBloomPrefilter).has_value(), "Post-process pass unexpectedly mapped to a mesh pass");
    Require(!MeshPassForRenderPassKind(RenderPassKind::FinalComposite).has_value(), "FinalComposite unexpectedly mapped to a mesh pass");
}

void RunSceneMeshBatchBuilderCreatesStableViewsTest() {
    std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 11U,
            .materialAssetId = 3U,
        },
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .materialAssetId = 7U,
            .instances = {
                SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U, .materialAssetId = 7U },
                SceneRenderMeshInstance{ .entityId = 2U, .meshAssetId = 42U, .materialAssetId = 7U },
            },
        },
    };

    std::vector<SceneMeshBatch> batches;
    SceneMeshBatchBuilder::BuildInto(drawGroups, batches);

    Require(batches.size() == 1U, "SceneMeshBatchBuilder did not skip empty draw groups");
    Require(batches[0].meshAssetId == 42U && batches[0].materialAssetId == 7U, "SceneMeshBatchBuilder did not preserve batch keys");
    Require(batches[0].sourceDrawGroupIndex == 1U, "SceneMeshBatchBuilder did not preserve source draw group index");
    Require(batches[0].instances.size() == 2U, "SceneMeshBatchBuilder did not preserve batch instances");
    Require(batches[0].instances.data() == drawGroups[1].instances.data(), "SceneMeshBatchBuilder copied instances instead of creating a view");

    drawGroups[1].instances[0].entityId = 99U;
    Require(batches[0].instances[0].entityId == 99U, "SceneMeshBatch view did not reflect source instance storage");
}

void RunMeshPipelineBuildsFromSceneMeshBatchesTest() {
    const std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .materialAssetId = 7U,
            .instances = {
                SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U, .materialAssetId = 7U },
                SceneRenderMeshInstance{ .entityId = 2U, .meshAssetId = 42U, .materialAssetId = 7U },
            },
        },
    };
    const std::vector<SceneMeshBatch> batches = SceneMeshBatchBuilder::Build(drawGroups);

    const MeshPipelineBuildResult result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .meshBatches = &batches,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });

    Require(result.commands.size() == 1U, "MeshPipeline did not build commands from scene mesh batches");
    Require(result.commands[0].meshAssetId == 42U, "MeshPipeline batch path did not preserve mesh asset id");
    Require(result.commands[0].materialAssetId == 7U, "MeshPipeline batch path did not preserve material asset id");
    Require(result.commands[0].instances.size() == 2U, "MeshPipeline batch path did not preserve instances");

    MeshPipelineBuildResult reusableResult;
    MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .drawGroups = &drawGroups,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    }, reusableResult);
    Require(reusableResult.meshBatchScratch.empty(), "MeshPipeline exposed transient batch views after build");
    Require(reusableResult.meshBatchScratch.capacity() >= 1U, "MeshPipeline draw group adapter did not retain batch scratch capacity");
    Require(reusableResult.stats.meshCachedDrawCommandCount == 1U, "MeshPipeline did not populate cached draw command templates");
    Require(reusableResult.stats.meshDrawCommandCacheMissCount == 1U, "MeshPipeline did not report cached draw command miss on first build");
    Require(reusableResult.stats.meshDrawCommandCacheBuildCount == 1U, "MeshPipeline did not report cached draw command build on first build");

    MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .meshBatches = &batches,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    }, reusableResult);
    Require(reusableResult.meshBatchScratch.empty(), "MeshPipeline retained stale batch scratch when external batches were provided");
    Require(!reusableResult.commandLookupScratch.empty(), "MeshPipeline batch path did not use command lookup scratch");
    Require(reusableResult.stats.meshCachedDrawCommandCount == 1U, "MeshPipeline did not retain cached draw command templates");
    Require(reusableResult.stats.meshDrawCommandCacheHitCount == 1U, "MeshPipeline did not reuse cached draw command template");
    Require(reusableResult.stats.meshDrawCommandCacheMissCount == 0U, "MeshPipeline rebuilt cached draw command template unnecessarily");

    MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{
        .pass = MeshPassType::Depth,
        .meshBatches = &batches,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    }, reusableResult);
    Require(reusableResult.stats.meshCachedDrawCommandCount == 2U, "MeshPipeline did not keep cached draw command templates for multiple passes");
    Require(reusableResult.stats.meshDrawCommandCacheMissCount == 1U, "MeshPipeline did not build a depth-pass cached draw command template");
    Require(reusableResult.stats.meshDrawCommandCachePruneCount == 0U, "MeshPipeline pruned another pass while building depth commands");

    MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .meshBatches = &batches,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    }, reusableResult);
    Require(reusableResult.stats.meshCachedDrawCommandCount == 2U, "MeshPipeline did not retain cached draw command templates across pass switches");
    Require(reusableResult.stats.meshDrawCommandCacheHitCount == 1U, "MeshPipeline did not reuse a cached draw command after another pass was built");
    Require(reusableResult.stats.meshDrawCommandCacheMissCount == 0U, "MeshPipeline rebuilt a cached draw command after another pass was built");

    MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{}, reusableResult);
    Require(reusableResult.commands.empty(), "MeshPipeline retained commands after an empty build input");
    Require(reusableResult.commandLookupScratch.empty(), "MeshPipeline retained stale command lookup entries after an empty build input");

    const std::vector<SceneMeshBatch> emptyBatches;
    MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .meshBatches = &emptyBatches,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    }, reusableResult);
    Require(reusableResult.stats.meshCachedDrawCommandCount == 1U, "MeshPipeline pruned cached draw commands from another pass after empty batch input");
    Require(reusableResult.stats.meshDrawCommandCachePruneCount == 1U, "MeshPipeline did not report pruning stale cached draw commands for the current pass");

    MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{
        .pass = MeshPassType::Depth,
        .meshBatches = &emptyBatches,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    }, reusableResult);
    Require(reusableResult.stats.meshCachedDrawCommandCount == 0U, "MeshPipeline retained cached draw commands after pruning all pass caches");
    Require(reusableResult.stats.meshDrawCommandCachePruneCount == 1U, "MeshPipeline did not report pruning the stale cached depth command");

    RenderMeshResource versionedMesh{};
    versionedMesh.indexCount = 3U;
    versionedMesh.version = 1U;
    RenderMaterialResource versionedMaterial{};
    versionedMaterial.version = 1U;
    MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .meshBatches = &batches,
        .resolvedMeshResource = &versionedMesh,
        .resolvedMaterialResource = &versionedMaterial,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    }, reusableResult);
    Require(reusableResult.stats.meshCachedDrawCommandCount == 1U, "MeshPipeline did not retain a versioned cached draw command");
    Require(reusableResult.stats.meshDrawCommandCacheMissCount == 1U, "MeshPipeline did not build a versioned cached draw command");
    Require(reusableResult.stats.meshDrawCommandCacheBuildCount == 1U, "MeshPipeline did not report versioned cached draw command build");

    RenderMeshResource relocatedMesh{};
    relocatedMesh.indexCount = 3U;
    relocatedMesh.version = 1U;
    RenderMaterialResource relocatedMaterial{};
    relocatedMaterial.version = 1U;
    MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .meshBatches = &batches,
        .resolvedMeshResource = &relocatedMesh,
        .resolvedMaterialResource = &relocatedMaterial,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    }, reusableResult);
    Require(reusableResult.stats.meshDrawCommandCacheHitCount == 1U, "MeshPipeline did not reuse versioned cached draw command after registry storage moved");
    Require(!reusableResult.commands.empty() && reusableResult.commands[0].meshResource == &relocatedMesh, "MeshPipeline cache hit kept a stale mesh resource pointer");
    Require(!reusableResult.commands.empty() && reusableResult.commands[0].materialResource == &relocatedMaterial, "MeshPipeline cache hit kept a stale material resource pointer");

    versionedMaterial.version = 2U;
    MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .meshBatches = &batches,
        .resolvedMeshResource = &versionedMesh,
        .resolvedMaterialResource = &versionedMaterial,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    }, reusableResult);
    Require(reusableResult.stats.meshCachedDrawCommandCount == 1U, "MeshPipeline did not retain the rebuilt versioned cached draw command");
    Require(reusableResult.stats.meshDrawCommandCacheHitCount == 0U, "MeshPipeline reused cached draw command after material version changed");
    Require(reusableResult.stats.meshDrawCommandCacheMissCount == 1U, "MeshPipeline did not rebuild cached draw command after material version changed");
    Require(reusableResult.stats.meshDrawCommandCacheBuildCount == 1U, "MeshPipeline did not report rebuilt cached draw command after material version changed");
    Require(reusableResult.stats.meshDrawCommandCachePruneCount == 1U, "MeshPipeline did not prune stale cached draw command after material version changed");

    MeshPipelineBuildResult textureDependencyResult;
    versionedMaterial.version = 3U;
    versionedMaterial.albedoTexture = RenderTextureHandle{ 0x0000'0001'0000'0001ULL };
    MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .meshBatches = &batches,
        .resolvedMeshResource = &versionedMesh,
        .resolvedMaterialResource = &versionedMaterial,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    }, textureDependencyResult);
    Require(textureDependencyResult.stats.meshDrawCommandCacheMissCount == 1U, "MeshPipeline did not build a texture-dependent cached draw command");

    MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .meshBatches = &batches,
        .resolvedMeshResource = &versionedMesh,
        .resolvedMaterialResource = &versionedMaterial,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    }, textureDependencyResult);
    Require(textureDependencyResult.stats.meshDrawCommandCacheHitCount == 1U, "MeshPipeline did not reuse an unchanged texture-dependent cached draw command");
    Require(textureDependencyResult.stats.meshDrawCommandCacheMissCount == 0U, "MeshPipeline rebuilt an unchanged texture-dependent cached draw command");

    versionedMaterial.albedoTexture = RenderTextureHandle{ 0x0000'0001'0000'0002ULL };
    MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .meshBatches = &batches,
        .resolvedMeshResource = &versionedMesh,
        .resolvedMaterialResource = &versionedMaterial,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    }, textureDependencyResult);
    Require(textureDependencyResult.stats.meshDrawCommandCacheHitCount == 0U, "MeshPipeline reused cached draw command after material texture dependency changed");
    Require(textureDependencyResult.stats.meshDrawCommandCacheMissCount == 1U, "MeshPipeline did not rebuild cached draw command after material texture dependency changed");
    Require(textureDependencyResult.stats.meshDrawCommandCacheBuildCount == 1U, "MeshPipeline did not report cached draw command rebuild after material texture dependency changed");
    Require(textureDependencyResult.stats.meshDrawCommandCachePruneCount == 1U, "MeshPipeline did not prune stale cached draw command after material texture dependency changed");

    MeshPipelineBuildResult textureBindingDependencyResult;
    SceneRenderResourceMap textureResourceMap;
    versionedMaterial.albedoTexture = {};
    versionedMaterial.albedoTextureAssetId = 512U;
    textureResourceMap.BindTexture(512U, RenderTextureColorSpace::Srgb, RenderTextureHandle{ 0x0000'0001'0000'0010ULL });
    MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .meshBatches = &batches,
        .resourceMap = &textureResourceMap,
        .resolvedMeshResource = &versionedMesh,
        .resolvedMaterialResource = &versionedMaterial,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    }, textureBindingDependencyResult);
    Require(textureBindingDependencyResult.stats.meshDrawCommandCacheMissCount == 1U, "MeshPipeline did not build a texture-binding-dependent cached draw command");

    MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .meshBatches = &batches,
        .resourceMap = &textureResourceMap,
        .resolvedMeshResource = &versionedMesh,
        .resolvedMaterialResource = &versionedMaterial,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    }, textureBindingDependencyResult);
    Require(textureBindingDependencyResult.stats.meshDrawCommandCacheHitCount == 1U, "MeshPipeline did not reuse an unchanged texture-binding-dependent cached draw command");
    Require(textureBindingDependencyResult.stats.meshDrawCommandCacheMissCount == 0U, "MeshPipeline rebuilt an unchanged texture-binding-dependent cached draw command");

    textureResourceMap.BindTexture(512U, RenderTextureColorSpace::Srgb, RenderTextureHandle{ 0x0000'0001'0000'0011ULL });
    MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .meshBatches = &batches,
        .resourceMap = &textureResourceMap,
        .resolvedMeshResource = &versionedMesh,
        .resolvedMaterialResource = &versionedMaterial,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    }, textureBindingDependencyResult);
    Require(textureBindingDependencyResult.stats.meshDrawCommandCacheHitCount == 0U, "MeshPipeline reused cached draw command after texture binding changed");
    Require(textureBindingDependencyResult.stats.meshDrawCommandCacheMissCount == 1U, "MeshPipeline did not rebuild cached draw command after texture binding changed");
    Require(textureBindingDependencyResult.stats.meshDrawCommandCachePruneCount == 1U, "MeshPipeline did not prune stale cached draw command after texture binding changed");

    MeshPipelineBuildResult selectionDependencyResult;
    const std::array<std::uint64_t, 2U> selectedIds{1U, 2U};
    versionedMaterial.albedoTextureAssetId = 0U;
    versionedMaterial.albedoTexture = RenderTextureHandle{ 0x0000'0001'0000'0020ULL };
    MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{
        .pass = MeshPassType::SelectionId,
        .meshBatches = &batches,
        .resolvedMeshResource = &versionedMesh,
        .resolvedMaterialResource = &versionedMaterial,
        .selectedEntityIds = selectedIds,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    }, selectionDependencyResult);
    Require(selectionDependencyResult.stats.meshDrawCommandCacheMissCount == 1U, "MeshPipeline did not build a selection cached draw command");

    versionedMaterial.albedoTexture = RenderTextureHandle{ 0x0000'0001'0000'0021ULL };
    MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{
        .pass = MeshPassType::SelectionId,
        .meshBatches = &batches,
        .resolvedMeshResource = &versionedMesh,
        .resolvedMaterialResource = &versionedMaterial,
        .selectedEntityIds = selectedIds,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    }, selectionDependencyResult);
    Require(selectionDependencyResult.stats.meshDrawCommandCacheHitCount == 1U, "MeshPipeline rebuilt selection cached draw command after irrelevant texture dependency changed");
    Require(selectionDependencyResult.stats.meshDrawCommandCacheMissCount == 0U, "MeshPipeline reported a selection cache miss for an irrelevant texture dependency change");

    MeshPipelineBuildResult shadowDependencyResult;
    versionedMaterial.alphaMode = RenderMaterialAlphaMode::Mask;
    versionedMaterial.albedoTexture = RenderTextureHandle{ 0x0000'0001'0000'0030ULL };
    MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{
        .pass = MeshPassType::ShadowDepth,
        .meshBatches = &batches,
        .resolvedMeshResource = &versionedMesh,
        .resolvedMaterialResource = &versionedMaterial,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    }, shadowDependencyResult);
    Require(shadowDependencyResult.stats.meshDrawCommandCacheMissCount == 1U, "MeshPipeline did not build a shadow cached draw command");

    versionedMaterial.albedoTexture = RenderTextureHandle{ 0x0000'0001'0000'0031ULL };
    MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{
        .pass = MeshPassType::ShadowDepth,
        .meshBatches = &batches,
        .resolvedMeshResource = &versionedMesh,
        .resolvedMaterialResource = &versionedMaterial,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    }, shadowDependencyResult);
    Require(shadowDependencyResult.stats.meshDrawCommandCacheHitCount == 0U, "MeshPipeline reused shadow cached draw command after alpha-mask texture dependency changed");
    Require(shadowDependencyResult.stats.meshDrawCommandCacheMissCount == 1U, "MeshPipeline did not rebuild shadow cached draw command after alpha-mask texture dependency changed");
    Require(shadowDependencyResult.stats.meshDrawCommandCachePruneCount == 1U, "MeshPipeline did not prune stale shadow cached draw command after alpha-mask texture dependency changed");
}

void RunMeshPipelineSplitsSkinnedPalettesAndPreservesMotionHistoryTest() {
    const RenderSkinningPaletteHandle firstCurrent{ .frame = 10U, .firstMatrix = 0U, .matrixCount = 2U, .bufferIndex = 0U };
    const RenderSkinningPaletteHandle firstPrevious{ .frame = 9U, .firstMatrix = 0U, .matrixCount = 2U, .bufferIndex = 1U };
    const RenderSkinningPaletteHandle secondCurrent{ .frame = 10U, .firstMatrix = 2U, .matrixCount = 2U, .bufferIndex = 0U };
    const RenderSkinningPaletteHandle secondPrevious{ .frame = 9U, .firstMatrix = 2U, .matrixCount = 2U, .bufferIndex = 1U };
    const std::vector<SceneRenderDrawGroup> groups{ SceneRenderDrawGroup{
        .meshAssetId = 42U, .materialAssetId = 7U,
        .instances = {
            SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U, .materialAssetId = 7U, .currentSkinningPalette = firstCurrent, .previousSkinningPalette = firstPrevious },
            SceneRenderMeshInstance{ .entityId = 2U, .meshAssetId = 42U, .materialAssetId = 7U, .currentSkinningPalette = secondCurrent, .previousSkinningPalette = secondPrevious },
        },
    } };
    const std::vector<SceneMeshBatch> batches = SceneMeshBatchBuilder::Build(groups);
    const MeshPipelineBuildResult result = MeshPipelineProcessor::Build({
        .pass = MeshPassType::BaseOpaque, .meshBatches = &batches,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });
    Require(result.commands.size() == 2U &&
            result.commands[0].instances.size() == 1U &&
            result.commands[1].instances.size() == 1U &&
            ((result.commands[0].currentSkinningPalette == firstCurrent && result.commands[0].previousSkinningPalette == firstPrevious && result.commands[1].currentSkinningPalette == secondCurrent && result.commands[1].previousSkinningPalette == secondPrevious) ||
             (result.commands[1].currentSkinningPalette == firstCurrent && result.commands[1].previousSkinningPalette == firstPrevious && result.commands[0].currentSkinningPalette == secondCurrent && result.commands[0].previousSkinningPalette == secondPrevious)),
        "Mesh pipeline merged skinned instances with different current or previous palettes");
}

void RunMeshPipelineReportsMissingMeshBindingPerInstanceTest() {
    const std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .materialAssetId = 7U,
            .instances = {
                SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U, .materialAssetId = 7U },
                SceneRenderMeshInstance{ .entityId = 2U, .meshAssetId = 42U, .materialAssetId = 7U },
            },
        },
    };
    RenderResourceRegistry registry;
    SceneRenderResourceMap resourceMap;
    SceneRenderDiagnostics diagnostics;

    const MeshPipelineBuildResult result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .drawGroups = &drawGroups,
        .resources = &registry,
        .resourceMap = &resourceMap,
        .diagnostics = &diagnostics,
    });

    Require(result.commands.empty(), "MeshPipeline emitted a command without a mesh binding");
    Require(result.stats.visibleMeshCount == 2U, "MeshPipeline did not count visible mesh instances");
    Require(result.stats.visibleDrawGroupCount == 1U, "MeshPipeline did not count visible draw groups");
    Require(result.stats.missingMeshBindingCount == 2U, "MeshPipeline did not count one missing mesh binding per instance");
    Require(diagnostics.events.size() == 2U, "MeshPipeline did not emit diagnostics per missing mesh instance");
    Require(diagnostics.events[0].kind == SceneRenderDiagnosticKind::MissingMeshBinding, "MeshPipeline emitted the wrong diagnostic kind");
    Require(diagnostics.events[1].entityId == 2U, "MeshPipeline diagnostic did not preserve entity id");
}

void RunMeshPipelineReportsMissingOcclusionTextureBindingTest() {
    RenderMaterialResource material{};
    material.occlusionTextureAssetId = 909U;
    RenderResourceRegistry registry;
    SceneRenderResourceMap resourceMap;
    SceneRenderSubmitStats stats;
    SceneRenderDiagnostics diagnostics;
    const SceneRenderMeshInstance instance{ .entityId = 1U, .meshAssetId = 42U, .materialAssetId = 7U };

    MeshPipelineResourceResolver::ValidateMaterialTextureOrFallback(instance, 7U, &material, registry, resourceMap, stats, &diagnostics);

    Require(stats.missingTextureBindingCount == 1U, "MeshPipeline did not count missing occlusion texture binding");
    Require(diagnostics.events.size() == 1U, "MeshPipeline did not emit missing occlusion texture diagnostic");
    Require(diagnostics.events[0].kind == SceneRenderDiagnosticKind::MissingTextureBinding, "MeshPipeline emitted the wrong occlusion texture diagnostic kind");
    Require(diagnostics.events[0].materialAssetId == 7U, "MeshPipeline occlusion diagnostic lost material asset id");
}

void RunMeshPipelineCanBuildPassCommandsWithoutResourceValidationTest() {
    const std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .materialAssetId = 7U,
            .instances = {
                SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U, .materialAssetId = 7U },
                SceneRenderMeshInstance{ .entityId = 2U, .meshAssetId = 42U, .materialAssetId = 7U },
            },
        },
    };

    MeshPipelineBuildResult result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .drawGroups = &drawGroups,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });
    Require(result.commands.size() == 1U, "MeshPipeline did not build a command when validation was skipped");
    Require(result.commands[0].pass == MeshPassType::BaseOpaque, "MeshPipeline command stored the wrong pass");
    Require(result.commands[0].instances.size() == 2U, "MeshPipeline command did not preserve instances");

    MeshPipelineProcessor::CountCommandsAsSubmitted(result.stats, result.commands);
    Require(result.stats.submittedMeshCount == 2U, "MeshPipeline did not count submitted meshes from commands");
    Require(result.stats.submittedDrawCallCount == 1U, "MeshPipeline did not count submitted draw calls from commands");
    Require(result.stats.meshPipelineCommandCount == 1U, "MeshPipeline did not expose command count");
    Require(result.stats.meshPipelineCommandCapacity >= result.stats.meshPipelineCommandCount, "MeshPipeline did not expose command vector capacity");
    Require(result.stats.meshPipelineSortKeyCount == 1U, "MeshPipeline did not expose sort key count");
    Require(result.stats.meshPipelineScratchInstanceCapacity >= 2U, "MeshPipeline did not expose scratch instance capacity");
}

void RunMeshPipelineBuildIntoReusesCommandInstanceCapacityTest() {
    std::vector<SceneRenderMeshInstance> manyInstances;
    manyInstances.reserve(8U);
    for (std::uint64_t entityId = 1U; entityId <= 8U; ++entityId) {
        manyInstances.push_back(SceneRenderMeshInstance{ .entityId = entityId, .meshAssetId = 42U });
    }
    std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .instances = manyInstances,
        },
    };
    MeshPipelineBuildResult reusableResult;

    MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .drawGroups = &drawGroups,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    }, reusableResult);
    Require(reusableResult.commands.size() == 1U, "MeshPipeline BuildInto did not emit initial command");
    const std::size_t initialCapacity = reusableResult.commands[0].instances.capacity();
    Require(initialCapacity >= 8U, "MeshPipeline BuildInto did not reserve the initial instance capacity");

    drawGroups[0].instances.resize(2U);
    MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .drawGroups = &drawGroups,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    }, reusableResult);

    Require(reusableResult.commands.size() == 1U, "MeshPipeline BuildInto changed stable command count");
    Require(reusableResult.commands[0].instances.size() == 2U, "MeshPipeline BuildInto did not rebuild the smaller instance set");
    Require(reusableResult.commands[0].instances.capacity() >= initialCapacity, "MeshPipeline BuildInto released reusable instance capacity");
}

void RunMeshPipelineFiltersShadowCastingInstancesTest() {
    const std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .instances = {
                SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U, .castsShadow = true },
                SceneRenderMeshInstance{ .entityId = 2U, .meshAssetId = 42U, .castsShadow = false },
            },
        },
    };

    const MeshPipelineBuildResult result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::ShadowDepth,
        .drawGroups = &drawGroups,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });

    Require(result.commands.size() == 1U, "MeshPipeline shadow pass did not emit a command for shadow casters");
    Require(result.commands[0].instances.size() == 1U, "MeshPipeline shadow pass did not filter non-shadow-casting instances");
    Require(result.commands[0].instances[0].entityId == 1U, "MeshPipeline shadow pass kept the wrong instance");
}

void RunMeshPipelineFiltersSelectionInstancesTest() {
    const std::array<std::uint64_t, 1U> selected{2U};
    const std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .instances = {
                SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U },
                SceneRenderMeshInstance{ .entityId = 2U, .meshAssetId = 42U },
            },
        },
    };

    const MeshPipelineBuildResult emptySelection = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::SelectionId,
        .drawGroups = &drawGroups,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });
    Require(emptySelection.commands.empty(), "MeshPipeline selection pass rendered without selected entities");

    const MeshPipelineBuildResult result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::SelectionId,
        .drawGroups = &drawGroups,
        .selectedEntityIds = selected,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });

    Require(result.commands.size() == 1U, "MeshPipeline selection pass did not emit a command for selected entities");
    Require(result.commands[0].instances.size() == 1U, "MeshPipeline selection pass did not filter unselected instances");
    Require(result.commands[0].instances[0].entityId == 2U, "MeshPipeline selection pass kept the wrong entity");
}

// LIB-136: proves a camera's cullingMask actually excludes non-matching-layer instances
// from a real draw command build - not just that the fields exist and compile.
// MeshPipelinePassPolicy applies the check uniformly across every pass (opaque and shadow
// alike, per PassesCullingMask's own doc comment) - it is each SUBMITTER's responsibility to
// pass the right camera object in. The real shadow submission path
// (RendererShadowSubmitter/DirectionalShadowPassPlanner) always builds its OWN
// SceneRenderCamera for the light, which default-constructs with an unrestricted mask
// (SceneRenderCamera::cullingMask's own default member initializer) regardless of what the
// scene's viewing camera's mask is set to - so shadow casters are decoupled from the viewing
// camera's cullingMask in practice, without MeshPipelinePassPolicy needing a pass-specific
// exemption.
void RunMeshPipelineFiltersByCameraCullingMaskTest() {
    const std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .instances = {
                SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U, .castsShadow = true, .layer = 0x1U },
                SceneRenderMeshInstance{ .entityId = 2U, .meshAssetId = 42U, .castsShadow = true, .layer = 0x2U },
            },
        },
    };

    const SceneRenderCamera restrictedCamera{ .cullingMask = 0x2U };
    const MeshPipelineBuildResult opaqueResult = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .drawGroups = &drawGroups,
        .camera = &restrictedCamera,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });
    Require(opaqueResult.commands.size() == 1U, "MeshPipeline opaque pass did not emit a command for the culling-mask-matching layer");
    Require(opaqueResult.commands[0].instances.size() == 1U, "MeshPipeline opaque pass did not filter the non-matching layer instance");
    Require(opaqueResult.commands[0].instances[0].entityId == 2U, "MeshPipeline opaque pass kept the wrong (non-matching-layer) instance");

    // Same restricted mask, ShadowDepth pass this time - proves the filter applies
    // consistently (no per-pass exemption baked into MeshPipelinePassPolicy). A restricted
    // shadow-pass camera SHOULD filter exactly like an opaque one; production code stays
    // correct by never constructing the shadow submission's own camera with a restricted
    // mask (see the default-constructed camera assertion below).
    const MeshPipelineBuildResult restrictedShadowResult = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::ShadowDepth,
        .drawGroups = &drawGroups,
        .camera = &restrictedCamera,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });
    Require(restrictedShadowResult.commands.size() == 1U && restrictedShadowResult.commands[0].instances.size() == 1U && restrictedShadowResult.commands[0].instances[0].entityId == 2U,
        "MeshPipeline shadow pass did not apply an explicitly restricted cullingMask consistently with the opaque pass");

    const SceneRenderCamera unrestrictedCamera{};
    Require(unrestrictedCamera.cullingMask == 0xFFFFFFFFU, "SceneRenderCamera's own default cullingMask must be all-bits-set, so a default-constructed shadow-light camera never restricts casters");
    const MeshPipelineBuildResult unrestrictedShadowResult = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::ShadowDepth,
        .drawGroups = &drawGroups,
        .camera = &unrestrictedCamera,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });
    Require(unrestrictedShadowResult.commands.size() == 1U && unrestrictedShadowResult.commands[0].instances.size() == 2U,
        "MeshPipeline shadow pass with a default-constructed (all-bits) camera - matching real production shadow submission - must include every layer's casters");

    const MeshPipelineBuildResult allLayersResult = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .drawGroups = &drawGroups,
        .camera = &unrestrictedCamera,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });
    Require(allLayersResult.commands.size() == 1U && allLayersResult.commands[0].instances.size() == 2U,
        "MeshPipeline opaque pass with a default (all-bits) cullingMask must draw every layer, matching pre-LIB-136 behavior");
}

void RunMeshPipelineReportsShadowPassMissingResourcesOnlyForCastersTest() {
    const std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .instances = {
                SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U, .castsShadow = true },
                SceneRenderMeshInstance{ .entityId = 2U, .meshAssetId = 42U, .castsShadow = false },
            },
        },
    };
    RenderResourceRegistry registry;
    SceneRenderResourceMap resourceMap;
    SceneRenderDiagnostics diagnostics;

    const MeshPipelineBuildResult result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::ShadowDepth,
        .drawGroups = &drawGroups,
        .resources = &registry,
        .resourceMap = &resourceMap,
        .diagnostics = &diagnostics,
    });

    Require(result.commands.empty(), "MeshPipeline emitted a shadow command without a mesh binding");
    Require(result.stats.visibleMeshCount == 1U, "MeshPipeline shadow pass counted non-shadow-casting instances as visible");
    Require(result.stats.visibleDrawGroupCount == 1U, "MeshPipeline shadow pass did not count the casting draw group");
    Require(result.stats.missingMeshBindingCount == 1U, "MeshPipeline shadow pass reported missing mesh bindings for filtered instances");
    Require(diagnostics.events.size() == 1U, "MeshPipeline shadow pass emitted diagnostics for filtered instances");
    Require(diagnostics.events[0].entityId == 1U, "MeshPipeline shadow pass diagnostic kept the wrong entity");
}

void RunSceneRendererValidatesExplicitMeshPassTest() {
    RenderScene renderScene;
    static_cast<void>(renderScene.UpsertMesh(MeshRenderProxyDesc{
        .entityId = 1U,
        .meshAssetId = 42U,
        .castsShadow = true,
    }));
    static_cast<void>(renderScene.UpsertMesh(MeshRenderProxyDesc{
        .entityId = 2U,
        .meshAssetId = 42U,
        .castsShadow = false,
    }));

    SceneRenderer renderer;
    const SceneRenderSubmitStats stats = renderer.ValidateSceneResources(renderScene, MeshPassType::ShadowDepth);
    Require(stats.visibleMeshCount == 1U, "SceneRenderer explicit shadow pass counted filtered instances");
    Require(stats.missingMeshBindingCount == 1U, "SceneRenderer explicit shadow pass did not use mesh pass validation");
}

void RunMeshPipelineBuildsCommandsPerSectionAndMaterialSlotTest() {
    const std::vector<RenderMeshSection> sections{
        RenderMeshSection{
            .indexStart = 0U,
            .indexCount = 6U,
            .vertexStart = 0U,
            .vertexCount = 8U,
            .materialSlot = 0U,
            .bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 1.0F },
        },
        RenderMeshSection{
            .indexStart = 6U,
            .indexCount = 12U,
            .vertexStart = 8U,
            .vertexCount = 16U,
            .materialSlot = 1U,
            .bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 1.0F },
        },
    };
    const std::vector<RenderMaterialSlot> materialSlots{
        RenderMaterialSlot{ .defaultMaterialAssetId = 101U },
        RenderMaterialSlot{ .defaultMaterialAssetId = 102U },
    };
    RenderMeshResource mesh{};
    mesh.vertexCount = 24U;
    mesh.indexCount = 18U;
    mesh.sections = sections;
    mesh.materialSlots = materialSlots;
    mesh.bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 1.0F };

    const std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .materialAssetId = 999U,
            .instances = {
                SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U, .model = IdentityMatrix() },
                SceneRenderMeshInstance{ .entityId = 2U, .meshAssetId = 42U, .model = IdentityMatrix() },
            },
        },
    };

    const MeshPipelineBuildResult result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .drawGroups = &drawGroups,
        .resolvedMeshResource = &mesh,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });

    Require(result.commands.size() == 2U, "MeshPipeline did not emit one command per mesh section");
    Require(result.commands[0].materialAssetId == 999U, "MeshPipeline did not prefer the Mesh Renderer main material over section slot 0 default material");
    Require(result.commands[0].indexStart == 0U && result.commands[0].indexCount == 6U, "MeshPipeline section 0 did not preserve index range");
    Require(result.commands[0].vertexStart == 0U && result.commands[0].vertexCount == 8U,
        "MeshPipeline section 0 did not preserve vertex range");
    Require(result.commands[1].materialAssetId == 999U, "MeshPipeline did not prefer the Mesh Renderer main material over section slot 1 default material");
    Require(result.commands[1].indexStart == 6U && result.commands[1].indexCount == 12U, "MeshPipeline section 1 did not preserve index range");
    Require(result.commands[1].vertexStart == 8U && result.commands[1].vertexCount == 16U,
        "MeshPipeline section 1 did not preserve its rebased vertex range");
    Require(result.commands[0].instances.size() == 2U && result.commands[1].instances.size() == 2U, "MeshPipeline section commands did not preserve instancing");
}

void RunMeshPipelineUsesMeshDefaultMaterialWhenMainMaterialIsEmptyTest() {
    RenderMeshResource mesh{};
    mesh.indexCount = 6U;
    mesh.sections = {
        RenderMeshSection{
            .indexStart = 0U,
            .indexCount = 6U,
            .materialSlot = 0U,
            .bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 1.0F },
        },
    };
    mesh.materialSlots = {
        RenderMaterialSlot{ .defaultMaterialAssetId = 101U },
    };
    mesh.bounds = mesh.sections[0].bounds;

    const std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .materialAssetId = 0U,
            .instances = {
                SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U, .model = IdentityMatrix() },
            },
        },
    };

    const MeshPipelineBuildResult result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .drawGroups = &drawGroups,
        .resolvedMeshResource = &mesh,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });

    Require(result.commands.size() == 1U, "MeshPipeline did not emit a command for a mesh default material");
    Require(result.commands[0].materialAssetId == 101U, "MeshPipeline did not use the mesh default material when Mesh Renderer material is empty");
}

void RunMeshPipelineSplitsSectionCommandsByMaterialSlotOverrideTest() {
    RenderMeshResource mesh{};
    mesh.indexCount = 3U;
    mesh.sections = {
        RenderMeshSection{
            .indexStart = 0U,
            .indexCount = 3U,
            .materialSlot = 1U,
            .bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 1.0F },
        },
    };
    mesh.materialSlots = {
        RenderMaterialSlot{},
        RenderMaterialSlot{ .defaultMaterialAssetId = 101U },
    };
    mesh.bounds = mesh.sections[0].bounds;

    SceneRenderMeshInstance overridden{
        .entityId = 2U,
        .meshAssetId = 42U,
        .materialSlotAssetIds = { 0U, 201U },
        .materialSlotOverrideCount = 2U,
        .model = IdentityMatrix(),
    };
    const std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .materialAssetId = 999U,
            .instances = {
                SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U, .model = IdentityMatrix() },
                overridden,
            },
        },
    };

    const MeshPipelineBuildResult result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .drawGroups = &drawGroups,
        .resolvedMeshResource = &mesh,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });

    Require(result.commands.size() == 2U, "MeshPipeline did not split section commands by material slot override");
    const MeshDrawCommand* mainMaterialCommand = nullptr;
    const MeshDrawCommand* overrideMaterialCommand = nullptr;
    for (const MeshDrawCommand& command : result.commands) {
        if (command.materialAssetId == 999U) {
            mainMaterialCommand = &command;
        } else if (command.materialAssetId == 201U) {
            overrideMaterialCommand = &command;
        }
    }
    Require(mainMaterialCommand != nullptr, "MeshPipeline did not use the Mesh Renderer main material when no slot override was present");
    Require(mainMaterialCommand->instances.size() == 1U && mainMaterialCommand->instances[0].entityId == 1U, "MeshPipeline put the wrong instance in the main material command");
    Require(overrideMaterialCommand != nullptr, "MeshPipeline did not use the instance material slot override");
    Require(overrideMaterialCommand->instances.size() == 1U && overrideMaterialCommand->instances[0].entityId == 2U, "MeshPipeline put the wrong instance in the override material command");
    Require(result.stats.meshCommandLookupCapacity > 0U, "MeshPipeline did not expose command lookup scratch capacity");
}

void RunMeshPipelineDrawBudgetDropsOverflowCommandsTest() {
    RenderMeshResource mesh{};
    mesh.indexCount = 3U;
    mesh.sections = {
        RenderMeshSection{
            .indexStart = 0U,
            .indexCount = 3U,
            .materialSlot = 1U,
            .bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 1.0F },
        },
    };
    mesh.materialSlots = {
        RenderMaterialSlot{},
        RenderMaterialSlot{ .defaultMaterialAssetId = 101U },
    };
    mesh.bounds = mesh.sections[0].bounds;
    SceneRenderDiagnostics diagnostics;

    const std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .instances = {
                SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U, .model = IdentityMatrix() },
                SceneRenderMeshInstance{ .entityId = 2U, .meshAssetId = 42U, .materialSlotAssetIds = { 0U, 201U }, .materialSlotOverrideCount = 2U, .model = IdentityMatrix() },
            },
        },
    };

    const MeshPipelineBuildResult result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .drawGroups = &drawGroups,
        .resolvedMeshResource = &mesh,
        .diagnostics = &diagnostics,
        .maxDrawCommands = 1U,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });

    Require(result.commands.size() == 1U, "MeshPipeline draw budget did not cap command count");
    Require(result.commands[0].materialAssetId == 101U, "MeshPipeline draw budget dropped the wrong command");
    Require(result.stats.droppedInstanceCount == 1U, "MeshPipeline draw budget did not count dropped instances");
    Require(diagnostics.events.size() == 1U && diagnostics.events[0].kind == SceneRenderDiagnosticKind::DroppedInstances, "MeshPipeline draw budget did not emit a drop diagnostic");
}

void RunMeshPipelineSortsCommandsBySortKeyTest() {
    const std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 90U,
            .materialAssetId = 9U,
            .instances = { SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 90U, .materialAssetId = 9U } },
        },
        SceneRenderDrawGroup{
            .meshAssetId = 80U,
            .materialAssetId = 7U,
            .instances = { SceneRenderMeshInstance{ .entityId = 2U, .meshAssetId = 80U, .materialAssetId = 7U } },
        },
    };

    const MeshPipelineBuildResult result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .drawGroups = &drawGroups,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });

    Require(result.commands.size() == 2U, "MeshPipeline sort test did not build two commands");
    Require(result.commands[0].materialAssetId == 7U, "MeshPipeline did not sort commands by material key");
    Require(result.commands[0].sortKey < result.commands[1].sortKey, "MeshPipeline sort keys are not monotonic");
}

void RunMeshPipelineRoutesMaterialAlphaModesToPassesTest() {
    RenderMeshResource mesh{};
    mesh.indexCount = 3U;
    mesh.bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 1.0F };
    mesh.sections = {
        RenderMeshSection{
            .indexStart = 0U,
            .indexCount = 3U,
            .bounds = mesh.bounds,
        },
    };
    const std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .materialAssetId = 7U,
            .instances = { SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U, .materialAssetId = 7U, .model = IdentityMatrix() } },
        },
    };
    RenderMaterialResource masked{};
    masked.alphaMode = RenderMaterialAlphaMode::Mask;
    masked.baseColor[3] = 0.25F;
    RenderMaterialResource blended{};
    blended.alphaMode = RenderMaterialAlphaMode::Blend;

    const MeshPipelineBuildResult maskedOpaque = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .drawGroups = &drawGroups,
        .resolvedMeshResource = &mesh,
        .resolvedMaterialResource = &masked,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });
    Require(maskedOpaque.commands.size() == 1U, "MeshPipeline did not keep masked materials in the opaque pass");
    const MeshPipelineBuildResult maskedGBuffer = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::GBuffer,
        .drawGroups = &drawGroups,
        .resolvedMeshResource = &mesh,
        .resolvedMaterialResource = &masked,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });
    Require(maskedGBuffer.commands.size() == 1U, "MeshPipeline did not keep masked materials in the GBuffer pass");

    SceneRenderDiagnostics blendedDiagnostics;
    const MeshPipelineBuildResult blendedOpaque = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .drawGroups = &drawGroups,
        .resolvedMeshResource = &mesh,
        .resolvedMaterialResource = &blended,
        .diagnostics = &blendedDiagnostics,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });
    Require(blendedOpaque.commands.empty(), "KBMAT-MAT80: MeshPipeline kept blended materials in the opaque pass");
    // MAT-80: routing a blended material out of the opaque pass is correct (it renders in transparent),
    // so it must NOT raise an unsupported-alpha-blend diagnostic anymore.
    Require(blendedDiagnostics.events.empty(), "KBMAT-MAT80: blended material excluded from opaque must not emit an unsupported diagnostic");
    const MeshPipelineBuildResult blendedGBuffer = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::GBuffer,
        .drawGroups = &drawGroups,
        .resolvedMeshResource = &mesh,
        .resolvedMaterialResource = &blended,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });
    Require(blendedGBuffer.commands.empty(), "MeshPipeline kept blended materials in the GBuffer pass");

    const MeshPipelineBuildResult blendedTransparent = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseTransparent,
        .drawGroups = &drawGroups,
        .resolvedMeshResource = &mesh,
        .resolvedMaterialResource = &blended,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });
    Require(blendedTransparent.commands.size() == 1U, "KBMAT-MAT80: MeshPipeline must route blended materials to the transparent pass");
    Require((MeshPipelinePassPolicy::State(MeshPassType::GBuffer, &mesh, &masked) & BGFX_STATE_WRITE_RGB) != 0U, "GBuffer state must write RGB attachments");
    Require((MeshPipelinePassPolicy::State(MeshPassType::GBuffer, &mesh, &masked) & BGFX_STATE_WRITE_A) != 0U, "GBuffer state must write alpha attachments");
    Require((MeshPipelinePassPolicy::State(MeshPassType::GBuffer, &mesh, &masked) & BGFX_STATE_WRITE_Z) != 0U, "GBuffer state must write depth");
    Require((MeshPipelinePassPolicy::State(MeshPassType::GBuffer, &mesh, &masked) & BGFX_STATE_BLEND_MASK) == 0U, "GBuffer state must not enable blending");
}

void RunMeshPipelineUsesMaterialDoubleSidedStateTest() {
    RenderMeshResource mesh{};
    mesh.indexCount = 3U;
    mesh.doubleSided = false;
    mesh.bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 1.0F };
    mesh.sections = {
        RenderMeshSection{
            .indexStart = 0U,
            .indexCount = 3U,
            .bounds = mesh.bounds,
        },
    };
    RenderMaterialResource material{};
    material.doubleSided = true;
    const std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .materialAssetId = 7U,
            .instances = { SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U, .materialAssetId = 7U, .model = IdentityMatrix() } },
        },
    };

    const MeshPipelineBuildResult result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .drawGroups = &drawGroups,
        .resolvedMeshResource = &mesh,
        .resolvedMaterialResource = &material,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });

    Require(result.commands.size() == 1U, "MeshPipeline double sided material test did not emit a command");
    Require((result.commands[0].state & BGFX_STATE_CULL_CW) == 0U, "MeshPipeline ignored material double sided state");
    Require((result.commands[0].state & BGFX_STATE_CULL_CCW) == 0U, "MeshPipeline ignored material double sided state");
}

void RunMeshPipelineCullsBackFacesForSingleSidedMeshesTest() {
    RenderMeshResource mesh{};
    mesh.indexCount = 3U;
    mesh.doubleSided = false;
    mesh.bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 1.0F };
    mesh.sections = {
        RenderMeshSection{
            .indexStart = 0U,
            .indexCount = 3U,
            .bounds = mesh.bounds,
        },
    };
    RenderMaterialResource material{};
    const std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .materialAssetId = 7U,
            .instances = { SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U, .materialAssetId = 7U, .model = IdentityMatrix() } },
        },
    };

    const MeshPipelineBuildResult result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .drawGroups = &drawGroups,
        .resolvedMeshResource = &mesh,
        .resolvedMaterialResource = &material,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });

    Require(result.commands.size() == 1U, "MeshPipeline single-sided culling test did not emit a command");
    Require((result.commands[0].state & BGFX_STATE_CULL_CCW) != 0U, "MeshPipeline does not cull back faces for single-sided meshes");
    Require((result.commands[0].state & BGFX_STATE_CULL_CW) == 0U, "MeshPipeline culls the authored front faces");
}

void RunMeshPipelineSeparatesMirroredInstancesTest() {
    RenderMeshResource mesh{};
    mesh.indexCount = 3U;
    mesh.bounds = { .radius = 1.0F };
    RenderMaterialResource material{};
    std::vector<SceneRenderDrawGroup> groups{ SceneRenderDrawGroup{ .meshAssetId = 42U, .materialAssetId = 7U } };
    for (std::uint64_t id = 1; id <= 3; ++id) {
        auto model = IdentityMatrix();
        if (id >= 2) model[0] = -2.0F;
        if (id == 3) model[5] = -3.0F;
        groups[0].instances.push_back({ .entityId = id, .meshAssetId = 42U, .materialAssetId = 7U, .model = model });
    }
    MeshPipelineBuildResult result;
    for (const auto pass : { MeshPassType::BaseOpaque, MeshPassType::Depth, MeshPassType::ShadowDepth, MeshPassType::MotionVectors }) {
        const MeshPipelineBuildDesc desc{
            .pass = pass, .drawGroups = &groups,
            .resolvedMeshResource = &mesh, .resolvedMaterialResource = &material,
            .resourceValidation = MeshPipelineResourceValidation::Skip,
        };
        material.doubleSided = false;
        MeshPipelineProcessor::BuildInto(desc, result);
        Require(result.commands.size() == 2U && result.stats.visibleMeshCount == 3U,
            "Opposite winding must split draw commands without losing instances");
        for (const auto& command : result.commands) {
            for (const auto& instance : command.instances) {
                const auto expected = instance.entityId == 2 ? BGFX_STATE_CULL_CW : BGFX_STATE_CULL_CCW;
                Require((command.state & BGFX_STATE_CULL_MASK) == expected,
                    "Mirrored instance has incorrect culling in a geometry or shadow pass");
            }
        }
        material.doubleSided = true;
        MeshPipelineProcessor::BuildInto(desc, result);
        Require(result.commands.size() == 1U && result.commands[0].instances.size() == 3U &&
                (result.commands[0].state & BGFX_STATE_CULL_MASK) == 0U,
            "Double-sided materials must retain one batch across transform handedness");
    }
}

void RunMeshPipelineSkinnedCommandStorageScalesWithInstancesTest() {
    constexpr std::uint32_t actorCount = 256U;
    std::vector<SceneRenderDrawGroup> groups{SceneRenderDrawGroup{.meshAssetId = 42U, .materialAssetId = 7U}};
    for (std::uint32_t index = 0U; index < actorCount; ++index) {
        groups.front().instances.push_back(SceneRenderMeshInstance{
            .entityId = index + 1U,
            .meshAssetId = 42U,
            .materialAssetId = 7U,
            .currentSkinningPalette = {.frame = 10U, .firstMatrix = index * 2U, .matrixCount = 2U, .bufferIndex = 0U},
            .previousSkinningPalette = {.frame = 9U, .firstMatrix = index * 2U, .matrixCount = 2U, .bufferIndex = 1U},
        });
    }
    const auto result = MeshPipelineProcessor::Build({
        .pass = MeshPassType::BaseOpaque,
        .drawGroups = &groups,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });
    Require(result.commands.size() == actorCount && result.stats.visibleMeshCount == actorCount,
        "Animated crowd must retain every independently skinned instance");
    std::size_t capacity = 0U;
    for (const auto& command : result.commands) {
        capacity += command.instances.capacity();
    }
    Require(capacity <= actorCount * 2U,
        "Split skinned commands must not each reserve storage for the entire crowd");
    Require(result.stats.meshPipelineScratchInstanceCapacity == capacity,
        "Mesh pipeline must report actual allocated instance storage");
}

void RunMeshPipelineKeepsBlendDisabledUntilTransparentPassIsReadyTest() {
    RenderMeshResource mesh{};
    mesh.indexCount = 3U;
    mesh.bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 1.0F };
    mesh.sections = {
        RenderMeshSection{
            .indexStart = 0U,
            .indexCount = 3U,
            .bounds = mesh.bounds,
        },
    };
    RenderMaterialResource material{};
    material.alphaMode = RenderMaterialAlphaMode::Blend;
    const std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .materialAssetId = 7U,
            .instances = { SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U, .materialAssetId = 7U, .model = TranslationMatrix(0.0F, 0.0F, 0.0F) } },
        },
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .materialAssetId = 7U,
            .instances = { SceneRenderMeshInstance{ .entityId = 2U, .meshAssetId = 42U, .materialAssetId = 7U, .model = TranslationMatrix(0.0F, 0.0F, 0.1F) } },
        },
    };
    const SceneRenderCamera camera{
        .view = IdentityMatrix(),
        .projection = IdentityMatrix(),
    };

    SceneRenderDiagnostics diagnostics;
    const MeshPipelineBuildResult result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseTransparent,
        .drawGroups = &drawGroups,
        .resolvedMeshResource = &mesh,
        .resolvedMaterialResource = &material,
        .camera = &camera,
        .diagnostics = &diagnostics,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });

    Require(result.commands.size() == 2U, "KBMAT-MAT80: Transparent pass must emit commands for blended materials (one per draw group)");
    Require(diagnostics.events.empty(), "KBMAT-MAT80: Transparent pass must not emit an unsupported-blend diagnostic");
}

void RunMeshPipelineCpuCullsByFrustumBoundsTest() {
    RenderMeshResource mesh{};
    mesh.indexCount = 3U;
    mesh.gpuCullingEnabled = true;
    mesh.bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 0.25F };
    mesh.sections = {
        RenderMeshSection{
            .indexStart = 0U,
            .indexCount = 3U,
            .bounds = mesh.bounds,
        },
    };

    const SceneRenderCamera camera{
        .view = IdentityMatrix(),
        .projection = IdentityMatrix(),
    };
    const std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .instances = {
                SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U, .model = TranslationMatrix(0.0F, 0.0F, 0.0F) },
                SceneRenderMeshInstance{ .entityId = 2U, .meshAssetId = 42U, .model = TranslationMatrix(4.0F, 0.0F, 0.0F) },
            },
        },
    };

    const MeshPipelineBuildResult result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .drawGroups = &drawGroups,
        .resolvedMeshResource = &mesh,
        .camera = &camera,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });

    Require(result.commands.size() == 1U, "MeshPipeline culling test did not emit the visible command");
    Require(result.commands[0].instances.size() == 1U, "MeshPipeline did not CPU-cull the out-of-frustum instance");
    Require(result.commands[0].instances[0].entityId == 1U, "MeshPipeline kept the wrong culled instance");
    Require(result.commands[0].instances[0].worldBounds.IsValid(), "MeshPipeline did not store per-instance world bounds");
    Require(result.stats.visibleMeshCount == 1U, "MeshPipeline culling stats did not count visible instances");
    Require(result.stats.culledInstanceCount == 1U, "MeshPipeline culling stats did not count culled instances");
    Require(result.gpuDrivenInputRecords.size() == 1U && result.gpuDrivenInputRecords[0].entityId == 1U,
        "MeshPipeline uploaded an instance already culled by the CPU");
}

void RunMeshPipelineSortsInterleavedTransparentInstancesTest() {
    RenderMeshResource mesh{};
    mesh.indexCount = 3U;
    mesh.bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 0.02F };
    mesh.sections = { RenderMeshSection{ .indexStart = 0U, .indexCount = 3U, .bounds = mesh.bounds } };
    RenderMaterialResource material{};
    material.alphaMode = RenderMaterialAlphaMode::Blend;
    const std::vector<SceneRenderDrawGroup> groups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .materialAssetId = 7U,
            .instances = {
                SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U, .materialAssetId = 7U, .model = TranslationMatrix(0.0F, 0.0F, 0.8F) },
                SceneRenderMeshInstance{ .entityId = 3U, .meshAssetId = 42U, .materialAssetId = 7U, .model = TranslationMatrix(0.0F, 0.0F, 0.2F) },
            },
        },
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .materialAssetId = 8U,
            .instances = {
                SceneRenderMeshInstance{ .entityId = 2U, .meshAssetId = 42U, .materialAssetId = 8U, .model = TranslationMatrix(0.0F, 0.0F, 0.5F) },
            },
        },
    };
    auto verify = [&](const SceneRenderCamera& camera, std::array<std::uint64_t, 3U> expected) {
        const MeshPipelineBuildResult result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
            .pass = MeshPassType::BaseTransparent,
            .drawGroups = &groups,
            .resolvedMeshResource = &mesh,
            .resolvedMaterialResource = &material,
            .camera = &camera,
            .resourceValidation = MeshPipelineResourceValidation::Skip,
        });
        Require(result.commands.size() == 3U && result.stats.visibleMeshCount == 3U,
            "Transparent interleaved instances must remain individually ordered across materials");
        for (std::size_t index = 0U; index < expected.size(); ++index) {
            Require(result.commands[index].instances.size() == 1U &&
                result.commands[index].instances.front().entityId == expected[index],
                "Transparent commands are not globally back-to-front");
        }
    };
    SceneRenderCamera camera{ .view = IdentityMatrix(), .projection = IdentityMatrix() };
    verify(camera, { 1U, 2U, 3U });
    camera.view[10] = -1.0F;
    camera.view[14] = 1.0F;
    verify(camera, { 3U, 2U, 1U });
    camera.view = IdentityMatrix();
    SceneRenderDiagnostics diagnostics;
    const MeshPipelineBuildResult limited = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseTransparent,
        .drawGroups = &groups,
        .resolvedMeshResource = &mesh,
        .resolvedMaterialResource = &material,
        .camera = &camera,
        .diagnostics = &diagnostics,
        .maxDrawCommands = 2U,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });
    Require(limited.commands.size() == 2U && limited.stats.droppedInstanceCount == 1U &&
        limited.stats.visibleMeshCount == 2U && !diagnostics.events.empty(),
        "Transparent ordering must enforce the final draw budget and report dropped instances");
}

void RunMeshPipelineKeepsTerrainOverlayLayerOrderTest() {
    RenderMeshResource mesh{};
    mesh.indexCount = 3U;
    mesh.terrainLayerCount = 3U;
    mesh.bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 0.02F };
    mesh.sections = {
        RenderMeshSection{ .indexStart = 0U, .indexCount = 3U, .bounds = mesh.bounds, .terrainLayerIndex = 2U },
        RenderMeshSection{ .indexStart = 0U, .indexCount = 3U, .bounds = mesh.bounds, .terrainLayerIndex = 1U },
        RenderMeshSection{ .indexStart = 0U, .indexCount = 3U, .bounds = mesh.bounds },
    };
    RenderMaterialResource material{};
    material.alphaMode = RenderMaterialAlphaMode::Blend;
    const std::vector<SceneRenderDrawGroup> groups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .materialAssetId = 7U,
            .instances = { SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U,
                .materialAssetId = 7U, .model = TranslationMatrix(0.0F, 0.0F, 0.5F) } },
        },
    };
    const SceneRenderCamera camera{ .view = IdentityMatrix(), .projection = IdentityMatrix() };
    const MeshPipelineBuildResult result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseTransparent,
        .drawGroups = &groups,
        .resolvedMeshResource = &mesh,
        .resolvedMaterialResource = &material,
        .camera = &camera,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });
    Require(result.commands.size() == 3U && result.commands[0].terrainLayerIndex == 1U &&
        result.commands[1].terrainLayerIndex == 2U && result.commands[2].terrainLayerIndex == UINT8_MAX,
        "Transparent terrain overlays must preserve authored layer order at equal depth");
}

void RunMeshPipelineAnimatedBoundsOverrideControlsCullingTest() {
    RenderMeshResource mesh{};
    mesh.indexCount = 3U;
    mesh.bounds = RenderBoundsSphere{ .center = { 4.0F, 0.0F, 0.0F }, .radius = 0.25F };
    mesh.sections = {
        RenderMeshSection{ .indexStart = 0U, .indexCount = 3U, .bounds = mesh.bounds },
    };
    const SceneRenderCamera camera{ .view = IdentityMatrix(), .projection = IdentityMatrix() };
    const std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .instances = {
                SceneRenderMeshInstance{
                    .entityId = 1U, .meshAssetId = 42U, .model = TranslationMatrix(0.0F, 0.0F, 0.0F),
                    .boundsOverride = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 0.25F },
                },
                SceneRenderMeshInstance{
                    .entityId = 2U, .meshAssetId = 42U, .model = TranslationMatrix(0.0F, 0.0F, 0.0F),
                    .boundsOverride = RenderBoundsSphere{ .center = { 4.0F, 0.0F, 0.0F }, .radius = 0.25F },
                },
            },
        },
    };
    const MeshPipelineBuildResult result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .drawGroups = &drawGroups,
        .resolvedMeshResource = &mesh,
        .camera = &camera,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });
    Require(result.commands.size() == 1U && result.commands[0].instances.size() == 1U &&
            result.commands[0].instances[0].entityId == 1U && result.stats.culledInstanceCount == 1U,
        "MeshPipeline did not cull from the animated bounds override instead of static mesh bounds");
}

void RunMeshPipelineSelectsLodAndCarriesMeshletRangesTest() {
    RenderMeshResource mesh{};
    mesh.indexCount = 12U;
    mesh.bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 0.1F };
    mesh.sections = {
        RenderMeshSection{
            .indexStart = 0U,
            .indexCount = 6U,
            .bounds = mesh.bounds,
            .lodLevel = 0U,
        },
        RenderMeshSection{
            .indexStart = 6U,
            .indexCount = 6U,
            .bounds = mesh.bounds,
            .lodLevel = 1U,
        },
    };
    mesh.meshlets = {
        RenderMeshletDesc{
            .indexStart = 0U,
            .indexCount = 6U,
            .vertexStart = 0U,
            .vertexCount = 4U,
            .sectionIndex = 0U,
            .bounds = mesh.bounds,
            .lodLevel = 0U,
        },
        RenderMeshletDesc{
            .indexStart = 6U,
            .indexCount = 6U,
            .vertexStart = 0U,
            .vertexCount = 4U,
            .sectionIndex = 1U,
            .bounds = mesh.bounds,
            .lodLevel = 1U,
        },
    };
    mesh.lods = {
        RenderMeshLodDesc{ .firstSection = 0U, .sectionCount = 1U, .firstMeshlet = 0U, .meshletCount = 1U, .minScreenCoverage = 0.5F },
        RenderMeshLodDesc{ .firstSection = 1U, .sectionCount = 1U, .firstMeshlet = 1U, .meshletCount = 1U, .minScreenCoverage = 0.0F },
    };
    mesh.gpuCullingEnabled = true;
    mesh.indirectDrawsEnabled = true;
    mesh.meshletCullingEnabled = true;

    const SceneRenderCamera camera = PerspectiveCamera();
    const std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .instances = {
                SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U, .model = TranslationMatrix(0.0F, 0.0F, 0.1F) },
                SceneRenderMeshInstance{ .entityId = 2U, .meshAssetId = 42U, .model = TranslationMatrix(0.0F, 0.0F, 0.9F) },
            },
        },
    };

    const MeshPipelineBuildResult result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .drawGroups = &drawGroups,
        .resolvedMeshResource = &mesh,
        .camera = &camera,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });

    Require(result.commands.size() == 2U, "MeshPipeline did not emit one command per selected LOD section");
    bool sawLod0 = false;
    bool sawLod1 = false;
    for (const MeshDrawCommand& command : result.commands) {
        if (command.lodLevel == 0U) {
            sawLod0 = command.instances.size() == 1U && command.instances[0].entityId == 1U && command.firstMeshlet == 0U && command.meshletCount == 1U;
        }
        if (command.lodLevel == 1U) {
            sawLod1 = command.instances.size() == 1U && command.instances[0].entityId == 2U && command.firstMeshlet == 1U && command.meshletCount == 1U;
        }
    }
    Require(sawLod0, "MeshPipeline did not select the near instance into LOD0 meshlet range");
    Require(sawLod1, "MeshPipeline did not select the far instance into LOD1 meshlet range");
    Require(result.stats.gpuDrivenDrawCandidateCount == 2U, "MeshPipeline did not count GPU-driven draw candidates");
    Require(result.stats.indirectDrawCandidateCount == 2U, "MeshPipeline did not count indirect draw candidates");
    Require(result.stats.meshletCullingCandidateCount == 2U, "MeshPipeline did not count meshlet culling candidates");
    Require(result.stats.gpuDrivenInputInstanceCount == 2U, "MeshPipeline did not build GPU-driven input records");
    Require(result.gpuDrivenInputRecords.size() == 2U, "MeshPipeline did not preserve GPU-driven input batch records");
    Require(result.gpuDrivenInputRecords[0].IsValid(), "MeshPipeline produced an invalid GPU-driven bounds record");
    Require(result.stats.lodSelectionCount == 4U, "MeshPipeline did not evaluate LOD selection for every section candidate");
    Require(result.stats.gpuDrivenFeatureState == SceneGpuDrivenFeatureState::CpuValidationOnly, "MeshPipeline reported CPU GPU-driven candidates as executed GPU work");
    Require(result.stats.gpuDrivenCounterSource == SceneGpuDrivenCounterSource::CpuCandidates, "MeshPipeline did not mark GPU-driven counters as CPU candidates");
    Require(result.stats.gpuCullingDispatchCount == 0U, "MeshPipeline reported GPU culling dispatches without a GPU pass");
    Require(result.stats.indirectDrawSubmitCount == 0U, "MeshPipeline reported indirect submits without a GPU pass");
    Require(result.stats.meshletSubmitCount == 0U, "MeshPipeline reported meshlet submits without a GPU pass");
}

void RunMeshPipelineCullsWithVisibilityBlockerTest() {
    RenderMeshResource mesh{};
    mesh.indexCount = 3U;
    mesh.bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 0.05F };
    mesh.sections = { RenderMeshSection{ .indexStart = 0U, .indexCount = 3U, .bounds = mesh.bounds } };
    const SceneRenderCamera camera{ .view = IdentityMatrix(), .projection = IdentityMatrix() };
    const std::vector<SceneRenderDrawGroup> drawGroups{ SceneRenderDrawGroup{
        .meshAssetId = 42U,
        .instances = { SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U, .model = TranslationMatrix(0.0F, 0.0F, 0.9F) } },
    } };
    std::array<SceneRenderVisibilityBlocker, 1U> blockers{{
        SceneRenderVisibilityBlocker{ .entityId = 2U, .model = TranslationMatrix(0.0F, 0.0F, 0.5F), .size = { 0.2F, 0.2F, 0.2F } },
    }};
    const MeshPipelineBuildResult result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque, .drawGroups = &drawGroups, .resolvedMeshResource = &mesh,
        .camera = &camera, .visibilityBlockers = blockers, .resourceValidation = MeshPipelineResourceValidation::Skip,
    });
    Require(result.commands.empty(), "Visibility Blocker emitted a draw instead of rejecting the occluded instance");
    Require(result.stats.culledInstanceCount == 1U, "Visibility Blocker did not contribute to culling statistics");
    blockers[0].model = TranslationMatrix(0.0F, 0.0F, -0.5F);
    for (const float viewDirection : {1.0F, -1.0F}) {
        auto oppositeCamera = camera;
        oppositeCamera.view[10] = viewDirection;
        oppositeCamera.projection[10] *= viewDirection;
        const auto unobstructed = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
            .pass = MeshPassType::BaseOpaque, .drawGroups = &drawGroups, .resolvedMeshResource = &mesh,
            .camera = &oppositeCamera, .visibilityBlockers = blockers, .resourceValidation = MeshPipelineResourceValidation::Skip,
        });
        Require(unobstructed.commands.size() == 1U && unobstructed.stats.culledInstanceCount == 0U,
            "A blocker on the opposite side of the camera culled the visible mesh");
    }
    auto distantGroups = drawGroups;
    distantGroups[0].instances[0].model = TranslationMatrix(0.3F, 0.0F, 100.0F);
    auto orthographicCamera = camera;
    orthographicCamera.projection[10] = 0.001F;
    blockers[0].model = TranslationMatrix(0.0F, 0.0F, 0.5F);
    const auto besideBlocker = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque, .drawGroups = &distantGroups, .resolvedMeshResource = &mesh,
        .camera = &orthographicCamera, .visibilityBlockers = blockers, .resourceValidation = MeshPipelineResourceValidation::Skip,
    });
    Require(besideBlocker.commands.size() == 1U && besideBlocker.stats.culledInstanceCount == 0U,
        "Orthographic visibility used perspective angular bounds to hide a mesh beside the blocker");
}

// Cells A, B and C in a row along +Z with portals A -> B and B -> C: the mesh passes and the visibility
// feedback cull what stands in a cell the camera cannot see through the portals.
void RunMeshPipelineCullsInvisibleVisibilityCellsTest() {
    kb::scene::Scene scene;
    const auto addCell = [&scene](float z) {
        const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Cell", .transform = kb::scene::TransformComponent{ .localPosition = { 0.0F, 0.0F, z } } });
        scene.Components().RegionShapes().Set(object.Entity(),
            kb::scene::RegionShapeComponent{ .kind = kb::scene::RegionShapeKind::Box, .size = { 10.0F, 6.0F, 10.0F } });
        scene.Components().VisibilityCells().Set(object.Entity(), kb::scene::VisibilityCellComponent{});
        return object.Entity();
    };
    const auto addPortal = [&scene](float z, kb::scene::SceneEntity from, kb::scene::SceneEntity to) {
        const kb::scene::SceneObject object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{
            .name = "Portal", .transform = kb::scene::TransformComponent{ .localPosition = { 0.0F, 0.0F, z } } });
        scene.Components().RegionShapes().Set(object.Entity(),
            kb::scene::RegionShapeComponent{ .kind = kb::scene::RegionShapeKind::Box, .size = { 2.0F, 3.0F, 0.5F } });
        scene.Components().RegionPortals().Set(object.Entity(),
            kb::scene::SceneRegionPortalComponent{ .sourceCell = from, .targetCell = to, .enabled = true });
        return object.Entity();
    };
    const kb::scene::SceneEntity a = addCell(0.0F);
    const kb::scene::SceneEntity b = addCell(10.0F);
    const kb::scene::SceneEntity c = addCell(20.0F);
    static_cast<void>(addPortal(5.0F, a, b));
    const kb::scene::SceneEntity bc = addPortal(15.0F, b, c);
    static_cast<void>(scene.Runtime().Update(0.0F));

    RenderScene cameras;
    static_cast<void>(cameras.UpsertCamera(CameraRenderProxyDesc{
        .entityId = 1U, .position = { 0.0F, 0.0F, -3.0F }, .verticalFovDegrees = 90.0F, .primary = true }));
    const std::optional<SceneRenderCamera> camera = cameras.BuildPrimaryCamera(1024U, 1024U);
    Require(camera.has_value(), "Portal culling test camera could not be built");
    const kb::scene::ScenePortalCamera portalCamera = MeshPipelineVisibility::PortalCamera(*camera);
    Require(NearlyEqual(portalCamera.position.x, 0.0F) && NearlyEqual(portalCamera.position.y, 0.0F) &&
            NearlyEqual(portalCamera.position.z, -3.0F),
        "The portal pass must see the render camera at its position");

    RenderMeshResource mesh{};
    mesh.indexCount = 3U;
    mesh.bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 0.5F };
    mesh.sections = { RenderMeshSection{ .indexStart = 0U, .indexCount = 3U, .bounds = mesh.bounds } };
    const std::vector<SceneRenderDrawGroup> drawGroups{ SceneRenderDrawGroup{
        .meshAssetId = 42U,
        .instances = {
            SceneRenderMeshInstance{ .entityId = 11U, .meshAssetId = 42U, .model = TranslationMatrix(0.0F, 0.0F, 2.0F) },
            SceneRenderMeshInstance{ .entityId = 12U, .meshAssetId = 42U, .model = TranslationMatrix(0.0F, 0.0F, 10.0F) },
            SceneRenderMeshInstance{ .entityId = 13U, .meshAssetId = 42U, .model = TranslationMatrix(0.0F, 0.0F, 20.0F) },
        },
    } };
    const auto drawnInstances = [&](MeshPassType pass, const kb::scene::ScenePortalVisibility* visibility, std::uint32_t& culled) {
        const MeshPipelineBuildResult result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
            .pass = pass, .drawGroups = &drawGroups, .resolvedMeshResource = &mesh, .camera = &*camera,
            .portalVisibility = visibility, .resourceValidation = MeshPipelineResourceValidation::Skip,
        });
        culled = result.stats.culledInstanceCount;
        std::size_t drawn = 0U;
        for (const MeshDrawCommand& command : result.commands) drawn += command.instances.size();
        return drawn;
    };

    std::uint32_t culled = 0U;
    const kb::scene::ScenePortalVisibility open = kb::scene::ComputeScenePortalVisibility(scene, portalCamera);
    Require(drawnInstances(MeshPassType::BaseOpaque, &open, culled) == 3U && culled == 0U,
        "Meshes in cells seen through open portals must be drawn");

    kb::scene::SceneRegionPortalComponent closed = *scene.Components().RegionPortals().TryGet(bc);
    closed.enabled = false;
    scene.Components().RegionPortals().Set(bc, closed);
    const kb::scene::ScenePortalVisibility visibility = kb::scene::ComputeScenePortalVisibility(scene, portalCamera);
    Require(drawnInstances(MeshPassType::BaseOpaque, &visibility, culled) == 2U && culled == 1U,
        "A mesh in the cell behind a closed portal must be culled from the opaque pass");
    Require(drawnInstances(MeshPassType::ShadowDepth, &visibility, culled) == 3U,
        "Shadow casters in hidden cells must still cast into the visible ones");
    Require(drawnInstances(MeshPassType::BaseOpaque, nullptr, culled) == 3U, "Without portal visibility nothing is culled by cell");

    // The visibility feedback scripts read reports the same.
    RenderScene renderScene;
    for (const SceneRenderMeshInstance& instance : drawGroups[0].instances) {
        static_cast<void>(renderScene.UpsertMesh(MeshRenderProxyDesc{ .entityId = instance.entityId, .meshAssetId = 42U, .model = instance.model,
            .boundsOverride = mesh.bounds, .visible = true, .layer = 1U }));
    }
    renderScene.SetPortalVisibility(visibility);
    kb::scene::SceneRenderVisibilityFrame frame;
    SceneRenderVisibilityPublisher::BuildFrame(renderScene, &*camera, 1U, 0U, 1024U, 1024U, nullptr, nullptr, frame);
    Require(frame.entries.size() == 3U && frame.entries[0].visible && frame.entries[1].visible && !frame.entries[2].visible,
        "Visibility feedback must report a mesh in a hidden cell as not visible");
    renderScene.SetPortalVisibility(std::nullopt);
    Require(renderScene.PortalVisibility() == nullptr, "Clearing the portal visibility must stop culling by cell");
    static_cast<void>(c);
}

void RunMeshPipelineCoordinatesDetailSwitchGroupsWithHysteresisTest() {
    RenderMeshResource mesh{};
    mesh.indexCount = 12U;
    mesh.bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 0.1F };
    mesh.sections = {
        RenderMeshSection{ .indexStart = 0U, .indexCount = 6U, .bounds = mesh.bounds, .lodLevel = 0U },
        RenderMeshSection{ .indexStart = 6U, .indexCount = 6U, .bounds = mesh.bounds, .lodLevel = 1U },
    };
    mesh.lods = {
        RenderMeshLodDesc{ .firstSection = 0U, .sectionCount = 1U, .minScreenCoverage = 0.5F },
        RenderMeshLodDesc{ .firstSection = 1U, .sectionCount = 1U, .minScreenCoverage = 0.0F },
    };
    const SceneRenderCamera camera = PerspectiveCamera();
    std::vector<SceneRenderDrawGroup> groups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .instances = {
                SceneRenderMeshInstance{
                    .entityId = 1U, .meshAssetId = 42U, .model = TranslationMatrix(0.0F, 0.0F, 0.1F),
                    .detailSwitchGroupId = 7U, .detailSwitchMinimumLod = 0U, .detailSwitchMaximumLod = 1U,
                    .detailSwitchPromoteCoverage = 0.20F, .detailSwitchDemoteCoverage = 0.15F, .detailSwitchEnabled = true,
                },
                SceneRenderMeshInstance{
                    .entityId = 2U, .meshAssetId = 42U, .model = TranslationMatrix(0.0F, 0.0F, 0.9F),
                    .detailSwitchGroupId = 7U, .detailSwitchMinimumLod = 0U, .detailSwitchMaximumLod = 1U,
                    .detailSwitchPromoteCoverage = 0.20F, .detailSwitchDemoteCoverage = 0.15F, .detailSwitchEnabled = true,
                },
            },
        },
    };
    MeshPipelineBuildResult result;
    MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque, .drawGroups = &groups, .resolvedMeshResource = &mesh, .camera = &camera,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    }, result);
    Require(result.commands.size() == 1U && result.commands[0].lodLevel == 0U && result.commands[0].instances.size() == 2U,
        "Detail Switch did not force one coordinated LOD level for its group");

    groups[0].instances.resize(1U);
    groups[0].instances[0].model = TranslationMatrix(0.0F, 0.0F, 0.55F);
    MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque, .drawGroups = &groups, .resolvedMeshResource = &mesh, .camera = &camera,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    }, result);
    Require(result.commands.size() == 1U && result.commands[0].lodLevel == 0U,
        "Detail Switch changed LOD inside its hysteresis band");

    groups[0].instances[0].model = TranslationMatrix(0.0F, 0.0F, 0.8F);
    // Shadows run before the opaque pass that advances the shared hysteresis.
    MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{
        .pass = MeshPassType::ShadowDepth, .drawGroups = &groups, .resolvedMeshResource = &mesh, .camera = &camera,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    }, result);
    Require(result.commands.size() == 1U && result.commands[0].lodLevel == 0U,
        "Shadow pass unexpectedly advanced the opaque LOD history");
    SceneMeshCommandReuseKey shadowKey{.detailSwitchHistoryRevision = result.detailSwitchHistoryRevision};
    SceneMeshCommandReuseGate shadowReuse;
    shadowReuse.Commit(shadowKey, result.commands, result.stats);
    const auto historyBeforeDemotion = result.detailSwitchHistoryRevision;
    MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque, .drawGroups = &groups, .resolvedMeshResource = &mesh, .camera = &camera,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    }, result);
    Require(result.commands.size() == 1U && result.commands[0].lodLevel == 1U,
        "Detail Switch did not demote after leaving its hysteresis band");
    shadowKey.detailSwitchHistoryRevision = result.detailSwitchHistoryRevision;
    Require(result.detailSwitchHistoryRevision != historyBeforeDemotion && !shadowReuse.Matches(shadowKey),
        "Opaque LOD demotion retained a shadow command with obsolete geometry");
    MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{
        .pass = MeshPassType::ShadowDepth, .drawGroups = &groups, .resolvedMeshResource = &mesh, .camera = &camera,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    }, result);
    Require(result.commands.size() == 1U && result.commands[0].lodLevel == 1U,
        "Rebuilt shadow commands did not consume the updated LOD history");

    groups[0].instances[0].model = TranslationMatrix(0.0F, 0.0F, 0.0F);
    groups[0].instances[0].detailSwitchMinimumLod = 1U;
    groups[0].instances[0].detailSwitchMaximumLod = 1U;
    MeshPipelineBuildResult forcedResult;
    MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque, .drawGroups = &groups, .resolvedMeshResource = &mesh, .camera = &camera,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    }, forcedResult);
    Require(forcedResult.commands.size() == 1U && forcedResult.commands[0].lodLevel == 1U,
        "Detail Switch minimum/maximum policy did not force the requested preview LOD");
}

void RunGpuDrivenFeatureClassifierGatesByCapabilitiesTest() {
    constexpr SceneGpuDrivenFeatureRequest fullRequest{
        .gpuCullingRequested = true,
        .indirectDrawRequested = true,
        .meshletSubmitRequested = true,
    };

    Require(
        SceneGpuDrivenFeatureClassifier::Resolve(fullRequest, SceneGpuDrivenFeatureSupport{}) ==
            SceneGpuDrivenFeatureState::CpuValidationOnly,
        "GPU-driven classifier did not fall back to CPU validation when compute is unavailable");
    Require(
        SceneGpuDrivenFeatureClassifier::Resolve(fullRequest, SceneGpuDrivenFeatureSupport{.computeCullingSupported = true}) ==
            SceneGpuDrivenFeatureState::CpuValidationOnly,
        "GPU-driven classifier did not keep CPU validation when runtime GPU dispatch is unavailable");
    Require(
        SceneGpuDrivenFeatureClassifier::Resolve(fullRequest, SceneGpuDrivenFeatureSupport{
            .computeCullingSupported = true,
            .runtimeGpuDispatchSupported = true,
        }) == SceneGpuDrivenFeatureState::ComputeCulling,
        "GPU-driven classifier did not enable compute culling when only compute is available");
    Require(
        SceneGpuDrivenFeatureClassifier::Resolve(fullRequest, SceneGpuDrivenFeatureSupport{
            .computeCullingSupported = true,
            .indirectDrawSupported = true,
            .runtimeGpuDispatchSupported = true,
        }) == SceneGpuDrivenFeatureState::IndirectDrawSubmit,
        "GPU-driven classifier did not enable indirect draw submit when indirect draws are available");
    Require(
        SceneGpuDrivenFeatureClassifier::Resolve(fullRequest, SceneGpuDrivenFeatureSupport{
            .computeCullingSupported = true,
            .indirectDrawSupported = true,
            .meshletSubmitSupported = true,
            .runtimeGpuDispatchSupported = true,
        }) == SceneGpuDrivenFeatureState::MeshletSubmit,
        "GPU-driven classifier did not enable meshlet submit when all requested capabilities are available");
    Require(
        SceneGpuDrivenFeatureClassifier::CounterSourceForState(SceneGpuDrivenFeatureState::CpuValidationOnly) ==
            SceneGpuDrivenCounterSource::CpuCandidates,
        "GPU-driven classifier did not mark CPU validation counters as CPU candidates");
    Require(
        SceneGpuDrivenFeatureClassifier::CounterSourceForState(SceneGpuDrivenFeatureState::ComputeCulling) ==
            SceneGpuDrivenCounterSource::GpuDispatchCounters,
        "GPU-driven classifier did not mark compute culling counters as GPU dispatch counters");
}

void RunGpuDrivenFeatureClassifierReportsIndirectFallbackTest() {
    constexpr SceneGpuDrivenFeatureRequest indirectRequest{
        .gpuCullingRequested = true,
        .indirectDrawRequested = true,
    };

    constexpr SceneGpuDrivenFeatureDecision noCompute = SceneGpuDrivenFeatureClassifier::Decide(
        indirectRequest,
        SceneGpuDrivenFeatureSupport{});
    Require(noCompute.state == SceneGpuDrivenFeatureState::CpuValidationOnly, "GPU-driven fallback did not use CPU validation without compute");
    Require(noCompute.counterSource == SceneGpuDrivenCounterSource::CpuCandidates, "GPU-driven fallback did not mark CPU validation counters");
    Require(noCompute.fallbackReason == SceneGpuDrivenFallbackReason::ComputeUnsupported, "GPU-driven fallback did not report missing compute support");
    Require(noCompute.UsesFallback(), "GPU-driven fallback decision did not mark missing compute as a fallback");

    constexpr SceneGpuDrivenFeatureDecision noIndirect = SceneGpuDrivenFeatureClassifier::Decide(
        indirectRequest,
        SceneGpuDrivenFeatureSupport{
            .computeCullingSupported = true,
            .runtimeGpuDispatchSupported = true,
        });
    Require(noIndirect.state == SceneGpuDrivenFeatureState::ComputeCulling, "GPU-driven fallback did not downgrade indirect submit to compute culling");
    Require(noIndirect.counterSource == SceneGpuDrivenCounterSource::GpuDispatchCounters, "GPU-driven fallback did not mark compute counters as GPU dispatch counters");
    Require(noIndirect.fallbackReason == SceneGpuDrivenFallbackReason::IndirectDrawUnsupported, "GPU-driven fallback did not report missing indirect draw support");
    Require(noIndirect.UsesFallback(), "GPU-driven fallback decision did not mark missing indirect draw as a fallback");

    constexpr SceneGpuDrivenFeatureDecision runtimeDispatchMissing = SceneGpuDrivenFeatureClassifier::Decide(
        indirectRequest,
        SceneGpuDrivenFeatureSupport{
            .computeCullingSupported = true,
            .indirectDrawSupported = true,
        });
    Require(runtimeDispatchMissing.state == SceneGpuDrivenFeatureState::CpuValidationOnly, "GPU-driven fallback did not use CPU validation when runtime dispatch is unavailable");
    Require(runtimeDispatchMissing.counterSource == SceneGpuDrivenCounterSource::CpuCandidates, "GPU-driven runtime fallback did not mark CPU candidate counters");
    Require(runtimeDispatchMissing.fallbackReason == SceneGpuDrivenFallbackReason::RuntimeGpuDispatchUnavailable, "GPU-driven fallback did not report missing runtime GPU dispatch");

    constexpr SceneGpuDrivenFeatureDecision available = SceneGpuDrivenFeatureClassifier::Decide(
        indirectRequest,
        SceneGpuDrivenFeatureSupport{
            .computeCullingSupported = true,
            .indirectDrawSupported = true,
            .runtimeGpuDispatchSupported = true,
        });
    Require(available.state == SceneGpuDrivenFeatureState::IndirectDrawSubmit, "GPU-driven classifier did not keep indirect submit when supported");
    Require(available.fallbackReason == SceneGpuDrivenFallbackReason::None, "GPU-driven classifier reported fallback for supported indirect submit");
    Require(!available.UsesFallback(), "GPU-driven classifier marked supported indirect submit as fallback");
}

void RunMeshPipelineAppliesGpuDrivenRuntimeFallbackPolicyTest() {
    RenderMeshResource mesh{};
    mesh.indexCount = 3U;
    mesh.bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 1.0F };
    mesh.sections = {
        RenderMeshSection{
            .indexStart = 0U,
            .indexCount = 3U,
            .bounds = mesh.bounds,
        },
    };
    mesh.meshlets = {
        RenderMeshletDesc{
            .indexStart = 0U,
            .indexCount = 3U,
            .vertexStart = 0U,
            .vertexCount = 3U,
            .sectionIndex = 0U,
            .bounds = mesh.bounds,
        },
    };
    mesh.gpuCullingEnabled = true;
    mesh.indirectDrawsEnabled = true;
    mesh.meshletCullingEnabled = true;

    const std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .instances = { SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U, .model = IdentityMatrix() } },
        },
    };

    MeshPipelineBuildResult result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .drawGroups = &drawGroups,
        .resolvedMeshResource = &mesh,
        .gpuDrivenSupport = SceneGpuDrivenFeatureSupport{
            .computeCullingSupported = true,
            .indirectDrawSupported = true,
        },
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });
    Require(result.stats.gpuDrivenFeatureState == SceneGpuDrivenFeatureState::CpuValidationOnly, "MeshPipeline did not fall back to CPU validation when runtime GPU dispatch is unavailable");
    Require(result.stats.gpuDrivenCounterSource == SceneGpuDrivenCounterSource::CpuCandidates, "MeshPipeline runtime fallback did not mark CPU candidate counters");
    Require(result.stats.gpuDrivenFallbackReason == SceneGpuDrivenFallbackReason::RuntimeGpuDispatchUnavailable, "MeshPipeline did not report runtime GPU dispatch fallback");
    Require(result.stats.gpuDrivenFallbackCount == 1U, "MeshPipeline did not count runtime GPU dispatch fallback");
    Require(result.stats.gpuDrivenParityValidationStatus == SceneGpuDrivenParityValidationStatus::Valid, "MeshPipeline runtime fallback did not run valid parity validation");
    Require(result.stats.gpuDrivenParityValidationCount == 1U, "MeshPipeline runtime fallback did not expose parity validation record count");

    result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .drawGroups = &drawGroups,
        .resolvedMeshResource = &mesh,
        .gpuDrivenSupport = SceneGpuDrivenFeatureSupport{
            .computeCullingSupported = true,
            .indirectDrawSupported = true,
            .meshletSubmitSupported = true,
            .runtimeGpuDispatchSupported = true,
        },
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });
    Require(result.stats.gpuDrivenFeatureState == SceneGpuDrivenFeatureState::MeshletSubmit, "MeshPipeline did not keep meshlet submit when runtime support is complete");
    Require(result.stats.gpuDrivenCounterSource == SceneGpuDrivenCounterSource::GpuDispatchCounters, "MeshPipeline did not mark complete runtime support as GPU counters");
    Require(result.stats.gpuDrivenFallbackReason == SceneGpuDrivenFallbackReason::None, "MeshPipeline reported fallback when runtime support is complete");
}

void RunMeshPipelineParityValidationTracksDroppedBudgetTest() {
    RenderMeshResource mesh{};
    mesh.indexCount = 3U;
    mesh.bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 1.0F };
    mesh.sections = {
        RenderMeshSection{
            .indexStart = 0U,
            .indexCount = 3U,
            .bounds = mesh.bounds,
        },
    };
    mesh.gpuCullingEnabled = true;

    const std::vector<SceneRenderDrawGroup> drawGroups{
        SceneRenderDrawGroup{
            .meshAssetId = 42U,
            .instances = {
                SceneRenderMeshInstance{ .entityId = 1U, .meshAssetId = 42U, .model = IdentityMatrix() },
                SceneRenderMeshInstance{ .entityId = 2U, .meshAssetId = 42U, .model = IdentityMatrix() },
                SceneRenderMeshInstance{ .entityId = 3U, .meshAssetId = 42U, .model = IdentityMatrix() },
            },
        },
    };

    const MeshPipelineBuildResult result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .drawGroups = &drawGroups,
        .resolvedMeshResource = &mesh,
        .maxVisibleInstances = 1U,
        .maxDroppedInstances = 0U,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });

    Require(result.stats.droppedInstanceCount == 2U, "MeshPipeline test setup did not drop two instances");
    Require(result.stats.gpuDrivenInputInstanceCount == 3U, "MeshPipeline did not keep GPU-driven input records for dropped instances");
    Require(result.stats.gpuDrivenParityValidationStatus == SceneGpuDrivenParityValidationStatus::Valid, "MeshPipeline should not enforce dropped budget when it is disabled");
    Require(result.stats.gpuDrivenParityCpuDroppedInstanceCount == 2U, "MeshPipeline parity validation did not count CPU dropped instances");
    Require(result.stats.gpuDrivenParityGpuDroppedInstanceCount == 2U, "MeshPipeline parity validation did not count runtime dropped instances");

    const MeshPipelineBuildResult strictBudgetResult = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque,
        .drawGroups = &drawGroups,
        .resolvedMeshResource = &mesh,
        .maxVisibleInstances = 1U,
        .maxDroppedInstances = 1U,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    });
    Require(strictBudgetResult.stats.gpuDrivenParityValidationStatus == SceneGpuDrivenParityValidationStatus::DroppedInstanceBudgetExceeded, "MeshPipeline parity validation did not enforce dropped instance budget");
    Require(strictBudgetResult.stats.gpuDrivenParityValidationCount == 3U, "MeshPipeline parity validation did not include visible and dropped records");
}

void RunGpuDrivenFeatureClassifierReportsMeshletFallbackTest() {
    constexpr SceneGpuDrivenFeatureRequest meshletRequest{
        .gpuCullingRequested = true,
        .indirectDrawRequested = true,
        .meshletSubmitRequested = true,
    };

    constexpr SceneGpuDrivenFeatureDecision noMeshlet = SceneGpuDrivenFeatureClassifier::Decide(
        meshletRequest,
        SceneGpuDrivenFeatureSupport{
            .computeCullingSupported = true,
            .indirectDrawSupported = true,
            .runtimeGpuDispatchSupported = true,
        });
    Require(noMeshlet.state == SceneGpuDrivenFeatureState::IndirectDrawSubmit, "GPU-driven classifier did not fall back from meshlet submit to indirect submit");
    Require(noMeshlet.fallbackReason == SceneGpuDrivenFallbackReason::MeshletSubmitUnsupported, "GPU-driven classifier did not report missing meshlet submit support");

    constexpr SceneGpuDrivenFeatureDecision noRequest = SceneGpuDrivenFeatureClassifier::Decide(
        SceneGpuDrivenFeatureRequest{},
        SceneGpuDrivenFeatureSupport{
            .computeCullingSupported = true,
            .indirectDrawSupported = true,
            .meshletSubmitSupported = true,
            .runtimeGpuDispatchSupported = true,
        });
    Require(noRequest.state == SceneGpuDrivenFeatureState::Disabled, "GPU-driven classifier enabled a feature without a request");
    Require(noRequest.fallbackReason == SceneGpuDrivenFallbackReason::FeatureNotRequested, "GPU-driven classifier did not report unrequested features distinctly");
    Require(!noRequest.UsesFallback(), "GPU-driven classifier marked an unrequested feature as a quality fallback");
}

void RunGpuDrivenParityValidatorAcceptsMatchingRecordsTest() {
    const std::array<SceneGpuDrivenInstanceValidationRecord, 2U> cpuRecords{{
        SceneGpuDrivenInstanceValidationRecord{
            .entityId = 10U,
            .lodLevel = 0U,
            .firstMeshlet = 0U,
            .meshletCount = 2U,
            .visible = true,
        },
        SceneGpuDrivenInstanceValidationRecord{
            .entityId = 11U,
            .lodLevel = 1U,
            .firstMeshlet = 2U,
            .meshletCount = 1U,
            .visible = false,
            .dropped = true,
        },
    }};
    const std::array<SceneGpuDrivenInstanceValidationRecord, 2U> gpuRecords = cpuRecords;

    const SceneGpuDrivenParityValidationResult result = SceneGpuDrivenParityValidator::Validate(SceneGpuDrivenParityValidationDesc{
        .cpuRecords = cpuRecords,
        .gpuRecords = gpuRecords,
        .droppedInstanceBudget = 1U,
    });

    Require(result.IsValid(), "GPU-driven parity validator rejected matching CPU/GPU records");
    Require(result.cpuDroppedInstanceCount == 1U, "GPU-driven parity validator did not count CPU dropped instances");
    Require(result.gpuDroppedInstanceCount == 1U, "GPU-driven parity validator did not count GPU dropped instances");
}

void RunGpuDrivenParityValidatorReportsCullingLodAndMeshletMismatchesTest() {
    const std::array<SceneGpuDrivenInstanceValidationRecord, 1U> cpuRecords{{
        SceneGpuDrivenInstanceValidationRecord{
            .entityId = 20U,
            .lodLevel = 0U,
            .firstMeshlet = 3U,
            .meshletCount = 2U,
            .visible = true,
        },
    }};

    std::array<SceneGpuDrivenInstanceValidationRecord, 1U> gpuRecords = cpuRecords;
    gpuRecords[0].visible = false;
    SceneGpuDrivenParityValidationResult result = SceneGpuDrivenParityValidator::Validate(SceneGpuDrivenParityValidationDesc{
        .cpuRecords = cpuRecords,
        .gpuRecords = gpuRecords,
    });
    Require(result.status == SceneGpuDrivenParityValidationStatus::VisibilityMismatch, "GPU-driven parity validator did not report visibility mismatch");
    Require(result.entityId == 20U && result.cpuVisible && !result.gpuVisible, "GPU-driven parity visibility mismatch did not preserve debug values");

    gpuRecords = cpuRecords;
    gpuRecords[0].lodLevel = 1U;
    result = SceneGpuDrivenParityValidator::Validate(SceneGpuDrivenParityValidationDesc{
        .cpuRecords = cpuRecords,
        .gpuRecords = gpuRecords,
    });
    Require(result.status == SceneGpuDrivenParityValidationStatus::LodMismatch, "GPU-driven parity validator did not report LOD mismatch");
    Require(result.cpuLodLevel == 0U && result.gpuLodLevel == 1U, "GPU-driven parity LOD mismatch did not preserve LOD values");

    gpuRecords = cpuRecords;
    gpuRecords[0].firstMeshlet = 4U;
    gpuRecords[0].meshletCount = 1U;
    result = SceneGpuDrivenParityValidator::Validate(SceneGpuDrivenParityValidationDesc{
        .cpuRecords = cpuRecords,
        .gpuRecords = gpuRecords,
    });
    Require(result.status == SceneGpuDrivenParityValidationStatus::MeshletRangeMismatch, "GPU-driven parity validator did not report meshlet range mismatch");
    Require(result.cpuFirstMeshlet == 3U && result.gpuFirstMeshlet == 4U, "GPU-driven parity meshlet mismatch did not preserve first meshlet values");
    Require(result.cpuMeshletCount == 2U && result.gpuMeshletCount == 1U, "GPU-driven parity meshlet mismatch did not preserve meshlet count values");
}

void RunGpuDrivenParityValidatorReportsMissingRecordsAndDropBudgetTest() {
    const std::array<SceneGpuDrivenInstanceValidationRecord, 1U> cpuRecords{{
        SceneGpuDrivenInstanceValidationRecord{.entityId = 30U, .visible = true},
    }};
    const std::array<SceneGpuDrivenInstanceValidationRecord, 1U> gpuRecords{{
        SceneGpuDrivenInstanceValidationRecord{.entityId = 31U, .visible = true},
    }};

    SceneGpuDrivenParityValidationResult result = SceneGpuDrivenParityValidator::Validate(SceneGpuDrivenParityValidationDesc{
        .cpuRecords = cpuRecords,
        .gpuRecords = {},
    });
    Require(result.status == SceneGpuDrivenParityValidationStatus::GpuRecordMissing, "GPU-driven parity validator did not report missing GPU record");
    Require(result.entityId == 30U, "GPU-driven parity missing GPU record did not preserve CPU entity id");

    result = SceneGpuDrivenParityValidator::Validate(SceneGpuDrivenParityValidationDesc{
        .cpuRecords = {},
        .gpuRecords = gpuRecords,
    });
    Require(result.status == SceneGpuDrivenParityValidationStatus::CpuRecordMissing, "GPU-driven parity validator did not report missing CPU record");
    Require(result.entityId == 31U, "GPU-driven parity missing CPU record did not preserve GPU entity id");

    const std::array<SceneGpuDrivenInstanceValidationRecord, 2U> droppedRecords{{
        SceneGpuDrivenInstanceValidationRecord{.entityId = 40U, .dropped = true},
        SceneGpuDrivenInstanceValidationRecord{.entityId = 41U, .dropped = true},
    }};
    result = SceneGpuDrivenParityValidator::Validate(SceneGpuDrivenParityValidationDesc{
        .cpuRecords = droppedRecords,
        .gpuRecords = droppedRecords,
        .droppedInstanceBudget = 1U,
    });
    Require(result.status == SceneGpuDrivenParityValidationStatus::DroppedInstanceBudgetExceeded, "GPU-driven parity validator did not report dropped instance budget overflow");
    Require(result.cpuDroppedInstanceCount == 2U && result.gpuDroppedInstanceCount == 2U, "GPU-driven parity drop budget result did not preserve dropped counts");
}

void RunMeshLodRespectsCameraProjectionTest() {
    RenderMeshResource mesh;
    mesh.bounds = RenderBoundsSphere{ .radius = 1.0F };
    mesh.lods = {
        RenderMeshLodDesc{ .minScreenCoverage = 0.15F },
        RenderMeshLodDesc{ .minScreenCoverage = 0.0F },
    };
    RenderScene scene;
    CameraRenderProxyDesc cameraDesc{
        .entityId = 1U, .projection = RenderCameraProjection::Orthographic,
        .orthographicHeight = 10.0F, .primary = true,
    };
    auto select = [&](float depth) {
        static_cast<void>(scene.UpsertCamera(cameraDesc));
        const auto camera = scene.BuildPrimaryCamera(1280U, 720U);
        Require(camera.has_value(), "LOD projection test camera could not be built");
        return MeshPipelineVisibility::SelectLodLevel(&mesh,
            SceneRenderMeshInstance{ .model = TranslationMatrix(0.0F, 0.0F, depth) }, &*camera);
    };
    Require(select(5.0F) == 0U && select(100.0F) == 0U,
        "Orthographic LOD must depend on projected size rather than depth");
    cameraDesc.orthographicHeight = 20.0F;
    Require(select(5.0F) == 1U && select(100.0F) == 1U,
        "Orthographic zoom must change LOD at every depth");
    cameraDesc.projection = RenderCameraProjection::Perspective;
    mesh.lods[0].minScreenCoverage = 0.3F;
    cameraDesc.verticalFovDegrees = 60.0F;
    Require(select(10.0F) == 1U, "Wide perspective should select the distant LOD");
    cameraDesc.verticalFovDegrees = 20.0F;
    Require(select(10.0F) == 0U, "Perspective zoom must promote the enlarged mesh LOD");
    cameraDesc.verticalFovDegrees = 120.0F;
    mesh.lods[0].minScreenCoverage = 0.8F;
    Require(select(0.5F) == 0U, "A perspective camera inside mesh bounds must retain full coverage");
}

void RunRenderScenePropagatesMeshPassFlagsTest() {
    RenderScene renderScene;
    static_cast<void>(renderScene.UpsertMesh(MeshRenderProxyDesc{
        .entityId = 1U,
        .meshAssetId = 42U,
        .castsShadow = false,
        .receivesShadow = false,
    }));

    std::vector<SceneRenderDrawGroup> groups;
    renderScene.BuildDrawGroups(groups);
    Require(groups.size() == 1U && groups[0].instances.size() == 1U, "RenderScene did not build the expected draw group");
    Require(!groups[0].instances[0].castsShadow, "RenderScene did not propagate castsShadow to mesh pipeline instances");
    Require(!groups[0].instances[0].receivesShadow, "RenderScene did not propagate receivesShadow to mesh pipeline instances");
}

// Mesh objects of a scene spread over `combinations` mesh and material pairs, all in front of the camera at depths that
// put them on different levels of a mesh with levels of detail. RenderScene groups them by pair and by blocks of 1024
// entities, as a scene does when it renders.
void FillBenchmarkScene(RenderScene& scene, std::size_t objectCount, std::size_t combinations) {
    for (std::size_t index = 0U; index < objectCount; ++index) {
        const float x = static_cast<float>(index % 100U) - 50.0F;
        const float y = static_cast<float>((index / 100U) % 100U) - 50.0F;
        const float z = 100.0F + static_cast<float>(index % 21U);
        static_cast<void>(scene.UpsertMesh(MeshRenderProxyDesc{
            .entityId = index + 1U,
            .meshAssetId = 1000U + index % combinations,
            .materialAssetId = 2000U + index % combinations,
            .model = TranslationMatrix(x, y, z),
        }));
    }
}

// A mesh of one section, or of three levels of detail with two sections each.
RenderMeshResource BenchmarkMesh(bool levelsOfDetail) {
    RenderMeshResource mesh{};
    mesh.indexCount = 3U;
    mesh.bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 0.5F };
    if (!levelsOfDetail) {
        mesh.sections = { RenderMeshSection{ .indexStart = 0U, .indexCount = 3U, .bounds = mesh.bounds } };
        return mesh;
    }
    const std::array<float, 3> coverage{ 0.0052F, 0.0045F, 0.0F };
    for (std::uint8_t level = 0U; level < 3U; ++level) {
        mesh.lods.push_back(RenderMeshLodDesc{ .firstSection = level * 2U, .sectionCount = 2U, .minScreenCoverage = coverage[level] });
        for (std::uint32_t slot = 0U; slot < 2U; ++slot) {
            mesh.sections.push_back(RenderMeshSection{ .indexStart = 0U, .indexCount = 3U, .materialSlot = slot, .bounds = mesh.bounds, .lodLevel = level });
        }
    }
    return mesh;
}

class HeadlessSurface final : public RenderSurface {
public:
    [[nodiscard]] std::uint32_t Width() const noexcept override { return 64U; }
    [[nodiscard]] std::uint32_t Height() const noexcept override { return 64U; }
    [[nodiscard]] void* NativeWindowHandle() const noexcept override { return nullptr; }
    [[nodiscard]] void* NativeDisplayHandle() const noexcept override { return nullptr; }
};

[[nodiscard]] bool SameBounds(const RenderBoundsSphere& left, const RenderBoundsSphere& right) noexcept {
    return left.center == right.center && left.radius == right.radius;
}

// Whether two builds produced the same draw commands with the same instances, GPU-driven records and counters.
[[nodiscard]] bool SameBuild(const MeshPipelineBuildResult& left, const MeshPipelineBuildResult& right) noexcept {
    const SceneRenderSubmitStats& a = left.stats;
    const SceneRenderSubmitStats& b = right.stats;
    if (left.commands.size() != right.commands.size() || left.gpuDrivenInputRecords.size() != right.gpuDrivenInputRecords.size() ||
        a.visibleMeshCount != b.visibleMeshCount || a.visibleDrawGroupCount != b.visibleDrawGroupCount ||
        a.culledInstanceCount != b.culledInstanceCount || a.droppedInstanceCount != b.droppedInstanceCount ||
        a.lodSelectionCount != b.lodSelectionCount || a.meshCommandLookupCapacity != b.meshCommandLookupCapacity ||
        a.meshPipelineCommandCount != b.meshPipelineCommandCount || a.meshPipelineScratchInstanceCapacity != b.meshPipelineScratchInstanceCapacity ||
        a.meshCachedDrawCommandCount != b.meshCachedDrawCommandCount || a.meshDrawCommandCacheHitCount != b.meshDrawCommandCacheHitCount ||
        a.meshDrawCommandCacheMissCount != b.meshDrawCommandCacheMissCount || a.meshDrawCommandCacheBuildCount != b.meshDrawCommandCacheBuildCount ||
        a.gpuDrivenDrawCandidateCount != b.gpuDrivenDrawCandidateCount || a.indirectDrawCandidateCount != b.indirectDrawCandidateCount ||
        a.meshletCullingCandidateCount != b.meshletCullingCandidateCount || a.gpuDrivenInputInstanceCount != b.gpuDrivenInputInstanceCount ||
        a.gpuDrivenFeatureState != b.gpuDrivenFeatureState || a.missingMeshBindingCount != b.missingMeshBindingCount ||
        a.missingMeshResourceCount != b.missingMeshResourceCount || a.missingMaterialBindingCount != b.missingMaterialBindingCount ||
        a.missingMaterialResourceCount != b.missingMaterialResourceCount || a.missingTextureBindingCount != b.missingTextureBindingCount ||
        a.missingTextureResourceCount != b.missingTextureResourceCount || a.textureDimensionMismatchCount != b.textureDimensionMismatchCount) {
        return false;
    }
    for (std::size_t index = 0U; index < left.commands.size(); ++index) {
        const MeshDrawCommand& x = left.commands[index];
        const MeshDrawCommand& y = right.commands[index];
        if (x.pass != y.pass || x.meshAssetId != y.meshAssetId || x.materialAssetId != y.materialAssetId ||
            x.sectionIndex != y.sectionIndex || x.materialSlot != y.materialSlot || x.indexStart != y.indexStart ||
            x.indexCount != y.indexCount || x.vertexStart != y.vertexStart || x.vertexCount != y.vertexCount ||
            x.lodLevel != y.lodLevel || x.depthBucket != y.depthBucket || x.mesh != y.mesh || x.material != y.material ||
            x.meshResource != y.meshResource || x.materialResource != y.materialResource || x.state != y.state ||
            x.sortKey != y.sortKey || x.currentSkinningPalette != y.currentSkinningPalette ||
            x.gpuFrustumCull != y.gpuFrustumCull || x.instances.size() != y.instances.size()) {
            return false;
        }
        for (std::size_t instance = 0U; instance < x.instances.size(); ++instance) {
            const SceneRenderMeshInstance& p = x.instances[instance];
            const SceneRenderMeshInstance& q = y.instances[instance];
            if (p.entityId != q.entityId || p.meshAssetId != q.meshAssetId || p.materialAssetId != q.materialAssetId ||
                p.model != q.model || !SameBounds(p.worldBounds, q.worldBounds) || p.depthBucket != q.depthBucket) {
                return false;
            }
        }
    }
    for (std::size_t index = 0U; index < left.gpuDrivenInputRecords.size(); ++index) {
        const SceneGpuDrivenInputRecord& x = left.gpuDrivenInputRecords[index];
        const SceneGpuDrivenInputRecord& y = right.gpuDrivenInputRecords[index];
        if (x.entityId != y.entityId || x.worldBounds != y.worldBounds || x.drawCommandIndex != y.drawCommandIndex ||
            x.lodLevel != y.lodLevel || x.firstMeshlet != y.firstMeshlet || x.meshletCount != y.meshletCount) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool SameDiagnostics(const SceneRenderDiagnostics& left, const SceneRenderDiagnostics& right) noexcept {
    if (left.events.size() != right.events.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.events.size(); ++index) {
        const SceneRenderDiagnosticEvent& x = left.events[index];
        const SceneRenderDiagnosticEvent& y = right.events[index];
        if (x.severity != y.severity || x.kind != y.kind || x.entityId != y.entityId || x.meshAssetId != y.meshAssetId ||
            x.materialAssetId != y.materialAssetId || x.textureAssetId != y.textureAssetId || x.instanceCount != y.instanceCount) {
            return false;
        }
    }
    return true;
}

// Builds a pass twice into one result on the calling thread and twice into another with workers, and requires the
// same commands, counters and diagnostics each time: the second build reuses what the first left in the results.
// `expectScan` says whether the workers scan chunks that are written into the commands.
template <typename MakeDesc>
void RequireParallelBuildMatchesSerial(kb::ecs::WorkerPool& pool, MakeDesc&& makeDesc, bool expectScan, const char* message) {
    MeshPipelineBuildResult serial;
    MeshPipelineBuildResult parallel;
    for (int build = 0; build < 2; ++build) {
        SceneRenderDiagnostics serialDiagnostics;
        SceneRenderDiagnostics parallelDiagnostics;
        MeshPipelineProcessor::BuildInto(makeDesc(nullptr, &serialDiagnostics), serial);
        MeshPipelineProcessor::BuildInto(makeDesc(&pool, &parallelDiagnostics), parallel);
        Require(SameBuild(serial, parallel) && SameDiagnostics(serialDiagnostics, parallelDiagnostics), message);
        Require(serial.scanChunks.empty(), "A build without workers scanned chunks");
        const bool scanned = std::ranges::any_of(parallel.scanChunks, [](const MeshPipelineScanChunk& chunk) { return chunk.clean; });
        Require(scanned == expectScan, "The workers did not scan the pass as the test expects");
    }
}

// Builds `passCount` passes twice each, together into results of their own with workers and one by one on the calling
// thread, and requires the same commands, counters and diagnostics for every pass each time: the second build reuses what
// the first left in the results. `makeDesc(index, workers, diagnostics)` describes one pass.
template <typename MakeDesc>
void RequireTogetherBuildMatchesSerial(kb::ecs::WorkerPool& pool, MakeDesc&& makeDesc, std::size_t passCount, const char* message) {
    std::vector<MeshPipelineBuildResult> serial(passCount);
    std::vector<MeshPipelineBuildResult> together(passCount);
    for (int build = 0; build < 2; ++build) {
        std::vector<SceneRenderDiagnostics> serialDiagnostics(passCount);
        std::vector<SceneRenderDiagnostics> togetherDiagnostics(passCount);
        std::vector<MeshPipelinePassBuild> passes;
        for (std::size_t index = 0U; index < passCount; ++index) {
            MeshPipelineProcessor::BuildInto(makeDesc(index, nullptr, &serialDiagnostics[index]), serial[index]);
            passes.push_back(MeshPipelinePassBuild{ .desc = makeDesc(index, &pool, &togetherDiagnostics[index]), .result = &together[index] });
        }
        MeshPipelineProcessor::BuildPassesInto(passes);
        for (std::size_t index = 0U; index < passCount; ++index) {
            Require(SameBuild(serial[index], together[index]) && SameDiagnostics(serialDiagnostics[index], togetherDiagnostics[index]), message);
        }
    }
}

// Objects of every kind a pass tells apart: layers, shadow casting, slot overrides, and positions in and out of the
// frustum, spread over enough batches for the workers to share.
void FillMixedScene(RenderScene& scene, std::size_t objectCount) {
    for (std::size_t index = 0U; index < objectCount; ++index) {
        MeshRenderProxyDesc proxy{
            .entityId = index + 1U,
            .meshAssetId = 1000U + index % 3U,
            .materialAssetId = 2000U + (index / 3U) % 2U,
            .model = TranslationMatrix(static_cast<float>(index % 100U) - 50.0F + (index % 7U == 0U ? 90.0F : 0.0F),
                static_cast<float>((index / 100U) % 100U) - 50.0F, 100.0F + static_cast<float>(index % 21U)),
            .castsShadow = index % 5U != 0U,
            .layer = index % 11U == 0U ? 4U : 1U,
        };
        if (index % 13U == 0U) {
            proxy.materialSlotAssetIds[0] = 3000U + index % 3U;
            proxy.materialSlotOverrideCount = 1U;
        }
        static_cast<void>(scene.UpsertMesh(proxy));
    }
}

void RunMeshPipelineParallelScanMatchesSerialTest() {
    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .workerCount = 8U } };
    pool.Start(kb::ecs::WorkerPoolConfig{ .workerCount = 8U });
    RenderScene scene;
    FillMixedScene(scene, 6000U);
    const std::vector<SceneMeshBatch> batches = SceneMeshBatchBuilder::Build(scene.DrawGroups());
    Require(batches.size() > 6U, "The parallel scan test scene has too few batches to share");

    RenderMeshResource mesh = BenchmarkMesh(true);
    RenderMeshResource gpuMesh = mesh;
    gpuMesh.gpuCullingEnabled = true;
    RenderMaterialResource opaque{};
    RenderMaterialResource blended{};
    blended.alphaMode = RenderMaterialAlphaMode::Blend;
    auto camera = PerspectiveCamera();
    SceneDepthPolicy::MakePerspective(camera.projection.data(), 90.0F, 1.0F, 0.1F, 1000.0F, SceneDepthPolicy::HomogeneousDepth());
    camera.cullingMask = 0x3U;
    std::vector<std::uint64_t> selected;
    for (std::uint64_t entity = 1U; entity <= 6000U; entity += 3U) selected.push_back(entity);

    const auto require = [&](MeshPassType pass, const RenderMeshResource& meshResource, const RenderMaterialResource& material,
                             std::uint32_t maxDrawCommands, std::uint32_t maxVisibleInstances, const char* message) {
        RequireParallelBuildMatchesSerial(pool, [&](kb::ecs::WorkerPool* workerPool, SceneRenderDiagnostics* diagnostics) {
            return MeshPipelineBuildDesc{
                .pass = pass,
                .meshBatches = &batches,
                .resolvedMeshResource = &meshResource,
                .resolvedMaterialResource = &material,
                .camera = &camera,
                .diagnostics = diagnostics,
                .maxDrawCommands = maxDrawCommands,
                .maxVisibleInstances = maxVisibleInstances,
                .selectedEntityIds = selected,
                .resourceValidation = MeshPipelineResourceValidation::Skip,
                .workerPool = workerPool,
            };
        }, true, message);
    };
    require(MeshPassType::BaseOpaque, mesh, opaque, 0U, 0U, "Workers built the opaque pass differently from the calling thread");
    require(MeshPassType::BaseOpaque, gpuMesh, opaque, 0U, 0U, "Workers built the GPU-driven candidates differently from the calling thread");
    require(MeshPassType::ShadowDepth, mesh, opaque, 0U, 0U, "Workers built the shadow pass differently from the calling thread");
    require(MeshPassType::SelectionId, mesh, opaque, 0U, 0U, "Workers built the selection pass differently from the calling thread");
    require(MeshPassType::BaseTransparent, mesh, blended, 0U, 0U, "Workers built the transparent pass differently from the calling thread");
    require(MeshPassType::BaseOpaque, mesh, opaque, 5U, 0U, "Workers did not drop to the draw command budget as the calling thread does");
    require(MeshPassType::BaseOpaque, gpuMesh, opaque, 0U, 4000U, "Workers did not drop to the instance budget as the calling thread does");
}

// Passes built together, of every pass kind, with cameras that see different objects, budgets and masks, in scenes too
// small for any worker, too small for a pass to be scanned in chunks but large together, and large.
void RunMeshPipelineBuildsPassesTogetherMatchesSerialTest() {
    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .workerCount = 8U } };
    pool.Start(kb::ecs::WorkerPoolConfig{ .workerCount = 8U });
    RenderMeshResource mesh = BenchmarkMesh(true);
    RenderMeshResource gpuMesh = mesh;
    gpuMesh.gpuCullingEnabled = true;
    const RenderMaterialResource opaque{};
    std::array<SceneRenderCamera, 4> cameras{};
    for (std::size_t index = 0U; index < cameras.size(); ++index) {
        cameras[index] = PerspectiveCamera();
        SceneDepthPolicy::MakePerspective(cameras[index].projection.data(), 90.0F, 1.0F, 0.1F, 1000.0F, SceneDepthPolicy::HomogeneousDepth());
        cameras[index].view[12] = 40.0F * static_cast<float>(index) - 60.0F;
        cameras[index].cullingMask = index == 3U ? 0x1U : 0x3U;
    }
    for (const std::size_t objectCount : { std::size_t{ 100U }, std::size_t{ 700U }, std::size_t{ 6000U } }) {
        RenderScene scene;
        FillMixedScene(scene, objectCount);
        const std::vector<SceneMeshBatch> batches = SceneMeshBatchBuilder::Build(scene.DrawGroups());
        std::vector<std::uint64_t> selected;
        for (std::uint64_t entity = 1U; entity <= objectCount; entity += 3U) selected.push_back(entity);
        const std::array<MeshPassType, 4> passKinds{ MeshPassType::ShadowDepth, MeshPassType::ShadowDepth, MeshPassType::BaseOpaque, MeshPassType::SelectionId };
        for (const RenderMeshResource* meshResource : { &mesh, &gpuMesh }) {
            RequireTogetherBuildMatchesSerial(pool, [&](std::size_t index, kb::ecs::WorkerPool* workers, SceneRenderDiagnostics* diagnostics) {
                return MeshPipelineBuildDesc{
                    .pass = passKinds[index],
                    .meshBatches = &batches,
                    .resolvedMeshResource = meshResource,
                    .resolvedMaterialResource = &opaque,
                    .camera = &cameras[index],
                    .diagnostics = diagnostics,
                    .maxVisibleInstances = index == 1U ? static_cast<std::uint32_t>(objectCount / 2U) : 0U,
                    .selectedEntityIds = passKinds[index] == MeshPassType::SelectionId ? std::span<const std::uint64_t>{ selected } : std::span<const std::uint64_t>{},
                    .resourceValidation = MeshPipelineResourceValidation::Skip,
                    .workerPool = workers,
                };
            }, passKinds.size(), "Passes built together differ from the same passes built one by one");
        }
    }
}

// ---- Visibility index of the draw groups ------------------------------------------------------------------------

// Pseudo-random numbers that every platform draws the same.
class IndexTestRandom {
public:
    explicit IndexTestRandom(std::uint32_t seed) noexcept : state_(seed) {}
    [[nodiscard]] std::uint32_t Next() noexcept {
        state_ = state_ * 1664525U + 1013904223U;
        return state_ >> 8U;
    }
    [[nodiscard]] float Unit() noexcept { return static_cast<float>(Next() & 0xFFFFU) / 65536.0F; }
    [[nodiscard]] float Range(float low, float high) noexcept { return low + (high - low) * Unit(); }
    [[nodiscard]] std::uint32_t Below(std::uint32_t limit) noexcept { return Next() % limit; }

private:
    std::uint32_t state_;
};

// A model of random rotation, size and place, sheared when `shear` is set.
std::array<float, 16> RandomIndexModel(IndexTestRandom& random, float spread, float minimumScale, float maximumScale, bool shear) {
    const float a = random.Range(0.0F, 6.2831853F);
    const float b = random.Range(0.0F, 6.2831853F);
    const float c = random.Range(0.0F, 6.2831853F);
    const float ca = std::cos(a), sa = std::sin(a), cb = std::cos(b), sb = std::sin(b), cc = std::cos(c), sc = std::sin(c);
    const std::array<float, 9> rotation{
        cc * cb, sc * cb, -sb,
        cc * sb * sa - sc * ca, sc * sb * sa + cc * ca, cb * sa,
        cc * sb * ca + sc * sa, sc * sb * ca - cc * sa, cb * ca,
    };
    std::array<float, 16> model = IdentityMatrix();
    for (std::size_t column = 0U; column < 3U; ++column) {
        const float scale = random.Range(minimumScale, maximumScale);
        for (std::size_t row = 0U; row < 3U; ++row) model[column * 4U + row] = rotation[column * 3U + row] * scale;
    }
    if (shear) {
        model[4] += random.Range(-1.0F, 1.0F);
        model[9] += random.Range(-1.0F, 1.0F);
    }
    model[12] = random.Range(-spread, spread);
    model[13] = random.Range(-spread, spread);
    model[14] = random.Range(-spread, spread);
    return model;
}

// An object of every kind a pass tells apart: layers, shadow casting, and bounds of its own now and then.
MeshRenderProxyDesc RandomIndexProxy(IndexTestRandom& random, std::uint64_t entityId, float spread) {
    constexpr std::array<std::uint32_t, 6> layers{ 1U, 1U, 1U, 2U, 4U, 6U };
    MeshRenderProxyDesc proxy{
        .entityId = entityId,
        .meshAssetId = 500U + entityId % 2U,
        .materialAssetId = 600U + (entityId / 2U) % 2U,
        .model = RandomIndexModel(random, spread, 0.3F, 2.5F, random.Below(8U) == 0U),
        .castsShadow = random.Below(5U) != 0U,
        .receivesShadow = random.Below(3U) != 0U,
        .shadowOnly = random.Below(40U) == 0U,
        .layer = layers[random.Below(6U)],
    };
    if (random.Below(16U) == 0U) {
        proxy.boundsOverride = RenderBoundsSphere{
            .center = { random.Range(-1.0F, 1.0F), random.Range(-1.0F, 1.0F), random.Range(-1.0F, 1.0F) },
            .radius = random.Range(0.3F, 2.0F),
        };
    }
    return proxy;
}

void FillRandomIndexScene(RenderScene& scene, IndexTestRandom& random, std::size_t count, std::uint64_t firstEntity, std::uint64_t step, float spread) {
    for (std::size_t index = 0U; index < count; ++index) {
        static_cast<void>(scene.UpsertMesh(RandomIndexProxy(random, firstEntity + index * step, spread)));
    }
}

// Cameras of a scene that spreads 60 around the origin: from far and near, perspective and orthographic, with and
// without a guard band, and taking some layers only. The last two see all of it and none of it.
std::vector<SceneRenderCamera> IndexTestCameras(IndexTestRandom& random, std::size_t count) {
    constexpr std::array<std::uint32_t, 5> masks{ 0xFFFFFFFFU, 1U, 4U, 6U, 0U };
    constexpr std::array<float, 5> farClips{ 6.0F, 25.0F, 90.0F, 400.0F, 4000.0F };
    std::vector<SceneRenderCamera> cameras;
    for (std::size_t index = 0U; index < count; ++index) {
        SceneRenderCamera camera{};
        const bx::Vec3 eye{ random.Range(-90.0F, 90.0F), random.Range(-90.0F, 90.0F), random.Range(-90.0F, 90.0F) };
        const bx::Vec3 target{ random.Range(-30.0F, 30.0F), random.Range(-30.0F, 30.0F), random.Range(-30.0F, 30.0F) };
        bx::mtxLookAt(camera.view.data(), eye, target, bx::Vec3{ 0.0F, 1.0F, 0.0F });
        const float farClip = farClips[random.Below(5U)];
        if (index % 4U == 3U) {
            SceneDepthPolicy::MakeOrthographic(camera.projection.data(), random.Range(5.0F, 120.0F), random.Range(0.5F, 2.0F), 0.1F, farClip,
                SceneDepthPolicy::HomogeneousDepth());
        } else {
            SceneDepthPolicy::MakePerspective(camera.projection.data(), random.Range(20.0F, 130.0F), random.Range(0.5F, 2.0F), 0.1F, farClip,
                SceneDepthPolicy::HomogeneousDepth());
        }
        if (index % 3U == 0U) {
            camera.cullingGuardBand = { 0.04F, 0.04F };
            camera.temporalProjectionOffset = { 0.002F, -0.003F };
        }
        camera.cullingMask = masks[index % masks.size()];
        cameras.push_back(camera);
    }
    SceneRenderCamera seesAll{};
    bx::mtxLookAt(seesAll.view.data(), bx::Vec3{ 0.0F, 0.0F, -300.0F }, bx::Vec3{ 0.0F, 0.0F, 0.0F }, bx::Vec3{ 0.0F, 1.0F, 0.0F });
    SceneDepthPolicy::MakeOrthographic(seesAll.projection.data(), 600.0F, 1.0F, 0.1F, 4000.0F, SceneDepthPolicy::HomogeneousDepth());
    cameras.push_back(seesAll);
    SceneRenderCamera seesNone = seesAll;
    bx::mtxLookAt(seesNone.view.data(), bx::Vec3{ 0.0F, 0.0F, 1000.0F }, bx::Vec3{ 0.0F, 0.0F, 2000.0F }, bx::Vec3{ 0.0F, 1.0F, 0.0F });
    cameras.push_back(seesNone);
    return cameras;
}

// Meshes that put a scan through each of its paths: bounds off the origin, sections without bounds of their own, one level
// of detail and a chain of them.
RenderMeshResource IndexTestMesh(std::size_t variant) {
    RenderMeshResource mesh{};
    mesh.indexCount = 3U;
    switch (variant) {
    case 0U:
        mesh.bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 0.9F };
        mesh.sections = { RenderMeshSection{ .indexStart = 0U, .indexCount = 3U, .bounds = mesh.bounds } };
        break;
    case 1U:
        mesh.bounds = RenderBoundsSphere{ .center = { 0.7F, -0.4F, 1.1F }, .radius = 0.6F };
        mesh.sections = {
            RenderMeshSection{ .indexStart = 0U, .indexCount = 3U, .materialSlot = 0U },
            RenderMeshSection{ .indexStart = 0U, .indexCount = 3U, .materialSlot = 1U,
                .bounds = RenderBoundsSphere{ .center = { -1.0F, 0.5F, 0.0F }, .radius = 1.4F } },
        };
        break;
    case 2U:
        mesh.bounds = RenderBoundsSphere{ .center = { 0.0F, 0.3F, 0.0F }, .radius = 1.2F };
        mesh.sections = { RenderMeshSection{ .indexStart = 0U, .indexCount = 3U, .bounds = mesh.bounds, .lodLevel = 0U } };
        mesh.lods = { RenderMeshLodDesc{ .firstSection = 0U, .sectionCount = 1U, .minScreenCoverage = 0.0F } };
        break;
    default:
        mesh.bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 1.0F };
        for (std::uint8_t level = 0U; level < 2U; ++level) {
            mesh.lods.push_back(RenderMeshLodDesc{ .firstSection = level, .sectionCount = 1U, .minScreenCoverage = level == 0U ? 0.02F : 0.0F });
            mesh.sections.push_back(RenderMeshSection{ .indexStart = 0U, .indexCount = 3U, .bounds = mesh.bounds, .lodLevel = level });
        }
        break;
    }
    return mesh;
}

// Opaque; reaching beyond its objects, as a material that moves vertices does; and blended.
std::array<RenderMaterialResource, 3> IndexTestMaterials() {
    std::array<RenderMaterialResource, 3> materials{};
    materials[1].graphProgram.active = true;
    materials[1].graphProgram.boundsMargin = 3.0F;
    materials[2].alphaMode = RenderMaterialAlphaMode::Blend;
    return materials;
}

constexpr std::array<MeshPassType, 8> kIndexTestPasses{ MeshPassType::BaseOpaque, MeshPassType::Depth, MeshPassType::GBuffer,
    MeshPassType::BaseTransparent, MeshPassType::ShadowDepth, MeshPassType::MotionVectors, MeshPassType::SelectionId, MeshPassType::Gizmo };

// Builds `groups`, and the same groups without their visibility index, for every pass with every camera and without one,
// and requires the same draw commands, counters and diagnostics. `makeDesc(pass, camera, source, diagnostics, selected,
// motionFrame, step)` describes a build; `step` rotates meshes and materials through the combinations. Returns the number of
// builds in which the frustum turned instances away.
template <typename MakeDesc>
std::size_t RequireIndexMatchesScan(const std::vector<SceneRenderDrawGroup>& groups, std::span<const SceneRenderCamera> cameras,
    MakeDesc&& makeDesc, const char* message) {
    std::vector<SceneRenderDrawGroup> reference = groups;
    for (SceneRenderDrawGroup& group : reference) group.visibility = {};
    std::vector<std::uint64_t> selected;
    for (const SceneRenderDrawGroup& group : groups) {
        for (std::size_t index = 0U; index < group.instances.size(); index += 7U) selected.push_back(group.instances[index].entityId);
    }
    std::size_t turnedAway = 0U;
    for (std::size_t cameraIndex = 0U; cameraIndex <= cameras.size(); ++cameraIndex) {
        const SceneRenderCamera* camera = cameraIndex == cameras.size() ? nullptr : &cameras[cameraIndex];
        for (std::size_t passIndex = 0U; passIndex < kIndexTestPasses.size(); ++passIndex) {
            const MeshPassType pass = kIndexTestPasses[passIndex];
            const std::uint64_t motionFrame = pass == MeshPassType::MotionVectors && cameraIndex % 2U == 1U ? 5U : 0U;
            SceneRenderDiagnostics acceleratedDiagnostics;
            SceneRenderDiagnostics referenceDiagnostics;
            const MeshPipelineBuildResult accelerated = MeshPipelineProcessor::Build(
                makeDesc(pass, camera, groups, acceleratedDiagnostics, selected, motionFrame, cameraIndex + passIndex));
            const MeshPipelineBuildResult original = MeshPipelineProcessor::Build(
                makeDesc(pass, camera, reference, referenceDiagnostics, selected, motionFrame, cameraIndex + passIndex));
            Require(SameBuild(accelerated, original) && SameDiagnostics(acceleratedDiagnostics, referenceDiagnostics), message);
            turnedAway += accelerated.stats.culledInstanceCount != 0U ? 1U : 0U;
        }
    }
    return turnedAway;
}

// Meshes and materials of the builds that skip resource validation, in every combination a build can take.
struct IndexTestResources {
    std::array<RenderMeshResource, 4> meshes{ IndexTestMesh(0U), IndexTestMesh(1U), IndexTestMesh(2U), IndexTestMesh(3U) };
    std::array<RenderMaterialResource, 3> materials = IndexTestMaterials();
};

std::size_t RequireIndexMatchesScan(const std::vector<SceneRenderDrawGroup>& groups, std::span<const SceneRenderCamera> cameras, const char* message) {
    const IndexTestResources resources;
    return RequireIndexMatchesScan(groups, cameras,
        [&resources](MeshPassType pass, const SceneRenderCamera* camera, const std::vector<SceneRenderDrawGroup>& source,
            SceneRenderDiagnostics& diagnostics, std::span<const std::uint64_t> selected, std::uint64_t motionFrame, std::size_t step) {
            return MeshPipelineBuildDesc{
                .pass = pass,
                .drawGroups = &source,
                .resolvedMeshResource = &resources.meshes[step % resources.meshes.size()],
                .resolvedMaterialResource = &resources.materials[(2U * step + 1U) % resources.materials.size()],
                .camera = camera,
                .diagnostics = &diagnostics,
                .selectedEntityIds = selected,
                .resourceValidation = MeshPipelineResourceValidation::Skip,
                .motionFrame = motionFrame,
            };
        }, message);
}

// What the index holds of a group is what the group holds: every instance has an entry with its facts, in a cluster whose
// bounds enclose it, and the bounds of the clusters add up to those of the group.
void RequireIndexDescribesInstances(std::span<const SceneRenderMeshInstance> instances, const SceneMeshVisibilityIndex& index, const char* message) {
    if (index.Empty()) {
        Require(instances.size() < SceneMeshVisibilityIndexBuilder::kMinimumInstances && index.slots.empty(), message);
        return;
    }
    constexpr std::uint32_t capacity = SceneMeshVisibilityIndex::kClusterCapacity;
    Require(index.slots.size() == instances.size() && index.entries.size() == index.clusters.size() * capacity &&
            index.root.count == instances.size(), message);
    std::size_t counted = 0U;
    std::array<std::uint32_t, 4> rootKinds{};
    for (std::size_t clusterIndex = 0U; clusterIndex < index.clusters.size(); ++clusterIndex) {
        const SceneMeshVisibilityIndex::Cluster& cluster = index.clusters[clusterIndex];
        Require(cluster.count >= 1U && cluster.count <= capacity && cluster.stale == 0U, message);
        counted += cluster.count;
        std::array<std::uint32_t, 4> kinds{};
        std::uint32_t layersAny = 0U;
        std::uint32_t layersAll = 0xFFFFFFFFU;
        for (std::uint32_t member = 0U; member < cluster.count; ++member) {
            const std::size_t slot = clusterIndex * capacity + member;
            const SceneMeshVisibilityIndex::Entry& entry = index.entries[slot];
            Require(entry.instance < instances.size() && index.slots[entry.instance] == slot, message);
            const SceneRenderMeshInstance& instance = instances[entry.instance];
            const std::uint8_t kind = static_cast<std::uint8_t>((instance.castsShadow ? 1U : 0U) | (instance.shadowOnly ? 2U : 0U));
            Require(entry.layer == instance.layer && entry.kind == kind, message);
            ++kinds[kind];
            layersAny |= entry.layer;
            layersAll &= entry.layer;
            if (!entry.unbounded) {
                Require(entry.origin == std::array<float, 3>{ instance.model[12], instance.model[13], instance.model[14] }, message);
                for (std::size_t axis = 0U; axis < 3U; ++axis) {
                    Require(std::abs(entry.origin[axis] - cluster.center[axis]) <= cluster.halfExtent[axis] * 1.0001F + 1.0e-3F, message);
                }
                Require(entry.scale <= cluster.maximumScale && entry.overrideReach <= cluster.overrideReach, message);
            } else {
                Require(cluster.unbounded, message);
            }
        }
        Require(kinds == cluster.kindCount && layersAny == cluster.layersAny && layersAll == cluster.layersAll, message);
        for (std::size_t kind = 0U; kind < kinds.size(); ++kind) rootKinds[kind] += kinds[kind];
        if (!cluster.unbounded) {
            for (std::size_t axis = 0U; axis < 3U; ++axis) {
                Require(cluster.center[axis] - cluster.halfExtent[axis] >= index.root.center[axis] - index.root.halfExtent[axis] - 1.0e-3F &&
                        cluster.center[axis] + cluster.halfExtent[axis] <= index.root.center[axis] + index.root.halfExtent[axis] + 1.0e-3F, message);
            }
        }
    }
    Require(counted == instances.size() && rootKinds == index.root.kindCount, message);
}

// The groups made of mesh proxies are indexed from the size the index is worth.
void RequireIndexDescribesGroups(const std::vector<SceneRenderDrawGroup>& groups, const char* message) {
    for (const SceneRenderDrawGroup& group : groups) {
        RequireIndexDescribesInstances(group.instances, group.visibility, message);
        Require(!group.visibility.Empty() || group.instances.size() < SceneMeshVisibilityIndexBuilder::kMinimumInstances, message);
    }
}

// The answers of the box and of the point against the frustum never rule out an instance IsInsideFrustum lets through, nor
// say inside of one it turns away, whatever the frustum, the box and the reach.
void RunVisibilityBoundsClassificationIsConservativeTest() {
    IndexTestRandom random(7U);
    std::vector<SceneRenderCamera> cameras = IndexTestCameras(random, 60U);
    // A camera that sees to a very far plane.
    SceneRenderCamera distant{};
    distant.view = IdentityMatrix();
    SceneDepthPolicy::MakePerspective(distant.projection.data(), 60.0F, 1.0F, 0.1F, 1.0e6F, SceneDepthPolicy::HomogeneousDepth());
    cameras.push_back(distant);
    std::size_t outside = 0U;
    std::size_t inside = 0U;
    std::size_t partial = 0U;
    for (const SceneRenderCamera& camera : cameras) {
        const MeshPipelineFrustum frustum = MeshPipelineVisibility::BuildFrustum(&camera);
        for (int boxIndex = 0; boxIndex < 300; ++boxIndex) {
            SceneMeshVisibilityBounds bounds;
            bounds.center = { random.Range(-120.0F, 120.0F), random.Range(-120.0F, 120.0F), random.Range(-120.0F, 120.0F) };
            // Boxes of every shape, among them slabs that are long in one direction.
            const float side = random.Range(0.01F, 60.0F);
            bounds.halfExtent = { side * random.Range(0.01F, 1.0F), side * random.Range(0.01F, 1.0F), side * random.Range(0.01F, 1.0F) };
            bounds.halfExtent[random.Below(3U)] = side * 3.0F;
            // Instances have bounds of a size above nothing, which a frustum would never turn away.
            const float reach = random.Below(4U) == 0U ? 0.01F : random.Range(0.01F, 6.0F);
            const MeshPipelineBoundsVisibility visibility = MeshPipelineVisibility::ClassifyBounds(frustum, bounds, reach);
            outside += visibility == MeshPipelineBoundsVisibility::Outside ? 1U : 0U;
            inside += visibility == MeshPipelineBoundsVisibility::Inside ? 1U : 0U;
            partial += visibility == MeshPipelineBoundsVisibility::Partial ? 1U : 0U;
            for (int sample = 0; sample < 40; ++sample) {
                // An instance whose origin is in the box and whose bounds reach no farther than `reach` from it.
                std::array<float, 3> origin{};
                for (std::size_t axis = 0U; axis < 3U; ++axis) {
                    const float unit = sample < 8 ? (((sample >> axis) & 1) != 0 ? 1.0F : -1.0F) : random.Range(-1.0F, 1.0F);
                    origin[axis] = bounds.center[axis] + unit * bounds.halfExtent[axis];
                }
                const float share = random.Unit();
                const RenderBoundsSphere sphere{
                    .center = { origin[0] + reach * share * 0.5F, origin[1] - reach * share * 0.5F, origin[2] + reach * share * 0.5F },
                    .radius = reach * (1.0F - share),
                };
                Require(sphere.IsValid(), "The classification test made an instance without bounds");
                const bool lets = MeshPipelineVisibility::IsInsideFrustum(frustum, sphere);
                Require(visibility != MeshPipelineBoundsVisibility::Outside || !lets, "A box said outside holds an instance the frustum lets through");
                Require(visibility != MeshPipelineBoundsVisibility::Inside || lets, "A box said inside holds an instance the frustum turns away");
                Require(!MeshPipelineVisibility::IsReachOutsideFrustum(frustum, origin, reach) || !lets,
                    "A point said outside is an instance the frustum lets through");
            }
        }
    }
    Require(outside > 1000U && inside > 100U && partial > 1000U, "The classification test did not meet every answer");
    // Without a frustum nothing is ruled out, nor with an instance that has no bounds.
    const MeshPipelineFrustum none = MeshPipelineVisibility::BuildFrustum(nullptr);
    SceneMeshVisibilityBounds bounds;
    bounds.center = { 1.0e6F, 0.0F, 0.0F };
    Require(MeshPipelineVisibility::ClassifyBounds(none, bounds, 1.0F) == MeshPipelineBoundsVisibility::Partial &&
            !MeshPipelineVisibility::IsReachOutsideFrustum(none, bounds.center, 1.0F), "A frustum that is not there ruled instances out");
    bounds.unbounded = true;
    Require(MeshPipelineVisibility::ClassifyBounds(MeshPipelineVisibility::BuildFrustum(&cameras[0]), bounds, 1.0F) == MeshPipelineBoundsVisibility::Partial,
        "A box with an instance of no bounds ruled instances out");
}

// Instances come, go, move and change in any order: the index describes them after every step, and after the instances
// have drifted far from where it placed them, it places them again.
void RunVisibilityIndexStaysInStepWithInstancesTest() {
    IndexTestRandom random(11U);
    std::vector<SceneRenderMeshInstance> instances;
    SceneMeshVisibilityIndex index;
    const auto makeInstance = [&random](float spread) {
        SceneRenderMeshInstance instance;
        const MeshRenderProxyDesc proxy = RandomIndexProxy(random, 1U, spread);
        instance.model = proxy.model;
        instance.castsShadow = proxy.castsShadow;
        instance.shadowOnly = proxy.shadowOnly;
        instance.layer = proxy.layer;
        instance.boundsOverride = proxy.boundsOverride;
        return instance;
    };
    const auto append = [&] {
        instances.push_back(makeInstance(60.0F));
        SceneMeshVisibilityIndexBuilder::Append(instances, index);
    };
    const auto remove = [&] {
        const std::uint32_t position = random.Below(static_cast<std::uint32_t>(instances.size()));
        SceneMeshVisibilityIndexBuilder::Remove(index, position);
        if (position + 1U != instances.size()) instances[position] = std::move(instances.back());
        instances.pop_back();
    };
    const auto update = [&](bool jump) {
        const std::uint32_t position = random.Below(static_cast<std::uint32_t>(instances.size()));
        if (!jump && random.Below(2U) == 0U) {
            // A nudge, which mostly leaves the instance within its cluster.
            instances[position].model[12] += random.Range(-0.05F, 0.05F);
            instances[position].model[14] += random.Range(-0.05F, 0.05F);
        } else {
            instances[position] = makeInstance(jump ? 600.0F : 60.0F);
        }
        SceneMeshVisibilityIndexBuilder::Update(index, instances[position], position);
    };
    const auto check = [&](const char* message) {
        SceneMeshVisibilityIndexBuilder::Refresh(instances, index);
        RequireIndexDescribesInstances(instances, index, message);
    };
    // Growing past the size that is indexed, to many clusters.
    for (int step = 0; step < 700; ++step) {
        append();
        if (step % 29 == 0 || step == 31 || step == 32) check("The index of a growing group does not describe it");
    }
    Require(!index.Empty(), "A group of many instances was not indexed");
    // Mixed changes.
    for (int step = 0; step < 4000; ++step) {
        const std::uint32_t kind = random.Below(100U);
        if (kind < 35U) append();
        else if (kind < 60U && instances.size() > 40U) remove();
        else update(random.Below(10U) == 0U);
        if (step % 37 == 0) check("The index of a changing group does not describe it");
    }
    check("The index of a changed group does not describe it");
    // The instances all land somewhere else: the clusters are made again.
    const float builtBefore = index.builtSize;
    for (std::size_t position = 0U; position < instances.size(); ++position) {
        instances[position].model[12] = random.Range(-3000.0F, 3000.0F);
        instances[position].model[13] = random.Range(-3000.0F, 3000.0F);
        instances[position].model[14] = random.Range(-3000.0F, 3000.0F);
        SceneMeshVisibilityIndexBuilder::Update(index, instances[position], static_cast<std::uint32_t>(position));
    }
    check("The index of instances that moved far apart does not describe them");
    Require(index.builtSize > builtBefore * 4.0F && index.currentSize <= index.builtSize * 1.001F,
        "The clusters of instances that moved far apart were not made again");
    // Shrinking to a few instances empties the index, and growing again indexes them again.
    while (instances.size() > 5U) {
        remove();
        if (instances.size() % 53U == 0U) check("The index of a shrinking group does not describe it");
    }
    check("The index of a group that shrank does not describe it");
    Require(index.Empty(), "A group of a few instances kept its index");
    while (instances.size() < 70U) append();
    check("The index of a group that grew again does not describe it");
    Require(!index.Empty(), "A group that grew again was not indexed");
    // Instances whose models have no bounds the index can enclose are never ruled out with their cluster.
    SceneRenderMeshInstance flawed = makeInstance(60.0F);
    flawed.model[0] = flawed.model[5] = flawed.model[10] = 0.0F;
    instances.push_back(flawed);
    SceneMeshVisibilityIndexBuilder::Append(instances, index);
    instances.push_back(makeInstance(60.0F));
    instances.back().model[12] = std::numeric_limits<float>::quiet_NaN();
    SceneMeshVisibilityIndexBuilder::Append(instances, index);
    check("The index of instances without bounds does not describe them");
    std::size_t unbounded = 0U;
    for (const SceneMeshVisibilityIndex::Cluster& cluster : index.clusters) unbounded += cluster.unbounded ? 1U : 0U;
    Require(unbounded >= 1U && unbounded <= 2U, "Instances without bounds did not mark their clusters");
    // An index that fell out of step with its instances is made again.
    instances.pop_back();
    instances.pop_back();
    index.stale = 1U;
    check("An index that fell out of step with its instances was not made again");
}

// The visibility index changes nothing a pass builds. Scenes of every shape, with every pass, camera, mesh and material,
// give the commands, counters and diagnostics the scan of every instance gives.
void RunMeshPipelineVisibilityIndexMatchesScanTest() {
    IndexTestRandom random(3U);
    const std::vector<SceneRenderCamera> cameras = IndexTestCameras(random, 12U);
    std::size_t turnedAway = 0U;
    // Scenes of no object, of one, around the size that is indexed, and of many.
    for (const std::size_t count : { std::size_t{ 0U }, std::size_t{ 1U }, std::size_t{ 31U }, std::size_t{ 32U }, std::size_t{ 33U },
             std::size_t{ 120U }, std::size_t{ 1500U } }) {
        // Up to a few dozen, the ids are such that all objects share a group.
        RenderScene scene;
        FillRandomIndexScene(scene, random, count, 1U, count < 100U ? 4U : 1U, 60.0F);
        const std::vector<SceneRenderDrawGroup>& groups = scene.DrawGroups();
        RequireIndexDescribesGroups(groups, "The index of the scene does not describe its groups");
        turnedAway += RequireIndexMatchesScan(groups, cameras, "The visibility index changed what a pass builds from a scene of random objects");
    }
    {
        // Ids far apart: many groups of a few dozen each.
        RenderScene scene;
        FillRandomIndexScene(scene, random, 1200U, 1U, 17U, 60.0F);
        RequireIndexDescribesGroups(scene.DrawGroups(), "The index of a scene of groups of a few dozen does not describe it");
        turnedAway += RequireIndexMatchesScan(scene.DrawGroups(), cameras, "The visibility index changed what a pass builds from many small groups");
    }
    {
        // Every object in one point, in every layer and of every kind.
        RenderScene scene;
        for (std::uint64_t entity = 1U; entity <= 400U; ++entity) {
            MeshRenderProxyDesc proxy = RandomIndexProxy(random, entity, 1.0F);
            proxy.model = TranslationMatrix(10.0F, 5.0F, -7.0F);
            static_cast<void>(scene.UpsertMesh(proxy));
        }
        RequireIndexDescribesGroups(scene.DrawGroups(), "The index of objects in one point does not describe them");
        turnedAway += RequireIndexMatchesScan(scene.DrawGroups(), cameras, "The visibility index changed what a pass builds from objects in one point");
    }
    {
        // Objects that cover the whole scene among small ones, and bounds overrides that do.
        RenderScene scene;
        FillRandomIndexScene(scene, random, 300U, 1U, 1U, 60.0F);
        for (std::uint64_t entity = 301U; entity <= 308U; ++entity) {
            MeshRenderProxyDesc proxy = RandomIndexProxy(random, entity, 60.0F);
            proxy.model = RandomIndexModel(random, 400.0F, 2000.0F, 4000.0F, false);
            static_cast<void>(scene.UpsertMesh(proxy));
        }
        for (std::uint64_t entity = 309U; entity <= 312U; ++entity) {
            MeshRenderProxyDesc proxy = RandomIndexProxy(random, entity, 60.0F);
            proxy.boundsOverride = RenderBoundsSphere{ .center = { 100.0F, 0.0F, 0.0F }, .radius = 5.0e4F };
            static_cast<void>(scene.UpsertMesh(proxy));
        }
        RequireIndexDescribesGroups(scene.DrawGroups(), "The index of a scene of huge objects does not describe it");
        turnedAway += RequireIndexMatchesScan(scene.DrawGroups(), cameras, "The visibility index changed what a pass builds from huge objects");
    }
    {
        // Objects that have no size, or next to none, are never turned away.
        RenderScene scene;
        FillRandomIndexScene(scene, random, 120U, 1U, 1U, 60.0F);
        for (std::uint64_t entity = 121U; entity <= 130U; ++entity) {
            MeshRenderProxyDesc proxy = RandomIndexProxy(random, entity, 60.0F);
            proxy.model[0] = proxy.model[1] = proxy.model[2] = proxy.model[4] = proxy.model[5] = proxy.model[6] = 0.0F;
            proxy.model[8] = proxy.model[9] = proxy.model[10] = 0.0F;
            static_cast<void>(scene.UpsertMesh(proxy));
        }
        for (std::uint64_t entity = 131U; entity <= 140U; ++entity) {
            MeshRenderProxyDesc proxy = RandomIndexProxy(random, entity, 60.0F);
            proxy.model = RandomIndexModel(random, 60.0F, 1.0e-9F, 2.0e-9F, false);
            static_cast<void>(scene.UpsertMesh(proxy));
        }
        RequireIndexDescribesGroups(scene.DrawGroups(), "The index of a scene of objects without size does not describe it");
        turnedAway += RequireIndexMatchesScan(scene.DrawGroups(), cameras, "The visibility index changed what a pass builds from objects without size");
    }
    Require(turnedAway > 100U, "The visibility index test scenes did not turn instances away");

    // A camera that sees all of a large scene turns nothing away, and one that sees none of it admits nothing.
    RenderScene scene;
    FillRandomIndexScene(scene, random, 1500U, 1U, 1U, 60.0F);
    const std::vector<SceneRenderDrawGroup>& groups = scene.DrawGroups();
    const RenderMeshResource mesh = IndexTestMesh(0U);
    const RenderMaterialResource material{};
    const auto build = [&](const std::vector<SceneRenderDrawGroup>& source, const SceneRenderCamera& camera, MeshPassType pass) {
        return MeshPipelineProcessor::Build(MeshPipelineBuildDesc{ .pass = pass, .drawGroups = &source, .resolvedMeshResource = &mesh,
            .resolvedMaterialResource = &material, .camera = &camera, .resourceValidation = MeshPipelineResourceValidation::Skip });
    };
    const MeshPipelineBuildResult everything = build(groups, cameras[cameras.size() - 2U], MeshPassType::BaseOpaque);
    Require(everything.stats.culledInstanceCount == 0U && everything.stats.visibleMeshCount > 1000U, "A camera that sees the whole scene turned instances away");
    const MeshPipelineBuildResult nothing = build(groups, cameras.back(), MeshPassType::BaseOpaque);
    Require(nothing.stats.visibleMeshCount == 0U && nothing.commands.empty() && nothing.stats.culledInstanceCount > 1000U,
        "A camera that sees none of the scene admitted instances");

    // The index is what rules instances out: an instance that does not lie where the index says is ruled out with it.
    std::vector<SceneRenderDrawGroup> tampered = groups;
    SceneRenderDrawGroup& group = *std::ranges::max_element(tampered, {}, [](const SceneRenderDrawGroup& candidate) { return candidate.instances.size(); });
    for (SceneRenderMeshInstance& instance : group.instances) {
        instance.model = TranslationMatrix(1.0e5F, 0.0F, 1.0e5F);
        instance.layer = 1U;
        instance.shadowOnly = false;
        instance.castsShadow = true;
        instance.boundsOverride = {};
    }
    SceneMeshVisibilityIndexBuilder::Build(group.instances, group.visibility);
    group.instances[3].model = TranslationMatrix(0.0F, 0.0F, 10.0F);
    std::vector<SceneRenderDrawGroup> unindexed = tampered;
    for (SceneRenderDrawGroup& candidate : unindexed) candidate.visibility = {};
    SceneRenderCamera front{};
    bx::mtxLookAt(front.view.data(), bx::Vec3{ 0.0F, 0.0F, 0.0F }, bx::Vec3{ 0.0F, 0.0F, 1.0F }, bx::Vec3{ 0.0F, 1.0F, 0.0F });
    SceneDepthPolicy::MakePerspective(front.projection.data(), 90.0F, 1.0F, 0.1F, 50.0F, SceneDepthPolicy::HomogeneousDepth());
    const std::uint64_t moved = group.instances[3].entityId;
    const auto admits = [&](const std::vector<SceneRenderDrawGroup>& source) {
        const MeshPipelineBuildResult result = build(source, front, MeshPassType::Depth);
        return std::ranges::any_of(result.commands, [moved](const MeshDrawCommand& command) {
            return std::ranges::any_of(command.instances, [moved](const SceneRenderMeshInstance& instance) { return instance.entityId == moved; });
        });
    };
    Require(admits(unindexed) && !admits(tampered), "The visibility index did not rule out the instances it holds far away");
}

// Objects move, change, come and go between frames, and the passes built from them are those the scan builds.
void RunMeshPipelineVisibilityIndexFollowsChangingSceneTest() {
    IndexTestRandom random(5U);
    const std::vector<SceneRenderCamera> cameras = IndexTestCameras(random, 6U);
    RenderScene scene;
    std::vector<std::uint64_t> entities;
    std::uint64_t nextEntity = 1U;
    const auto addObject = [&] {
        // Now and then in a block of ids of its own, so that groups come and go.
        if (random.Below(40U) == 0U) nextEntity += 1024U;
        static_cast<void>(scene.UpsertMesh(RandomIndexProxy(random, nextEntity, 60.0F)));
        entities.push_back(nextEntity++);
    };
    for (int index = 0; index < 1400; ++index) addObject();
    static_cast<void>(scene.DrawGroups());
    for (int frame = 0; frame < 14; ++frame) {
        const bool farMoves = frame % 5 == 4;
        const std::size_t moving = entities.size() * (farMoves ? 70U : 8U) / 100U;
        // Some move, in place.
        for (std::size_t index = 0U; index < moving; ++index) {
            const std::uint64_t entity = entities[random.Below(static_cast<std::uint32_t>(entities.size()))];
            Require(scene.UpdateMeshTransform(entity, RandomIndexModel(random, farMoves ? 400.0F : 60.0F, 0.3F, 2.5F, false)),
                "A mesh proxy to move was not found");
        }
        // Some change what they are, in place or by going to another group.
        for (std::size_t index = 0U; index < entities.size() / 40U; ++index) {
            const std::uint64_t entity = entities[random.Below(static_cast<std::uint32_t>(entities.size()))];
            MeshRenderProxyDesc proxy = RandomIndexProxy(random, entity, 60.0F);
            if (random.Below(3U) != 0U) {
                const MeshRenderProxy* existing = scene.FindMeshByEntity(entity);
                Require(existing != nullptr, "A mesh proxy to change was not found");
                proxy.meshAssetId = existing->desc.meshAssetId;
                proxy.materialAssetId = existing->desc.materialAssetId;
            } else {
                proxy.meshAssetId = 500U + random.Below(3U);
                proxy.materialAssetId = 600U + random.Below(3U);
            }
            static_cast<void>(scene.UpsertMesh(proxy));
        }
        // Some go and some come.
        for (std::size_t index = 0U; index < entities.size() / 30U; ++index) {
            const std::size_t position = random.Below(static_cast<std::uint32_t>(entities.size()));
            Require(scene.RemoveMesh(entities[position]), "A mesh proxy to remove was not found");
            entities[position] = entities.back();
            entities.pop_back();
        }
        for (std::size_t index = 0U; index < 50U; ++index) addObject();
        // Once, nearly all go, and come again.
        if (frame == 9) {
            while (entities.size() > 60U) {
                Require(scene.RemoveMesh(entities.back()), "A mesh proxy to remove was not found");
                entities.pop_back();
            }
            static_cast<void>(scene.DrawGroups());
            for (int index = 0; index < 600; ++index) addObject();
        }
        const std::vector<SceneRenderDrawGroup>& groups = scene.DrawGroups();
        RequireIndexDescribesGroups(groups, "The index does not describe the groups of a scene that changed");
        static_cast<void>(RequireIndexMatchesScan(groups, cameras, "The visibility index changed what a pass builds from a scene that changed"));
    }
}

// The same through a renderer that resolves its resources: batches that miss a mesh, a material or a texture report each of
// their instances as the scan does, batches with slot overrides are scanned instance by instance, and the others are ruled
// out by their index.
void RunMeshPipelineVisibilityIndexMatchesScanWithResourcesTest() {
    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);
    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "The visibility index test could not initialize bgfx");
    {
        const std::array<RenderStaticMeshVertexP3N3UV2, 3> vertices{ {
            { .x = 0.0F, .y = 0.0F, .z = 0.0F }, { .x = 1.0F, .y = 0.0F, .z = 0.0F }, { .x = 0.0F, .y = 1.0F, .z = 0.0F } } };
        const std::array<std::uint16_t, 6> indices{ 0U, 1U, 2U, 0U, 2U, 1U };
        const std::array<RenderMeshSectionDesc, 2> sections{ {
            { .indexStart = 0U, .indexCount = 3U, .vertexStart = 0U, .vertexCount = 3U, .materialSlot = 0U, .bounds = { .radius = 1.0F } },
            { .indexStart = 3U, .indexCount = 3U, .vertexStart = 0U, .vertexCount = 3U, .materialSlot = 1U,
                .bounds = { .center = { 1.0F, 0.0F, 0.0F }, .radius = 1.5F } } } };
        RenderResourceRegistry registry;
        SceneRenderResourceMap resourceMap;
        // Meshes 500 (two sections) and 501 (one) resolve; 502 has no binding. Materials 600 and 601 resolve, 602 blends,
        // 603 reads a texture that has no binding and 604 has none.
        resourceMap.BindMesh(500U, registry.RegisterMesh(RenderMeshDesc{
            .vertexData = vertices.data(), .vertexCount = 3U, .indices = indices.data(), .indexCount = 6U,
            .vertexFormat = RenderVertexFormat::P3N3UV2, .sections = sections.data(), .sectionCount = 2U, .bounds = { .radius = 1.0F } }));
        resourceMap.BindMesh(501U, registry.RegisterMesh(RenderMeshDesc{
            .vertexData = vertices.data(), .vertexCount = 3U, .indices = indices.data(), .indexCount = 3U,
            .vertexFormat = RenderVertexFormat::P3N3UV2, .bounds = { .center = { 0.0F, 0.5F, 0.0F }, .radius = 1.2F } }));
        RenderMaterialDesc plain{};
        RenderMaterialDesc blending{};
        blending.alphaMode = RenderMaterialAlphaMode::Blend;
        RenderMaterialDesc textured{};
        textured.occlusionTextureAssetId = 909U;
        resourceMap.BindMaterial(600U, registry.RegisterMaterial(plain));
        resourceMap.BindMaterial(601U, registry.RegisterMaterial(plain));
        resourceMap.BindMaterial(602U, registry.RegisterMaterial(blending));
        resourceMap.BindMaterial(603U, registry.RegisterMaterial(textured));

        IndexTestRandom random(9U);
        const std::vector<SceneRenderCamera> cameras = IndexTestCameras(random, 8U);
        RenderScene scene;
        constexpr std::size_t kObjects = 4096U;
        for (std::size_t index = 0U; index < kObjects; ++index) {
            const std::size_t block = index / 1024U;
            MeshRenderProxyDesc proxy = RandomIndexProxy(random, index + 1U, 60.0F);
            proxy.meshAssetId = block == 2U ? 502U : 500U + index % 2U;
            proxy.materialAssetId = block == 1U ? 604U : (block == 3U ? 603U : 600U + (index / 2U) % 3U);
            if (index % 17U == 0U && block == 0U) {
                proxy.materialSlotAssetIds[1] = 600U + index % 2U;
                proxy.materialSlotOverrideCount = 2U;
            }
            static_cast<void>(scene.UpsertMesh(proxy));
        }
        const std::vector<SceneRenderDrawGroup>& groups = scene.DrawGroups();
        RequireIndexDescribesGroups(groups, "The index of a scene with resources does not describe it");
        const std::size_t turnedAway = RequireIndexMatchesScan(groups, cameras,
            [&](MeshPassType pass, const SceneRenderCamera* camera, const std::vector<SceneRenderDrawGroup>& source,
                SceneRenderDiagnostics& diagnostics, std::span<const std::uint64_t> selected, std::uint64_t motionFrame, std::size_t) {
                return MeshPipelineBuildDesc{
                    .pass = pass,
                    .drawGroups = &source,
                    .resources = &registry,
                    .resourceMap = &resourceMap,
                    .camera = camera,
                    .diagnostics = &diagnostics,
                    .selectedEntityIds = selected,
                    .motionFrame = motionFrame,
                };
            }, "The visibility index changed what a pass builds from a scene with resources");
        Require(turnedAway > 20U, "The visibility index test with resources did not turn instances away");
        // And built on workers, as the passes of a frame are.
        kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .workerCount = 8U } };
        pool.Start(kb::ecs::WorkerPoolConfig{ .workerCount = 8U });
        const std::vector<SceneMeshBatch> batches = SceneMeshBatchBuilder::Build(groups);
        for (const MeshPassType pass : { MeshPassType::BaseOpaque, MeshPassType::ShadowDepth }) {
            RequireParallelBuildMatchesSerial(pool, [&](kb::ecs::WorkerPool* workers, SceneRenderDiagnostics* diagnostics) {
                return MeshPipelineBuildDesc{ .pass = pass, .meshBatches = &batches, .resources = &registry, .resourceMap = &resourceMap,
                    .camera = &cameras[1], .diagnostics = diagnostics, .workerPool = workers };
            }, true, "Workers built the batches of an index differently from the calling thread");
        }
    }
    renderer.Shutdown();
}

// Passes of a large scene built together, and in chunks, rule out the same instances as ones built on the calling thread.
void RunMeshPipelineVisibilityIndexOnWorkersMatchesSerialTest() {
    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .workerCount = 8U } };
    pool.Start(kb::ecs::WorkerPoolConfig{ .workerCount = 8U });
    IndexTestRandom random(13U);
    RenderScene scene;
    FillRandomIndexScene(scene, random, 6000U, 1U, 1U, 60.0F);
    const std::vector<SceneMeshBatch> batches = SceneMeshBatchBuilder::Build(scene.DrawGroups());
    const std::vector<SceneRenderCamera> cameras = IndexTestCameras(random, 4U);
    const RenderMeshResource mesh = IndexTestMesh(1U);
    const RenderMaterialResource material{};
    for (const MeshPassType pass : { MeshPassType::BaseOpaque, MeshPassType::ShadowDepth, MeshPassType::GBuffer }) {
        RequireParallelBuildMatchesSerial(pool, [&](kb::ecs::WorkerPool* workers, SceneRenderDiagnostics* diagnostics) {
            return MeshPipelineBuildDesc{ .pass = pass, .meshBatches = &batches, .resolvedMeshResource = &mesh, .resolvedMaterialResource = &material,
                .camera = &cameras[0], .diagnostics = diagnostics, .resourceValidation = MeshPipelineResourceValidation::Skip, .workerPool = workers };
        }, true, "Workers built the batches of an index differently from the calling thread");
    }
    RequireTogetherBuildMatchesSerial(pool, [&](std::size_t index, kb::ecs::WorkerPool* workers, SceneRenderDiagnostics* diagnostics) {
        return MeshPipelineBuildDesc{ .pass = index % 2U == 0U ? MeshPassType::ShadowDepth : MeshPassType::BaseOpaque, .meshBatches = &batches,
            .resolvedMeshResource = &mesh, .resolvedMaterialResource = &material, .camera = &cameras[index], .diagnostics = diagnostics,
            .resourceValidation = MeshPipelineResourceValidation::Skip, .workerPool = workers };
    }, cameras.size(), "Passes built together from an index differ from the same passes built one by one");
}

// Resources the workers cannot resolve cleanly are reported by the calling thread, which builds the batches that use
// them in order: the same counters and the same diagnostics in the same order as without workers.
void RunMeshPipelineParallelScanReportsMissingResourcesInOrderTest() {
    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);
    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "The parallel scan test could not initialize bgfx");
    {
        const std::array<RenderStaticMeshVertexP3N3UV2, 3> vertices{ {
            { .x = 0.0F, .y = 0.0F, .z = 0.0F }, { .x = 1.0F, .y = 0.0F, .z = 0.0F }, { .x = 0.0F, .y = 1.0F, .z = 0.0F } } };
        const std::array<std::uint16_t, 6> indices{ 0U, 1U, 2U, 0U, 2U, 1U };
        const std::array<RenderMeshSectionDesc, 2> sections{ {
            { .indexStart = 0U, .indexCount = 3U, .vertexStart = 0U, .vertexCount = 3U, .materialSlot = 0U, .bounds = { .radius = 1.0F } },
            { .indexStart = 3U, .indexCount = 3U, .vertexStart = 0U, .vertexCount = 3U, .materialSlot = 1U, .bounds = { .radius = 1.0F } } } };
        RenderResourceRegistry registry;
        SceneRenderResourceMap resourceMap;
        // Meshes 1000 and 1001 resolve, 1002 has no binding at all.
        for (std::uint64_t meshAssetId = 1000U; meshAssetId <= 1001U; ++meshAssetId) {
            resourceMap.BindMesh(meshAssetId, registry.RegisterMesh(RenderMeshDesc{
                .vertexData = vertices.data(), .vertexCount = 3U, .indices = indices.data(), .indexCount = 6U,
                .vertexFormat = RenderVertexFormat::P3N3UV2, .sections = sections.data(), .sectionCount = 2U,
                .bounds = { .radius = 1.0F } }));
            Require(resourceMap.ResolveMesh(meshAssetId).IsValid(), "The parallel scan test could not register its mesh");
        }
        // Materials 2000 and 2001 resolve and 2002 blends; 2003 reads a texture that has no binding; 2004 has none.
        RenderMaterialDesc plain{};
        RenderMaterialDesc blending{};
        blending.alphaMode = RenderMaterialAlphaMode::Blend;
        RenderMaterialDesc textured{};
        textured.occlusionTextureAssetId = 909U;
        resourceMap.BindMaterial(2000U, registry.RegisterMaterial(plain));
        resourceMap.BindMaterial(2001U, registry.RegisterMaterial(plain));
        resourceMap.BindMaterial(2002U, registry.RegisterMaterial(blending));
        resourceMap.BindMaterial(2003U, registry.RegisterMaterial(textured));

        // Each batch has one mesh and one material; the one with the unbound mesh and the ones with the unbound material
        // or the missing texture are among the batches the workers scan.
        RenderScene scene;
        constexpr std::size_t kObjects = 8192U;
        for (std::size_t index = 0U; index < kObjects; ++index) {
            const std::size_t block = index / 1024U;
            const std::uint64_t meshAssetId = block == 5U ? 1002U : 1000U + index % 2U;
            const std::uint64_t materialAssetId = block == 3U ? 2004U : (block == 6U ? 2003U : 2000U + (index / 2U) % 3U);
            MeshRenderProxyDesc proxy{
                .entityId = index + 1U,
                .meshAssetId = meshAssetId,
                .materialAssetId = materialAssetId,
                .model = TranslationMatrix(static_cast<float>(index % 100U) - 50.0F,
                    static_cast<float>((index / 100U) % 100U) - 50.0F, 100.0F + static_cast<float>(index % 21U)),
            };
            if (index % 17U == 0U) {
                // The second section of these objects is drawn with a material of its own, so a batch has several.
                proxy.materialSlotAssetIds[1] = 2000U + index % 3U;
                proxy.materialSlotOverrideCount = 2U;
            }
            static_cast<void>(scene.UpsertMesh(proxy));
        }
        const std::vector<SceneMeshBatch> batches = SceneMeshBatchBuilder::Build(scene.DrawGroups());
        auto camera = PerspectiveCamera();
        SceneDepthPolicy::MakePerspective(camera.projection.data(), 90.0F, 1.0F, 0.1F, 1000.0F, SceneDepthPolicy::HomogeneousDepth());
        kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .workerCount = 8U } };
        pool.Start(kb::ecs::WorkerPoolConfig{ .workerCount = 8U });
        for (const MeshPassType pass : { MeshPassType::BaseOpaque, MeshPassType::BaseTransparent, MeshPassType::ShadowDepth }) {
            const auto makeDesc = [&](kb::ecs::WorkerPool* workerPool, SceneRenderDiagnostics* diagnostics) {
                return MeshPipelineBuildDesc{
                    .pass = pass,
                    .meshBatches = &batches,
                    .resources = &registry,
                    .resourceMap = &resourceMap,
                    .camera = &camera,
                    .diagnostics = diagnostics,
                    .workerPool = workerPool,
                };
            };
            RequireParallelBuildMatchesSerial(pool, makeDesc, true,
                "Workers reported missing resources differently from the calling thread");
            const MeshPipelineBuildResult reference = MeshPipelineProcessor::Build(makeDesc(nullptr, nullptr));
            Require(reference.stats.missingMeshBindingCount != 0U && reference.stats.missingMaterialBindingCount != 0U &&
                    reference.stats.missingTextureBindingCount != 0U,
                "The parallel scan test scene does not miss the resources it means to");
        }
        // Passes built together report what is missing as they would one by one, each in its own diagnostics.
        const std::array<MeshPassType, 3> passKinds{ MeshPassType::BaseOpaque, MeshPassType::BaseTransparent, MeshPassType::ShadowDepth };
        RequireTogetherBuildMatchesSerial(pool, [&](std::size_t index, kb::ecs::WorkerPool* workerPool, SceneRenderDiagnostics* diagnostics) {
            return MeshPipelineBuildDesc{
                .pass = passKinds[index],
                .meshBatches = &batches,
                .resources = &registry,
                .resourceMap = &resourceMap,
                .camera = &camera,
                .diagnostics = diagnostics,
                .workerPool = workerPool,
            };
        }, passKinds.size(), "Passes built together reported missing resources differently from the same passes built one by one");
    }
    renderer.Shutdown();
}

// Median microseconds of one BaseOpaque build of the batches. The calling thread is busy for `pause` before every build,
// as in a frame with other work between its passes: with none, builds follow each other and the workers stay awake;
// with some, the workers are asleep and have to be woken.
double MedianBuildMicroseconds(const std::vector<SceneMeshBatch>& batches, const RenderMeshResource& mesh,
    const SceneRenderCamera& camera, kb::ecs::WorkerPool* workerPool, std::chrono::milliseconds pause, int runs,
    std::size_t visibleCount) {
    const RenderMaterialResource material{};
    MeshPipelineBuildResult result;
    std::vector<double> samples;
    for (int run = -20; run < runs; ++run) {
        for (const auto until = std::chrono::steady_clock::now() + pause; std::chrono::steady_clock::now() < until;) {
        }
        const auto begin = std::chrono::steady_clock::now();
        MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{
            .pass = MeshPassType::BaseOpaque,
            .meshBatches = &batches,
            .resolvedMeshResource = &mesh,
            .resolvedMaterialResource = &material,
            .camera = &camera,
            .resourceValidation = MeshPipelineResourceValidation::Skip,
            .workerPool = workerPool,
        }, result);
        const auto end = std::chrono::steady_clock::now();
        if (run >= 0) samples.push_back(std::chrono::duration<double, std::micro>(end - begin).count());
    }
    Require(result.stats.visibleMeshCount == visibleCount, "The scale benchmark scene is not fully visible");
    std::ranges::sort(samples);
    return samples[samples.size() / 2U];
}

// The cameras of the directional shadow cascades of a frame: each reaches farther than the one before.
std::vector<SceneRenderCamera> ShadowCascadeCameras(std::initializer_list<float> reaches = { 30.0F, 45.0F, 70.0F, 110.0F }) {
    std::vector<SceneRenderCamera> cameras;
    for (const float reach : reaches) {
        SceneRenderCamera camera{};
        camera.view = IdentityMatrix();
        SceneDepthPolicy::MakeOrthographic(camera.projection.data(), reach * 2.0F, 1.0F, 0.1F, 1000.0F, SceneDepthPolicy::HomogeneousDepth());
        cameras.push_back(camera);
    }
    return cameras;
}

// The cameras of the six faces of each of four point lights placed inside the benchmark scene.
std::vector<SceneRenderCamera> ShadowPointFaceCameras() {
    struct Face { bx::Vec3 axis; bx::Vec3 up; };
    const std::array<Face, 6> faces{ {
        { { 1.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F } }, { { -1.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F } },
        { { 0.0F, 1.0F, 0.0F }, { 0.0F, 0.0F, -1.0F } }, { { 0.0F, -1.0F, 0.0F }, { 0.0F, 0.0F, 1.0F } },
        { { 0.0F, 0.0F, 1.0F }, { 0.0F, 1.0F, 0.0F } }, { { 0.0F, 0.0F, -1.0F }, { 0.0F, 1.0F, 0.0F } } } };
    std::vector<SceneRenderCamera> cameras;
    for (const bx::Vec3 eye : { bx::Vec3{ -25.0F, -25.0F, 110.0F }, bx::Vec3{ 25.0F, -25.0F, 110.0F },
             bx::Vec3{ -25.0F, 25.0F, 110.0F }, bx::Vec3{ 25.0F, 25.0F, 110.0F } }) {
        for (const Face& face : faces) {
            SceneRenderCamera camera{};
            bx::mtxLookAt(camera.view.data(), eye, bx::add(eye, face.axis), face.up);
            SceneDepthPolicy::MakePerspective(camera.projection.data(), 90.0F, 1.0F, 0.1F, 80.0F, SceneDepthPolicy::HomogeneousDepth());
            cameras.push_back(camera);
        }
    }
    return cameras;
}

// The counters of a submitted pass that do not depend on how its instances are grouped into draw commands, which a
// renderer that keeps the commands of the batches of a still camera does differently from one that merges the batches.
[[nodiscard]] std::array<std::uint32_t, 12> PassCounters(const SceneRenderSubmitStats& stats) noexcept {
    return { stats.visibleMeshCount, stats.culledInstanceCount, stats.droppedInstanceCount, stats.lodSelectionCount,
        stats.submittedMeshCount, stats.missingMeshBindingCount, stats.missingMeshResourceCount, stats.missingMaterialBindingCount,
        stats.missingMaterialResourceCount, stats.shadowCasterCount, stats.submittedShadowCasterCount,
        stats.missingTextureBindingCount };
}

// A scene renderer that prepares the shadow passes of a frame submits them with the counters and diagnostics of one that
// builds each pass when it submits it, over scenes that miss resources, and builds ahead only what it was asked to.
void RunSceneRendererPreparedShadowPassesMatchOneByOneTest() {
    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);
    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "The prepared shadow pass test could not initialize bgfx");
    {
        kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{ .workerCount = 8U } };
        pool.Start(kb::ecs::WorkerPoolConfig{ .workerCount = 8U });
        // Two scene renderers of the same content: the first builds every pass when it submits it, the second prepares.
        std::array<SceneRenderer, 2> renderers;
        const std::array<RenderStaticMeshVertexP3N3UV2, 3> vertices{ {
            { .x = 0.0F, .y = 0.0F, .z = 0.0F }, { .x = 1.0F, .y = 0.0F, .z = 0.0F }, { .x = 0.0F, .y = 1.0F, .z = 0.0F } } };
        const std::array<std::uint16_t, 3> indices{ 0U, 1U, 2U };
        for (SceneRenderer& sceneRenderer : renderers) {
            Require(sceneRenderer.Initialize(), "The prepared shadow pass test could not initialize a scene renderer");
            sceneRenderer.SetWorkerPool(&pool);
            // Meshes 1000 and 1001 resolve, 1002 has no binding; materials 2000 to 2002 resolve, 2003 has none.
            for (std::uint64_t meshAssetId = 1000U; meshAssetId <= 1001U; ++meshAssetId) {
                sceneRenderer.ResourceMap().BindMesh(meshAssetId, sceneRenderer.Resources().RegisterMesh(RenderMeshDesc{
                    .vertexData = vertices.data(), .vertexCount = 3U, .indices = indices.data(), .indexCount = 3U,
                    .vertexFormat = RenderVertexFormat::P3N3UV2, .bounds = { .radius = 1.0F } }));
            }
            for (std::uint64_t materialAssetId = 2000U; materialAssetId <= 2002U; ++materialAssetId) {
                sceneRenderer.ResourceMap().BindMaterial(materialAssetId, sceneRenderer.Resources().RegisterMaterial(RenderMaterialDesc{}));
            }
        }
        RenderScene scene;
        constexpr std::size_t kObjects = 6144U;
        for (std::size_t index = 0U; index < kObjects; ++index) {
            const std::size_t block = index / 1024U;
            static_cast<void>(scene.UpsertMesh(MeshRenderProxyDesc{
                .entityId = index + 1U,
                .meshAssetId = block == 4U ? 1002U : 1000U + index % 2U,
                .materialAssetId = block == 2U ? 2003U : 2000U + (index / 2U) % 3U,
                .model = TranslationMatrix(static_cast<float>(index % 100U) - 50.0F,
                    static_cast<float>((index / 100U) % 100U) - 50.0F, 100.0F + static_cast<float>(index % 21U)),
            }));
        }
        // Every cascade sees every object, so a renderer that builds them one by one finds the draw commands of the first
        // in the cache it fills for the others.
        const std::vector<SceneRenderCamera> cameras = ShadowCascadeCameras({ 60.0F, 70.0F, 80.0F, 90.0F });
        std::vector<const SceneRenderCamera*> pointers;
        for (const SceneRenderCamera& camera : cameras) pointers.push_back(&camera);
        SceneRenderLightingConfig lighting{};
        lighting.shadowsEnabled = true;
        for (int round = 0; round < 3; ++round) {
            // The third round prepares a single pass, which is built as usual when it is submitted.
            const std::span<const SceneRenderCamera* const> prepared = round == 2 ? std::span{ pointers }.first(1U) : std::span{ pointers };
            renderers[1].PrepareMeshPasses(MeshPassType::ShadowDepth, scene, prepared);
            for (std::size_t index = 0U; index < cameras.size(); ++index) {
                const bgfx::ViewId view = static_cast<bgfx::ViewId>(index);
                renderers[0].SubmitMeshPass(view, MeshPassType::ShadowDepth, scene, 64U, 64U, &cameras[index], {}, lighting);
                renderers[1].SubmitMeshPass(view, MeshPassType::ShadowDepth, scene, 64U, 64U, &cameras[index], {}, lighting);
                const SceneRenderSubmitStats& expected = renderers[0].LastSubmitStats();
                const SceneRenderSubmitStats& actual = renderers[1].LastSubmitStats();
                Require(PassCounters(expected) == PassCounters(actual) &&
                        SameDiagnostics(renderers[0].LastDiagnostics(), renderers[1].LastDiagnostics()),
                    "A prepared shadow pass was submitted differently from one built when it was submitted");
                Require(expected.visibleMeshCount != 0U && expected.missingMeshBindingCount != 0U &&
                        expected.missingMaterialBindingCount != 0U && !renderers[0].LastDiagnostics().events.empty(),
                    "The prepared shadow pass test scene does not miss the resources it means to");
                // A pass built ahead starts with a draw command cache of its own; the passes built one by one share one.
                if (round == 0 && index != 0U) {
                    Require(actual.meshDrawCommandCacheBuildCount != 0U && expected.meshDrawCommandCacheBuildCount == 0U,
                        "The renderer did not build the prepared shadow passes ahead");
                }
            }
            for (SceneRenderer& sceneRenderer : renderers) sceneRenderer.TickFrame();
            bgfx::frame();
        }
    }
    renderer.Shutdown();
}

// How the shadow passes of a frame are built: one after the other, as the shadow stage submits them, or all at once.
enum class ShadowBuildMode { OneByOne, Together };

// Median microseconds the calling thread spends building the shadow passes of one frame. The calling thread is busy for
// `pause` before every pass built on its own, or once before all passes built together, as it is while it submits the
// pass before.
double MedianShadowStageMicroseconds(const std::vector<SceneMeshBatch>& batches, const RenderMeshResource& mesh,
    std::span<const SceneRenderCamera> cameras, kb::ecs::WorkerPool* workerPool, ShadowBuildMode mode,
    std::chrono::microseconds pause, int runs, std::size_t& admittedCount) {
    const RenderMaterialResource material{};
    std::vector<MeshPipelineBuildResult> results(mode == ShadowBuildMode::Together ? cameras.size() : 1U);
    const auto describe = [&](const SceneRenderCamera& camera) {
        return MeshPipelineBuildDesc{
            .pass = MeshPassType::ShadowDepth,
            .meshBatches = &batches,
            .resolvedMeshResource = &mesh,
            .resolvedMaterialResource = &material,
            .camera = &camera,
            .resourceValidation = MeshPipelineResourceValidation::Skip,
            .workerPool = workerPool,
        };
    };
    const auto busyWait = [](std::chrono::microseconds duration) {
        for (const auto until = std::chrono::steady_clock::now() + duration; std::chrono::steady_clock::now() < until;) {
        }
    };
    std::vector<double> samples;
    for (int run = -10; run < runs; ++run) {
        double microseconds = 0.0;
        admittedCount = 0U;
        if (mode == ShadowBuildMode::Together) {
            std::vector<MeshPipelinePassBuild> passes;
            for (std::size_t index = 0U; index < cameras.size(); ++index) {
                passes.push_back(MeshPipelinePassBuild{ .desc = describe(cameras[index]), .result = &results[index] });
            }
            busyWait(pause);
            const auto begin = std::chrono::steady_clock::now();
            MeshPipelineProcessor::BuildPassesInto(passes);
            microseconds = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begin).count();
            for (const MeshPipelineBuildResult& result : results) admittedCount += result.stats.visibleMeshCount;
        } else {
            for (const SceneRenderCamera& camera : cameras) {
                busyWait(pause);
                const auto begin = std::chrono::steady_clock::now();
                MeshPipelineProcessor::BuildInto(describe(camera), results.front());
                microseconds += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begin).count();
                admittedCount += results.front().stats.visibleMeshCount;
            }
        }
        if (run >= 0) samples.push_back(microseconds);
    }
    std::ranges::sort(samples);
    return samples[samples.size() / 2U];
}

// ---- Large scenes of shadow casters -----------------------------------------------------------------------------

// The value of an environment variable, empty when it is not set.
[[nodiscard]] std::string EnvironmentValue(const char* name) {
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t length = 0U;
    static_cast<void>(_dupenv_s(&value, &length, name));
    std::string result = value == nullptr ? std::string{} : std::string{ value };
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string{ value };
#endif
}

// The numbers of a comma separated list.
[[nodiscard]] std::vector<std::size_t> ParseCountList(const std::string& text) {
    std::vector<std::size_t> counts;
    for (std::size_t begin = 0U; begin < text.size();) {
        std::size_t end = text.find(',', begin);
        if (end == std::string::npos) end = text.size();
        if (end > begin) counts.push_back(static_cast<std::size_t>(std::stoull(text.substr(begin, end - begin))));
        begin = end + 1U;
    }
    return counts;
}

// A unit cube: one section and the bounding sphere of the cube.
RenderMeshResource CubeBenchmarkMesh() {
    RenderMeshResource mesh{};
    mesh.indexCount = 36U;
    mesh.bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 0.8660254F };
    mesh.sections = { RenderMeshSection{ .indexStart = 0U, .indexCount = 36U, .bounds = mesh.bounds } };
    return mesh;
}

// The box of 120 x 40 x 120 with its corner at (-60, 0, -60) that the large scenes fill with a regular grid of cubes: a grid
// of 3n x n x 3n cells, the first n of which that hold an object being spread evenly over all of them. The objects are
// numbered along the grid, or, shuffled, in an order that has no relation to where they are. A cube takes 0.4 of the
// smallest side of its cell, and the objects use 16 pairs of mesh and material, as the other benchmarks do.
void FillCasterGridScene(RenderScene& scene, std::size_t objectCount, bool shuffled) {
    std::uint64_t perHeight = 1U;
    while (9U * perHeight * perHeight * perHeight < objectCount) ++perHeight;
    const std::uint64_t cellsX = 3U * perHeight;
    const std::uint64_t cellsZ = 3U * perHeight;
    const std::uint64_t cells = cellsX * perHeight * cellsZ;
    const float stepX = 120.0F / static_cast<float>(cellsX);
    const float stepY = 40.0F / static_cast<float>(perHeight);
    const float stepZ = 120.0F / static_cast<float>(cellsZ);
    const float scale = 0.4F * std::min({ stepX, stepY, stepZ });
    for (std::size_t index = 0U; index < objectCount; ++index) {
        std::uint64_t cell = static_cast<std::uint64_t>(index) * cells / objectCount;
        if (shuffled) cell = cell * 2147483647ULL % cells;
        const std::uint64_t cellX = cell % cellsX;
        const std::uint64_t cellZ = (cell / cellsX) % cellsZ;
        const std::uint64_t cellY = cell / (cellsX * cellsZ);
        auto model = IdentityMatrix();
        model[0] = scale;
        model[5] = scale;
        model[10] = scale;
        model[12] = -60.0F + (static_cast<float>(cellX) + 0.5F) * stepX;
        model[13] = (static_cast<float>(cellY) + 0.5F) * stepY;
        model[14] = -60.0F + (static_cast<float>(cellZ) + 0.5F) * stepZ;
        static_cast<void>(scene.UpsertMesh(MeshRenderProxyDesc{
            .entityId = index + 1U,
            .meshAssetId = 1000U + index % 16U,
            .materialAssetId = 2000U + index % 16U,
            .model = model,
        }));
    }
}

// The viewer of a frame of the large scenes: it looks at the middle of the box from outside and turns and slides a little
// more every frame, so that no pass repeats the one before.
struct CasterViewer {
    bx::Vec3 position;
    bx::Vec3 forward;
    bx::Vec3 right;
    bx::Vec3 up;
};

[[nodiscard]] bx::Vec3 TurnAboutUp(const bx::Vec3& vector, float angle) noexcept {
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    return bx::Vec3{ vector.x * cosine + vector.z * sine, vector.y, -vector.x * sine + vector.z * cosine };
}

[[nodiscard]] CasterViewer CasterViewerAtFrame(int frame) noexcept {
    const bx::Vec3 start{ 0.0F, 20.0F, 66.0F };
    const bx::Vec3 forward = bx::normalize(bx::sub(bx::Vec3{ 0.0F, 14.0F, 0.0F }, start));
    const bx::Vec3 back = bx::mul(forward, -1.0F);
    const bx::Vec3 right = bx::normalize(bx::cross(bx::Vec3{ 0.0F, 1.0F, 0.0F }, back));
    const bx::Vec3 up = bx::cross(back, right);
    const float turn = 0.12F * std::sin(static_cast<float>(frame) * 0.35F);
    return CasterViewer{
        .position = bx::Vec3{ start.x + 0.5F * std::sin(static_cast<float>(frame) * 0.21F), start.y, start.z },
        .forward = TurnAboutUp(forward, turn),
        .right = TurnAboutUp(right, turn),
        .up = TurnAboutUp(up, turn),
    };
}

// The four cascades of a directional light that points along (0.35, -1, -0.25): the viewer's 70 degree frustum of aspect
// 16:9 is cut at 0.5 and at 0.1, 0.2, 0.5 and 1 of the way from there to `reach` (150 unless set), and each part gets the
// box that bounds it along the axes of the light, open towards the light. A box becomes an orthographic camera in the
// middle of it, looking along the light.
std::vector<SceneRenderCamera> CasterCascadeCameras(int frame, float reach) {
    const CasterViewer viewer = CasterViewerAtFrame(frame);
    const bx::Vec3 towardLight = bx::normalize(bx::Vec3{ -0.35F, 1.0F, 0.25F });
    const bx::Vec3 axisX = bx::normalize(bx::cross(bx::Vec3{ 0.0F, 1.0F, 0.0F }, towardLight));
    const bx::Vec3 axisY = bx::cross(towardLight, axisX);
    const std::array<float, 5> distances{ 0.5F, 0.5F + 0.1F * (reach - 0.5F), 0.5F + 0.2F * (reach - 0.5F),
        0.5F + 0.5F * (reach - 0.5F), reach };
    const float halfTangent = std::tan(bx::toRad(70.0F) * 0.5F);
    std::vector<SceneRenderCamera> cameras;
    for (std::size_t cascade = 0U; cascade < 4U; ++cascade) {
        std::array<float, 3> low{ 1.0e30F, 1.0e30F, 1.0e30F };
        std::array<float, 3> high{ -1.0e30F, -1.0e30F, -1.0e30F };
        for (const float distance : { distances[cascade], distances[cascade + 1U] }) {
            const float halfHeight = distance * halfTangent;
            const float halfWidth = halfHeight * (16.0F / 9.0F);
            for (const float sideX : { -1.0F, 1.0F }) {
                for (const float sideY : { -1.0F, 1.0F }) {
                    const bx::Vec3 corner = bx::add(bx::add(bx::add(viewer.position, bx::mul(viewer.forward, distance)),
                        bx::mul(viewer.right, sideX * halfWidth)), bx::mul(viewer.up, sideY * halfHeight));
                    const std::array<float, 3> along{ bx::dot(corner, axisX), bx::dot(corner, axisY), bx::dot(corner, towardLight) };
                    for (std::size_t axis = 0U; axis < 3U; ++axis) {
                        low[axis] = std::min(low[axis], along[axis]);
                        high[axis] = std::max(high[axis], along[axis]);
                    }
                }
            }
        }
        const float eyeDepth = high[2] + 2000.0F;
        const bx::Vec3 eye = bx::add(bx::add(bx::mul(axisX, 0.5F * (low[0] + high[0])), bx::mul(axisY, 0.5F * (low[1] + high[1]))),
            bx::mul(towardLight, eyeDepth));
        SceneRenderCamera camera{};
        bx::mtxLookAt(camera.view.data(), eye, bx::sub(eye, towardLight), axisY);
        const float halfExtentX = 0.5F * (high[0] - low[0]);
        const float halfExtentY = 0.5F * (high[1] - low[1]);
        SceneDepthPolicy::MakeOrthographic(camera.projection.data(), halfExtentY * 2.0F, halfExtentX / halfExtentY, 0.1F,
            eyeDepth - low[2], SceneDepthPolicy::HomogeneousDepth());
        cameras.push_back(camera);
    }
    return cameras;
}

// The six faces of each of four point lights of the given range, at (+-30, 20, +-30), each of which slides a little every frame.
std::vector<SceneRenderCamera> CasterPointFaceCameras(int frame, float range) {
    struct Face { bx::Vec3 axis; bx::Vec3 up; };
    const std::array<Face, 6> faces{ {
        { { 1.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F } }, { { -1.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F } },
        { { 0.0F, 1.0F, 0.0F }, { 0.0F, 0.0F, -1.0F } }, { { 0.0F, -1.0F, 0.0F }, { 0.0F, 0.0F, 1.0F } },
        { { 0.0F, 0.0F, 1.0F }, { 0.0F, 1.0F, 0.0F } }, { { 0.0F, 0.0F, -1.0F }, { 0.0F, 1.0F, 0.0F } } } };
    const std::array<bx::Vec3, 4> lights{ bx::Vec3{ -30.0F, 20.0F, -30.0F }, bx::Vec3{ 30.0F, 20.0F, -30.0F },
        bx::Vec3{ -30.0F, 20.0F, 30.0F }, bx::Vec3{ 30.0F, 20.0F, 30.0F } };
    std::vector<SceneRenderCamera> cameras;
    for (std::size_t light = 0U; light < lights.size(); ++light) {
        const bx::Vec3 eye{ lights[light].x + 0.01F * std::sin(static_cast<float>(frame) * 0.5F + static_cast<float>(light)),
            lights[light].y, lights[light].z };
        for (const Face& face : faces) {
            SceneRenderCamera camera{};
            bx::mtxLookAt(camera.view.data(), eye, bx::add(eye, face.axis), face.up);
            SceneDepthPolicy::MakePerspective(camera.projection.data(), 90.0F, 1.0F, 0.025F, range, SceneDepthPolicy::HomogeneousDepth());
            cameras.push_back(camera);
        }
    }
    return cameras;
}

// Microseconds the calling thread spends building the shadow passes of one frame, built together on the workers or one by
// one on the calling thread. `results` keeps the commands of the passes from one frame to the next, as a renderer does.
double ShadowFrameMicroseconds(const std::vector<SceneMeshBatch>& batches, const RenderMeshResource& mesh,
    std::span<const SceneRenderCamera> cameras, kb::ecs::WorkerPool* workerPool, ShadowBuildMode mode,
    std::vector<MeshPipelineBuildResult>& results, std::size_t& admittedCount, MeshPassType pass = MeshPassType::ShadowDepth) {
    const RenderMaterialResource material{};
    const auto describe = [&](const SceneRenderCamera& camera) {
        return MeshPipelineBuildDesc{
            .pass = pass,
            .meshBatches = &batches,
            .resolvedMeshResource = &mesh,
            .resolvedMaterialResource = &material,
            .camera = &camera,
            .resourceValidation = MeshPipelineResourceValidation::Skip,
            .workerPool = workerPool,
        };
    };
    admittedCount = 0U;
    if (mode == ShadowBuildMode::Together) {
        std::vector<MeshPipelinePassBuild> passes;
        for (std::size_t index = 0U; index < cameras.size(); ++index) {
            passes.push_back(MeshPipelinePassBuild{ .desc = describe(cameras[index]), .result = &results[index] });
        }
        const auto begin = std::chrono::steady_clock::now();
        MeshPipelineProcessor::BuildPassesInto(passes);
        const double microseconds = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begin).count();
        for (const MeshPipelineBuildResult& result : results) admittedCount += result.stats.visibleMeshCount;
        return microseconds;
    }
    double microseconds = 0.0;
    for (const SceneRenderCamera& camera : cameras) {
        const auto begin = std::chrono::steady_clock::now();
        MeshPipelineProcessor::BuildInto(describe(camera), results.front());
        microseconds += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begin).count();
        admittedCount += results.front().stats.visibleMeshCount;
    }
    return microseconds;
}

[[nodiscard]] double MedianOf(std::vector<double> samples) {
    if (samples.empty()) return -1.0;
    std::ranges::sort(samples);
    return samples[samples.size() / 2U];
}

[[nodiscard]] double WorstOf(const std::vector<double>& samples) {
    return samples.empty() ? -1.0 : *std::ranges::max_element(samples);
}

// The camera of the viewer of a frame of the large scenes, which sees 70 degrees to `farClip`.
SceneRenderCamera CasterViewerCamera(int frame, float farClip) {
    const CasterViewer viewer = CasterViewerAtFrame(frame);
    SceneRenderCamera camera{};
    bx::mtxLookAt(camera.view.data(), viewer.position, bx::add(viewer.position, viewer.forward), viewer.up);
    SceneDepthPolicy::MakePerspective(camera.projection.data(), 70.0F, 16.0F / 9.0F, 0.5F, farClip, SceneDepthPolicy::HomogeneousDepth());
    return camera;
}

// Microseconds that moving a share of the objects of a large scene takes every frame, with the visibility index of its draw
// groups kept in step and without it, that bringing the index up to date for the passes that follow takes, and that making
// the index takes. `percents` are the shares of the objects that move.
void RunMovingSceneBenchmark(std::size_t objectCount, const std::vector<std::size_t>& percents, int frames, bool shuffled) {
    for (const std::size_t percent : percents) {
        RenderScene scene;
        FillCasterGridScene(scene, objectCount, shuffled);
        const auto buildBegin = std::chrono::steady_clock::now();
        const std::vector<SceneRenderDrawGroup>& groups = scene.DrawGroups();
        const double groupsMicroseconds = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - buildBegin).count();
        const auto indexBegin = std::chrono::steady_clock::now();
        std::size_t clusters = 0U;
        for (const SceneRenderDrawGroup& group : groups) {
            SceneMeshVisibilityIndex index;
            SceneMeshVisibilityIndexBuilder::Build(group.instances, index);
            clusters += index.clusters.size();
        }
        const double indexMicroseconds = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - indexBegin).count();
        std::vector<double> updatesWith;
        std::vector<double> refreshes;
        std::vector<double> updatesWithout;
        const std::size_t moving = std::max<std::size_t>(1U, objectCount * percent / 100U);
        for (const bool withIndex : { true, false }) {
            if (!withIndex) {
                // The same moves with no index to keep: what the scene did before it had one.
                for (SceneRenderDrawGroup& group : const_cast<std::vector<SceneRenderDrawGroup>&>(groups)) group.visibility = {};
            }
            for (int frame = 0; frame < frames + 2; ++frame) {
                const auto updateBegin = std::chrono::steady_clock::now();
                for (std::size_t move = 0U; move < moving; ++move) {
                    const std::uint64_t entity = 1U + (move * 7919U + static_cast<std::size_t>(frame) * 104729U) % objectCount;
                    const MeshRenderProxy* proxy = scene.FindMeshByEntity(entity);
                    auto model = proxy->desc.model;
                    model[12] += 0.1F * std::sin(static_cast<float>(frame + static_cast<int>(move)));
                    model[14] += 0.1F * std::cos(static_cast<float>(frame));
                    static_cast<void>(scene.UpdateMeshTransform(entity, model));
                }
                const double update = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - updateBegin).count();
                const auto refreshBegin = std::chrono::steady_clock::now();
                static_cast<void>(scene.DrawGroups());
                const double refresh = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - refreshBegin).count();
                if (frame >= 2) {
                    (withIndex ? updatesWith : updatesWithout).push_back(update);
                    if (withIndex) refreshes.push_back(refresh);
                }
            }
        }
        std::fprintf(stderr, "shadow_scale_move percent objects groups clusters scene_build_us index_build_us update_with_index_us update_without_index_us refresh_us" "\n");
        std::fprintf(stderr, "shadow_scale_move %zu %zu %zu %zu %.0f %.0f %.1f %.1f %.1f" "\n", percent, objectCount, groups.size(), clusters,
            groupsMicroseconds, indexMicroseconds, MedianOf(updatesWith), MedianOf(updatesWithout), MedianOf(refreshes));
    }
}

// Median and worst microseconds of the shadow passes of a frame, over scenes of the sizes in `sizeList` that are far
// larger than the ones the benchmark otherwise builds. Every frame the camera and the lights move, so no pass repeats
// the one before, and each size is a row of its own whose passes are built on the workers and, unless `KB_SHADOW_SCALE_MODES`
// leaves it out, one after the other on the calling thread. `KB_SHADOW_SCALE_FRAMES` sets the frames measured (after three
// that are not), `KB_SHADOW_SCALE_STAGES` is cascades, points, main (the opaque pass of the viewer), both or all,
// `KB_SHADOW_SCALE_ORDER` is raster or shuffle and `KB_SHADOW_SCALE_POINT_RANGE` is the range of the point lights (25 unless
// set) and `KB_SHADOW_SCALE_CASCADE_REACH` is how far the cascades reach (150 unless set). `KB_SHADOW_SCALE_INDEX=off`
// builds without the visibility index of the draw groups, and `KB_SHADOW_SCALE_MOVING` lists shares of the objects (percent)
// to move every frame, which prints what keeping the index in step takes.
void RunShadowLargeSceneBenchmark(const std::string& sizeList) {
    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{} };
    pool.Start(kb::ecs::WorkerPoolConfig{});
    const std::string frameText = EnvironmentValue("KB_SHADOW_SCALE_FRAMES");
    const int frames = frameText.empty() ? 10 : std::max(1, std::atoi(frameText.c_str()));
    constexpr int kWarmupFrames = 3;
    const std::string stageText = EnvironmentValue("KB_SHADOW_SCALE_STAGES");
    const bool withCascades = stageText.empty() || stageText == "both" || stageText == "cascades" || stageText == "all";
    const bool withPoints = stageText.empty() || stageText == "both" || stageText == "points" || stageText == "all";
    // The pass of the viewer, the opaque one, which sees 70 degrees to `KB_SHADOW_SCALE_MAIN_FAR` (150 unless set).
    const bool withMain = stageText == "main" || stageText == "all";
    const std::string mainFarText = EnvironmentValue("KB_SHADOW_SCALE_MAIN_FAR");
    const float mainFar = mainFarText.empty() ? 150.0F : std::max(1.0F, static_cast<float>(std::atof(mainFarText.c_str())));
    // `KB_SHADOW_SCALE_MOVING` is a list of the shares of the objects (percent) that move every frame, for a row of their own.
    const std::string movingText = EnvironmentValue("KB_SHADOW_SCALE_MOVING");
    // `KB_SHADOW_SCALE_INDEX=off` builds the passes from groups without their visibility index, which scans every instance.
    const bool withoutIndex = EnvironmentValue("KB_SHADOW_SCALE_INDEX") == "off";
    const std::string modeText = EnvironmentValue("KB_SHADOW_SCALE_MODES");
    const bool withSerial = modeText.empty() || modeText.find("serial") != std::string::npos;
    const bool shuffled = EnvironmentValue("KB_SHADOW_SCALE_ORDER") == "shuffle";
    const std::string rangeText = EnvironmentValue("KB_SHADOW_SCALE_POINT_RANGE");
    const float pointRange = rangeText.empty() ? 25.0F : std::max(0.1F, static_cast<float>(std::atof(rangeText.c_str())));
    const std::string reachText = EnvironmentValue("KB_SHADOW_SCALE_CASCADE_REACH");
    const float cascadeReach = reachText.empty() ? 150.0F : std::max(1.0F, static_cast<float>(std::atof(reachText.c_str())));
    const RenderMeshResource mesh = CubeBenchmarkMesh();

    std::fprintf(stderr, "shadow_scale_large microseconds of the shadow passes of one frame, %zu workers, %s order\n",
        pool.WorkerCount(), shuffled ? "shuffled" : "grid");
    std::fprintf(stderr, "shadow_scale_large stage objects passes admitted frames together_median together_worst serial_median serial_worst workers\n");
    for (const std::size_t objectCount : ParseCountList(sizeList)) {
        RenderScene scene;
        FillCasterGridScene(scene, objectCount, shuffled);
        if (withoutIndex) {
            for (SceneRenderDrawGroup& group : const_cast<std::vector<SceneRenderDrawGroup>&>(scene.DrawGroups())) group.visibility = {};
        }
        const std::vector<SceneMeshBatch> batches = SceneMeshBatchBuilder::Build(scene.DrawGroups());
        if (withMain) {
            std::vector<MeshPipelineBuildResult> togetherResults(1U);
            std::vector<MeshPipelineBuildResult> serialResults(1U);
            std::vector<double> together;
            std::vector<double> serial;
            std::size_t admitted = 0U;
            for (int frame = -kWarmupFrames; frame < frames; ++frame) {
                const std::array<SceneRenderCamera, 1> cameras{ CasterViewerCamera(frame + kWarmupFrames, mainFar) };
                const double togetherTime = ShadowFrameMicroseconds(batches, mesh, cameras, &pool, ShadowBuildMode::Together, togetherResults, admitted, MeshPassType::BaseOpaque);
                if (frame >= 0) together.push_back(togetherTime);
                if (withSerial) {
                    std::size_t serialAdmitted = 0U;
                    const double serialTime = ShadowFrameMicroseconds(batches, mesh, cameras, nullptr, ShadowBuildMode::OneByOne, serialResults, serialAdmitted, MeshPassType::BaseOpaque);
                    Require(serialAdmitted == admitted, "The opaque pass built on one thread admitted other instances than the one built on workers");
                    if (frame >= 0) serial.push_back(serialTime);
                }
            }
            std::fprintf(stderr, "shadow_scale_large main %zu 1 %zu %d %.1f %.1f %.1f %.1f %zu" "\n", objectCount, admitted, frames,
                MedianOf(together), WorstOf(together), MedianOf(serial), WorstOf(serial), pool.WorkerCount());
        }
        if (!movingText.empty()) {
            RunMovingSceneBenchmark(objectCount, ParseCountList(movingText), frames, shuffled);
        }
        for (const bool cascades : { true, false }) {
            if (cascades ? !withCascades : !withPoints) continue;
            std::vector<MeshPipelineBuildResult> togetherResults(cascades ? 4U : 24U);
            std::vector<MeshPipelineBuildResult> serialResults(1U);
            std::vector<double> together;
            std::vector<double> serial;
            std::size_t passes = 0U;
            std::size_t admitted = 0U;
            for (int frame = -kWarmupFrames; frame < frames; ++frame) {
                const std::vector<SceneRenderCamera> cameras = cascades ? CasterCascadeCameras(frame + kWarmupFrames, cascadeReach)
                                                                        : CasterPointFaceCameras(frame + kWarmupFrames, pointRange);
                passes = cameras.size();
                std::size_t togetherAdmitted = 0U;
                const double togetherTime = ShadowFrameMicroseconds(batches, mesh, cameras, &pool, ShadowBuildMode::Together, togetherResults, togetherAdmitted);
                admitted = togetherAdmitted;
                if (frame >= 0) together.push_back(togetherTime);
                if (withSerial) {
                    std::size_t serialAdmitted = 0U;
                    const double serialTime = ShadowFrameMicroseconds(batches, mesh, cameras, nullptr, ShadowBuildMode::OneByOne, serialResults, serialAdmitted);
                    Require(serialAdmitted == togetherAdmitted, "The shadow passes built on one thread admitted other instances than the ones built together");
                    if (frame >= 0) serial.push_back(serialTime);
                }
            }
            std::fprintf(stderr, "shadow_scale_large %s %zu %zu %zu %d %.1f %.1f %.1f %.1f %zu\n", cascades ? "cascades" : "points",
                objectCount, passes, admitted, frames, MedianOf(together), WorstOf(together), MedianOf(serial), WorstOf(serial), pool.WorkerCount());
        }
    }
}

} // namespace

// Prints the median time of one BaseOpaque pass build over scenes of 1 000 to 50 000 mesh objects, built on the calling
// thread and with workers, each with the workers awake and asleep. No window and no GPU are involved.
void RunMeshPipelineBuildScaleBenchmark() {
    auto camera = PerspectiveCamera();
    SceneDepthPolicy::MakePerspective(camera.projection.data(), 90.0F, 1.0F, 0.1F, 1000.0F, SceneDepthPolicy::HomogeneousDepth());
    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{} };
    pool.Start(kb::ecs::WorkerPoolConfig{});

    std::fprintf(stderr, "mesh_pipeline_scale median microseconds of one pass build, %zu workers\n", pool.WorkerCount());
    std::fprintf(stderr, "mesh_pipeline_scale levels_of_detail pairs objects batches serial serial_paused parallel parallel_paused\n");
    struct Shape { bool levelsOfDetail; std::size_t pairs; };
    for (const Shape shape : { Shape{ true, 16U }, Shape{ false, 16U }, Shape{ true, 1U } }) {
        const RenderMeshResource mesh = BenchmarkMesh(shape.levelsOfDetail);
        for (const std::size_t objectCount : { std::size_t{ 1000U }, std::size_t{ 2000U }, std::size_t{ 4000U }, std::size_t{ 10000U }, std::size_t{ 50000U } }) {
            RenderScene scene;
            FillBenchmarkScene(scene, objectCount, shape.pairs);
            const std::vector<SceneMeshBatch> batches = SceneMeshBatchBuilder::Build(scene.DrawGroups());
            // Every object is drawn once per section of its level.
            const std::size_t visibleCount = objectCount * (shape.levelsOfDetail ? 2U : 1U);
            const auto measure = [&](kb::ecs::WorkerPool* workerPool, std::chrono::milliseconds pause, int runs) {
                return MedianBuildMicroseconds(batches, mesh, camera, workerPool, pause, runs, visibleCount);
            };
            const double serial = measure(nullptr, std::chrono::milliseconds{ 0 }, 200);
            const double serialPaused = measure(nullptr, std::chrono::milliseconds{ 1 }, 100);
            const double parallel = measure(&pool, std::chrono::milliseconds{ 0 }, 200);
            const double parallelPaused = measure(&pool, std::chrono::milliseconds{ 1 }, 100);
            std::fprintf(stderr, "mesh_pipeline_scale %d %zu %zu %zu %.1f %.1f %.1f %.1f\n", shape.levelsOfDetail ? 1 : 0, shape.pairs,
                objectCount, batches.size(), serial, serialPaused, parallel, parallelPaused);
        }
    }
}

// Prints the median time of the shadow passes of one frame (four directional cascades, or four point lights of six faces)
// over scenes of 1 000 to 50 000 shadow casters, built one after the other and together, on the calling thread and with
// workers, each with the calling thread busy before the passes and without. No window and no GPU are involved.
// With KB_SHADOW_SCALE_SIZES set to a comma separated list of object counts it measures large scene sizes instead
// (see RunShadowLargeSceneBenchmark), so the usual run stays as short as it was.
void RunMeshPipelineShadowBuildScaleBenchmark() {
    if (const std::string largeSizes = EnvironmentValue("KB_SHADOW_SCALE_SIZES"); !largeSizes.empty()) {
        RunShadowLargeSceneBenchmark(largeSizes);
        return;
    }
    kb::ecs::WorkerPool pool{ kb::ecs::WorkerPoolConfig{} };
    pool.Start(kb::ecs::WorkerPoolConfig{});
    const std::vector<SceneRenderCamera> cascades = ShadowCascadeCameras();
    const std::vector<SceneRenderCamera> pointFaces = ShadowPointFaceCameras();

    std::fprintf(stderr, "shadow_scale median microseconds of the shadow passes of one frame, %zu workers\n", pool.WorkerCount());
    std::fprintf(stderr, "shadow_scale stage levels_of_detail objects passes admitted serial serial_busy one_by_one one_by_one_busy together together_busy\n");
    const std::chrono::microseconds busy{ 300 };
    const std::chrono::microseconds none{ 0 };
    for (const bool levelsOfDetail : { false, true }) {
        const RenderMeshResource mesh = BenchmarkMesh(levelsOfDetail);
        for (const std::size_t objectCount : { std::size_t{ 1000U }, std::size_t{ 10000U }, std::size_t{ 50000U } }) {
            const int runs = static_cast<int>(std::clamp<std::size_t>(500000U / objectCount, 6U, 100U));
            RenderScene scene;
            FillBenchmarkScene(scene, objectCount, 16U);
            const std::vector<SceneMeshBatch> batches = SceneMeshBatchBuilder::Build(scene.DrawGroups());
            struct Stage { const char* name; std::span<const SceneRenderCamera> cameras; };
            for (const Stage stage : { Stage{ "cascades", cascades }, Stage{ "points", pointFaces } }) {
                std::size_t admitted = 0U;
                const auto measure = [&](kb::ecs::WorkerPool* workerPool, ShadowBuildMode mode, std::chrono::microseconds pause) {
                    return MedianShadowStageMicroseconds(batches, mesh, stage.cameras, workerPool, mode, pause, runs, admitted);
                };
                const double serial = measure(nullptr, ShadowBuildMode::OneByOne, none);
                const double serialBusy = measure(nullptr, ShadowBuildMode::OneByOne, busy);
                const double oneByOne = measure(&pool, ShadowBuildMode::OneByOne, none);
                const double oneByOneBusy = measure(&pool, ShadowBuildMode::OneByOne, busy);
                const double together = measure(&pool, ShadowBuildMode::Together, none);
                const double togetherBusy = measure(&pool, ShadowBuildMode::Together, busy);
                std::fprintf(stderr, "shadow_scale %s %d %zu %zu %zu %.1f %.1f %.1f %.1f %.1f %.1f\n", stage.name, levelsOfDetail ? 1 : 0,
                    objectCount, stage.cameras.size(), admitted, serial, serialBusy, oneByOne, oneByOneBusy, together, togetherBusy);
            }
        }
    }
}

void RunMeshPipelineTests() {
    {
        const SceneRenderMeshInstance occluder{.entityId = 1U, .meshAssetId = 42U, .shadowOnly = true};
        RenderMaterialResource material{};
        for (const auto pass : {MeshPassType::BaseOpaque, MeshPassType::Depth, MeshPassType::MotionVectors, MeshPassType::GBuffer, MeshPassType::SelectionId}) {
            Require(!MeshPipelinePassPolicy::CanEverContain(pass, occluder, {}, UINT32_MAX) &&
                !MeshPipelinePassPolicy::Accepts(pass, occluder, &material, {}, UINT32_MAX), "Tile occluders must never appear in color, depth or selection passes");
        }
        Require(MeshPipelinePassPolicy::CanEverContain(MeshPassType::ShadowDepth, occluder, {}, UINT32_MAX) &&
            MeshPipelinePassPolicy::Accepts(MeshPassType::ShadowDepth, occluder, &material, {}, UINT32_MAX), "Tile occluders contribute shadow depth");
    }
    auto shear = IdentityMatrix();
    shear[4] = 1.0F;
    const auto shearedBounds = MeshPipelineVisibility::TransformBounds(RenderBoundsSphere{.radius = 1.0F}, shear);
    for (int sample = 0; sample < 360; ++sample) {
        const float angle = sample * 0.0174532925F;
        const float x = std::cos(angle) + std::sin(angle), y = std::sin(angle);
        Require(shearedBounds.radius >= std::sqrt(x*x + y*y), "World bounds must enclose sheared geometry");
    }
    RunTemporalCullingConservatismTest();
    RunSceneMeshCommandReuseInvalidationTest();
    RunSceneMeshBatchCommandRetentionTest();
    RunMotionVectorPassDrawsOnlyMovingInstancesTest();
    // Compare accelerated generated ranges with the original per-instance path.
    // Camera motion, shear, off-centre bounds, growth and mixed owners must not
    // alter accepted ids or pass counters.
    {
        RenderScene scene;
        auto model = IdentityMatrix();
        model[4] = 1.5F; model[5] = 0.4F; model[10] = -2.0F; model[14] = 180.0F;
        GeometrySwarmRenderProxyDesc swarm{.entityId = 42U, .meshAssetId = 7U,
            .model = model, .instanceCount = 513U, .columns = 16U, .rows = 2U, .layers = 32U,
            .spacing = {20.0F, -2.0F, 20.0F}, .instanceScale = 1.3F};
        static_cast<void>(scene.UpsertGeometrySwarm(swarm));
        static_cast<void>(scene.DrawGroups());
        swarm.instanceCount = 900U;
        static_cast<void>(scene.UpsertGeometrySwarm(swarm));
        swarm.entityId = 77U; swarm.model[12] = 600.0F; swarm.castsShadow = false;
        static_cast<void>(scene.UpsertGeometrySwarm(swarm));
        static_cast<void>(scene.UpsertMesh(MeshRenderProxyDesc{.entityId = 2U, .meshAssetId = 7U, .model = TranslationMatrix(0.0F, 0.0F, 10.0F)}));
        auto groups = scene.DrawGroups();
        Require(std::ranges::any_of(groups, [](const auto& group) { return !group.visibilityClusters.empty(); }),
            "Swarm ranges must retain visibility acceleration");
        auto reference = groups;
        for (auto& group : reference) group.visibilityClusters.clear();
        RenderMeshResource mesh{};
        mesh.indexCount = 3U;
        mesh.bounds = {.center = {3.0F, 1.0F, 2.0F}, .radius = 2.0F};
        RenderMaterialResource material{};
        const std::array<std::uint64_t, 2> selected{groups.front().instances.front().entityId,
            groups.back().instances.front().entityId};
        for (const std::size_t lodCount : {0U, 1U}) {
            mesh.lods.assign(lodCount, RenderMeshLodDesc{});
            for (const auto pass : {MeshPassType::BaseOpaque, MeshPassType::MotionVectors, MeshPassType::ShadowDepth,
                    MeshPassType::SelectionId, MeshPassType::EditorSelection}) {
                for (const float offset : {-200.0F, 0.0F, 150.0F}) {
                    auto camera = PerspectiveCamera();
                    camera.view[12] = offset;
                    SceneDepthPolicy::MakePerspective(camera.projection.data(), 90.0F, 1.0F, 0.01F, 400.0F, SceneDepthPolicy::HomogeneousDepth());
                    const auto build = [&](const auto& source) {
                        return MeshPipelineProcessor::Build(MeshPipelineBuildDesc{.pass = pass, .drawGroups = &source,
                            .resolvedMeshResource = &mesh, .resolvedMaterialResource = &material, .camera = &camera,
                            .selectedEntityIds = selected, .resourceValidation = MeshPipelineResourceValidation::Skip});
                    };
                    const auto accelerated = build(groups), original = build(reference);
                    const auto ids = [](const auto& result) {
                        std::vector<std::uint64_t> values;
                        for (const auto& command : result.commands)
                            for (const auto& instance : command.instances) values.push_back(instance.entityId);
                        std::ranges::sort(values);
                        return values;
                    };
                    Require(ids(accelerated) == ids(original) && accelerated.stats.visibleMeshCount == original.stats.visibleMeshCount &&
                        accelerated.stats.culledInstanceCount == original.stats.culledInstanceCount &&
                        accelerated.stats.lodSelectionCount == original.stats.lodSelectionCount,
                        "Cluster culling must match per-instance visibility, selection and LOD counters under camera motion and shear");
                }
            }
        }
    }
    for (const bool homogeneous : {false, true}) {
        for (const bool orthographic : {false, true}) {
          for (const float nearClip : {0.1F, 0.01F, 0.001F}) {
            SceneRenderCamera camera{};
            camera.view = IdentityMatrix();
            if (orthographic) SceneDepthPolicy::MakeOrthographic(camera.projection.data(), 10.0F, 1.0F, nearClip, 1200.0F, homogeneous);
            else SceneDepthPolicy::MakePerspective(camera.projection.data(), 90.0F, 1.0F, nearClip, 1200.0F, homogeneous);
            const auto frustum = MeshPipelineVisibility::BuildFrustum(&camera, homogeneous);
            Require(MeshPipelineVisibility::IsInsideFrustum(frustum, RenderBoundsSphere{.center = {0.0F, 0.0F, 10.0F}, .radius = 0.01F}), "Frustum rejected visible reverse-Z geometry");
            Require(!MeshPipelineVisibility::IsInsideFrustum(frustum, RenderBoundsSphere{.center = {0.0F, 0.0F, 2500.0F}, .radius = 1.0F}), "Frustum failed to reject geometry beyond the far clip");
            Require(!MeshPipelineVisibility::IsInsideFrustum(frustum, RenderBoundsSphere{.center = {0.0F, 0.0F, nearClip * 0.1F}, .radius = nearClip * 0.01F}), "Frustum failed to reject geometry before the near clip");
          }
        }
    }
    RunMeshLodRespectsCameraProjectionTest();
    RunMeshPassTypeNamesResolveTest();
    RunStaticMeshVertexLayoutIsDefinedTest();
    RunFramePassKindsMapToMeshPassesTest();
    RunSceneMeshBatchBuilderCreatesStableViewsTest();
    RunMeshPipelineBuildsFromSceneMeshBatchesTest();
    RunMeshPipelineSplitsSkinnedPalettesAndPreservesMotionHistoryTest();
    RunMeshPipelineSkinnedCommandStorageScalesWithInstancesTest();
    RunMeshPipelineReportsMissingMeshBindingPerInstanceTest();
    RunMeshPipelineReportsMissingOcclusionTextureBindingTest();
    RunMeshPipelineCanBuildPassCommandsWithoutResourceValidationTest();
    RunMeshPipelineBuildIntoReusesCommandInstanceCapacityTest();
    RunMeshPipelineFiltersShadowCastingInstancesTest();
    RunMeshPipelineFiltersSelectionInstancesTest();
    RunMeshPipelineFiltersByCameraCullingMaskTest();
    RunMeshPipelineReportsShadowPassMissingResourcesOnlyForCastersTest();
    RunSceneRendererValidatesExplicitMeshPassTest();
    RunMeshPipelineBuildsCommandsPerSectionAndMaterialSlotTest();
    RunMeshPipelineUsesMeshDefaultMaterialWhenMainMaterialIsEmptyTest();
    RunMeshPipelineSplitsSectionCommandsByMaterialSlotOverrideTest();
    RunMeshPipelineDrawBudgetDropsOverflowCommandsTest();
    RunMeshPipelineSortsCommandsBySortKeyTest();
    RunMeshPipelineRoutesMaterialAlphaModesToPassesTest();
    RunMeshPipelineUsesMaterialDoubleSidedStateTest();
    RunMeshPipelineCullsBackFacesForSingleSidedMeshesTest();
    RunMeshPipelineSeparatesMirroredInstancesTest();
    RunMeshPipelineKeepsBlendDisabledUntilTransparentPassIsReadyTest();
    RunMeshPipelineSortsInterleavedTransparentInstancesTest();
    RunMeshPipelineKeepsTerrainOverlayLayerOrderTest();
    RunMeshPipelineCpuCullsByFrustumBoundsTest();
    RunMeshPipelineAnimatedBoundsOverrideControlsCullingTest();
    RunMeshPipelineCullsWithVisibilityBlockerTest();
    RunMeshPipelineCullsInvisibleVisibilityCellsTest();
    RunMeshPipelineSelectsLodAndCarriesMeshletRangesTest();
    RunMeshPipelineCoordinatesDetailSwitchGroupsWithHysteresisTest();
    RunGpuDrivenFeatureClassifierGatesByCapabilitiesTest();
    RunGpuDrivenFeatureClassifierReportsIndirectFallbackTest();
    RunMeshPipelineAppliesGpuDrivenRuntimeFallbackPolicyTest();
    RunMeshPipelineParityValidationTracksDroppedBudgetTest();
    RunGpuDrivenFeatureClassifierReportsMeshletFallbackTest();
    RunGpuDrivenParityValidatorAcceptsMatchingRecordsTest();
    RunGpuDrivenParityValidatorReportsCullingLodAndMeshletMismatchesTest();
    RunGpuDrivenParityValidatorReportsMissingRecordsAndDropBudgetTest();
    RunRenderScenePropagatesMeshPassFlagsTest();
    RunMeshPipelineParallelScanMatchesSerialTest();
    RunMeshPipelineBuildsPassesTogetherMatchesSerialTest();
    RunSceneRendererPreparedShadowPassesMatchOneByOneTest();
    RunMeshPipelineParallelScanReportsMissingResourcesInOrderTest();
    RunVisibilityBoundsClassificationIsConservativeTest();
    RunVisibilityIndexStaysInStepWithInstancesTest();
    RunMeshPipelineVisibilityIndexMatchesScanTest();
    RunMeshPipelineVisibilityIndexFollowsChangingSceneTest();
    RunMeshPipelineVisibilityIndexOnWorkersMatchesSerialTest();
    RunMeshPipelineVisibilityIndexMatchesScanWithResourcesTest();
}

} // namespace kb::render::tests
