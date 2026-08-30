#include "kb/render/scene/MeshPipeline.hpp"

#include "kb/render/frame/RenderPassKind.hpp"
#include "kb/render/scene/cache/SceneCachedDrawCommand.hpp"
#include "scene/pipeline/MeshPipelineCommandBuilder.hpp"
#include "scene/pipeline/MeshPipelineGpuDrivenRecorder.hpp"
#include "scene/batch/SceneMeshBatchSourceResolver.hpp"
#include "scene/pass/MeshPassProcessor.hpp"

namespace kb::render {

const char* MeshPassTypeName(MeshPassType pass) noexcept {
    switch (pass) {
    case MeshPassType::Depth:
        return "Depth";
    case MeshPassType::BaseOpaque:
        return "BaseOpaque";
    case MeshPassType::GBuffer:
        return "GBuffer";
    case MeshPassType::BaseTransparent:
        return "BaseTransparent";
    case MeshPassType::ShadowDepth:
        return "ShadowDepth";
    case MeshPassType::MotionVectors:
        return "MotionVectors";
    case MeshPassType::SelectionId:
        return "SelectionId";
    case MeshPassType::EditorSelection:
        return "EditorSelection";
    case MeshPassType::Gizmo:
        return "Gizmo";
    }

    return "Unknown";
}

std::optional<MeshPassType> MeshPassForRenderPassKind(RenderPassKind kind) noexcept {
    switch (kind) {
    case RenderPassKind::ShadowDepth:
        return MeshPassType::ShadowDepth;
    case RenderPassKind::OpaqueScene:
        return MeshPassType::BaseOpaque;
    case RenderPassKind::GBufferGeometry:
        return MeshPassType::GBuffer;
    case RenderPassKind::TransparentScene:
        return MeshPassType::BaseTransparent;
    case RenderPassKind::EditorSelectionMask:
        return MeshPassType::SelectionId;
    case RenderPassKind::PostProcessMotionVectors:
        return MeshPassType::MotionVectors;
    case RenderPassKind::SceneTargetSetup:
    case RenderPassKind::DeferredLighting:
    case RenderPassKind::PostProcessBloomPrefilter:
    case RenderPassKind::PostProcessBloomBlurH:
    case RenderPassKind::PostProcessBloomBlurV:
    case RenderPassKind::PostProcessHdrCombine:
    case RenderPassKind::PostProcessExposureReadback:
    case RenderPassKind::PostProcessTaaResolve:
    case RenderPassKind::PostProcessHdrFinalize:
    case RenderPassKind::EditorSceneOverlays:
    case RenderPassKind::FinalComposite:
    case RenderPassKind::EditorUiComposite:
    case RenderPassKind::EditorGizmoOverlay:
        return std::nullopt;
    }

    return std::nullopt;
}

std::size_t MeshCommandLookupKeyHash::operator()(MeshCommandLookupKey key) const noexcept {
    std::uint64_t mixed = key.materialAssetId ^ (key.materialHandleValue + 0x9e3779b97f4a7c15ULL + (key.materialAssetId << 6U) + (key.materialAssetId >> 2U));
    mixed ^= key.currentSkinningPalette.frame + (mixed << 6U) + (mixed >> 2U);
    mixed ^= static_cast<std::uint64_t>(key.currentSkinningPalette.firstMatrix) << 32U;
    mixed ^= key.previousSkinningPalette.frame + (mixed << 6U) + (mixed >> 2U);
    mixed ^= static_cast<std::uint64_t>(key.previousSkinningPalette.firstMatrix) << 16U;
    return static_cast<std::size_t>(mixed);
}

MeshPipelineBuildResult MeshPipelineProcessor::Build(const MeshPipelineBuildDesc& desc) noexcept {
    MeshPipelineBuildResult result{};
    BuildInto(desc, result);
    return result;
}

void MeshPipelineProcessor::BuildInto(const MeshPipelineBuildDesc& desc, MeshPipelineBuildResult& result) noexcept {
    for (MeshDrawCommand& command : result.commands) {
        command.instances.clear();
    }
    result.gpuDrivenInputRecords.clear();
    result.gpuDrivenCpuValidationRecords.clear();
    result.commandLookupScratch.clear();
    result.stats = SceneRenderSubmitStats{};
    const bool hasBatchSource = desc.meshBatches != nullptr || desc.drawGroups != nullptr;
    const std::span<const SceneMeshBatch> meshBatches = SceneMeshBatchSourceResolver::Resolve(SceneMeshBatchSourceDesc{
        .meshBatches = desc.meshBatches,
        .drawGroups = desc.drawGroups,
    }, result.meshBatchScratch);
    if (meshBatches.empty()) {
        result.commands.clear();
        if (hasBatchSource) {
            SceneDrawCommandCache::BeginBuild(result.drawCommandCache, desc.pass);
            SceneDrawCommandCache::EndBuild(result.drawCommandCache, desc.pass, result.stats);
        }
        return;
    }

    if (desc.resourceValidation == MeshPipelineResourceValidation::ResolveAndValidate &&
        (desc.resources == nullptr || desc.resourceMap == nullptr)) {
        result.commands.clear();
        return;
    }

    SceneDrawCommandCache::BeginBuild(result.drawCommandCache, desc.pass);
    MeshPassProcessor::BuildCommandsInto(MeshPassProcessorDesc{
        .pass = desc.pass,
        .meshBatches = meshBatches,
        .resources = desc.resources,
        .resourceMap = desc.resourceMap,
        .resolvedMeshResource = desc.resolvedMeshResource,
        .resolvedMaterialResource = desc.resolvedMaterialResource,
        .camera = desc.camera,
        .visibilityBlockers = desc.visibilityBlockers,
        .diagnostics = desc.diagnostics,
        .maxDrawCommands = desc.maxDrawCommands,
        .maxVisibleInstances = desc.maxVisibleInstances,
        .selectedEntityIds = desc.selectedEntityIds,
        .resourceValidation = desc.resourceValidation,
        .terrainLayersOnly = desc.terrainLayersOnly,
    }, result);
    SceneDrawCommandCache::EndBuild(result.drawCommandCache, desc.pass, result.stats);
    result.meshBatchScratch.clear();
    MeshPipelineGpuDrivenRecorder::Finalize(result, desc.gpuDrivenSupport, desc.maxDroppedInstances);
}

void MeshPipelineProcessor::CountCommandsAsSubmitted(SceneRenderSubmitStats& stats, const std::vector<MeshDrawCommand>& commands) noexcept {
    MeshPipelineCommandBuilder::CountCommandsAsSubmitted(stats, commands);
}

} // namespace kb::render
