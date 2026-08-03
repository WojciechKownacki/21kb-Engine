#include "RendererTestSupport.hpp"

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

#include <array>
#include <string_view>

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

void RunMeshPassTypeNamesResolveTest() {
    Require(MeshPassTypeName(MeshPassType::Depth) == std::string_view("Depth"), "MeshPassTypeName did not resolve Depth");
    Require(MeshPassTypeName(MeshPassType::BaseOpaque) == std::string_view("BaseOpaque"), "MeshPassTypeName did not resolve BaseOpaque");
    Require(MeshPassTypeName(MeshPassType::GBuffer) == std::string_view("GBuffer"), "MeshPassTypeName did not resolve GBuffer");
    Require(MeshPassTypeName(MeshPassType::BaseTransparent) == std::string_view("BaseTransparent"), "MeshPassTypeName did not resolve BaseTransparent");
    Require(MeshPassTypeName(MeshPassType::ShadowDepth) == std::string_view("ShadowDepth"), "MeshPassTypeName did not resolve ShadowDepth");
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
            .materialSlot = 0U,
            .bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 1.0F },
        },
        RenderMeshSection{
            .indexStart = 6U,
            .indexCount = 12U,
            .materialSlot = 1U,
            .bounds = RenderBoundsSphere{ .center = { 0.0F, 0.0F, 0.0F }, .radius = 1.0F },
        },
    };
    const std::vector<RenderMaterialSlot> materialSlots{
        RenderMaterialSlot{ .defaultMaterialAssetId = 101U },
        RenderMaterialSlot{ .defaultMaterialAssetId = 102U },
    };
    RenderMeshResource mesh{};
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
    Require(result.commands[1].materialAssetId == 999U, "MeshPipeline did not prefer the Mesh Renderer main material over section slot 1 default material");
    Require(result.commands[1].indexStart == 6U && result.commands[1].indexCount == 12U, "MeshPipeline section 1 did not preserve index range");
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

    const SceneRenderCamera camera{
        .view = IdentityMatrix(),
        .projection = IdentityMatrix(),
    };
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
    const std::array<SceneRenderVisibilityBlocker, 1U> blockers{{
        SceneRenderVisibilityBlocker{ .entityId = 2U, .model = TranslationMatrix(0.0F, 0.0F, 0.5F), .size = { 0.2F, 0.2F, 0.2F } },
    }};
    const MeshPipelineBuildResult result = MeshPipelineProcessor::Build(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque, .drawGroups = &drawGroups, .resolvedMeshResource = &mesh,
        .camera = &camera, .visibilityBlockers = blockers, .resourceValidation = MeshPipelineResourceValidation::Skip,
    });
    Require(result.commands.empty(), "Visibility Blocker emitted a draw instead of rejecting the occluded instance");
    Require(result.stats.culledInstanceCount == 1U, "Visibility Blocker did not contribute to culling statistics");
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
    const SceneRenderCamera camera{ .view = IdentityMatrix(), .projection = IdentityMatrix() };
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
    MeshPipelineProcessor::BuildInto(MeshPipelineBuildDesc{
        .pass = MeshPassType::BaseOpaque, .drawGroups = &groups, .resolvedMeshResource = &mesh, .camera = &camera,
        .resourceValidation = MeshPipelineResourceValidation::Skip,
    }, result);
    Require(result.commands.size() == 1U && result.commands[0].lodLevel == 1U,
        "Detail Switch did not demote after leaving its hysteresis band");
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

} // namespace

void RunMeshPipelineTests() {
    RunMeshPassTypeNamesResolveTest();
    RunStaticMeshVertexLayoutIsDefinedTest();
    RunFramePassKindsMapToMeshPassesTest();
    RunSceneMeshBatchBuilderCreatesStableViewsTest();
    RunMeshPipelineBuildsFromSceneMeshBatchesTest();
    RunMeshPipelineSplitsSkinnedPalettesAndPreservesMotionHistoryTest();
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
    RunMeshPipelineKeepsBlendDisabledUntilTransparentPassIsReadyTest();
    RunMeshPipelineCpuCullsByFrustumBoundsTest();
    RunMeshPipelineCullsWithVisibilityBlockerTest();
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
}

} // namespace kb::render::tests
