#include "kb/render/scene/MeshPipeline.hpp"
#include "scene/cache/SceneMeshBatchCommandCache.hpp"

#include "kb/render/frame/RenderPassKind.hpp"
#include "kb/render/scene/cache/SceneCachedDrawCommand.hpp"
#include "scene/pipeline/MeshPipelineCommandBuilder.hpp"
#include "scene/pipeline/MeshPipelineGpuDrivenRecorder.hpp"
#include "scene/batch/SceneMeshBatchSourceResolver.hpp"
#include "scene/pass/MeshPassProcessor.hpp"
#include "scene/pipeline/MeshPipelineVisibility.hpp"

#include <algorithm>

namespace kb::render {
namespace {
// Meshlets retain the section's vertex binding: their index ranges already
// reference that binding. Splitting changes index spans and conservative bounds.
void ExpandMeshletCommands(const MeshPipelineBuildDesc& desc, MeshPipelineBuildResult& result) {
    if (!desc.gpuDrivenSupport.meshletSubmitSupported || !desc.gpuDrivenSupport.runtimeGpuDispatchSupported) return;
    if (std::none_of(result.commands.begin(), result.commands.end(), [](const MeshDrawCommand& draw) {
        return draw.gpuFrustumCull && draw.meshResource != nullptr &&
            draw.meshResource->meshletCullingEnabled && draw.meshletCount != 0U;
    })) return;
    constexpr std::size_t maximumInstanceRecords = 1U << 20U;
    constexpr std::size_t maximumMeshletDraws = 4096U;
    std::vector<MeshDrawCommand> expanded;
    expanded.reserve(result.commands.size());
    std::size_t instanceRecords = 0U;
    for (auto& command : result.commands) {
        const auto* mesh = command.meshResource;
        bool valid = command.gpuFrustumCull && mesh != nullptr && mesh->meshletCullingEnabled &&
            command.meshletCount != 0U && command.firstMeshlet <= mesh->meshlets.size() &&
            command.meshletCount <= mesh->meshlets.size() - command.firstMeshlet;
        std::uint64_t nextIndex = command.indexStart;
        if (valid) {
            valid = command.meshletCount <= maximumMeshletDraws - std::min(expanded.size(), maximumMeshletDraws) &&
                command.instances.size() <= (maximumInstanceRecords - std::min(instanceRecords, maximumInstanceRecords)) / command.meshletCount;
            for (std::uint32_t i = 0U; valid && i < command.meshletCount; ++i) {
                const auto& m = mesh->meshlets[command.firstMeshlet + i];
                valid = m.IsValid() && m.sectionIndex == command.sectionIndex && m.lodLevel == command.lodLevel &&
                    m.indexStart == nextIndex && m.indexStart >= command.indexStart &&
                    m.indexCount <= static_cast<std::uint64_t>(command.indexStart) + command.indexCount - nextIndex;
                nextIndex += m.indexCount;
            }
            valid = valid && nextIndex == static_cast<std::uint64_t>(command.indexStart) + command.indexCount;
        }
        if (!valid) {
            expanded.push_back(std::move(command));
            if (command.gpuFrustumCull && mesh != nullptr && mesh->meshletCullingEnabled) ++result.stats.gpuDrivenFallbackCount;
            continue;
        }
        for (std::uint32_t i = 0U; i < command.meshletCount; ++i) {
            const auto index = command.firstMeshlet + i;
            const auto& meshlet = mesh->meshlets[index];
            auto& draw = expanded.emplace_back(command);
            draw.firstMeshlet = index;
            draw.meshletCount = 1U;
            draw.indexStart = meshlet.indexStart;
            draw.indexCount = meshlet.indexCount;
            draw.gpuMeshletDraw = true;
            draw.instanceRevision = 0U;
            const auto margin = MeshPipelineVisibility::MaterialBoundsMargin(command.materialResource);
            for (auto& instance : draw.instances) {
                const auto local = instance.boundsOverride.IsValid() ? instance.boundsOverride : meshlet.bounds;
                instance.worldBounds = MeshPipelineVisibility::GrowBounds(
                    MeshPipelineVisibility::TransformBounds(local, instance.model), margin);
            }
            instanceRecords += draw.instances.size();
        }
    }
    result.commands = std::move(expanded);
    result.stats.meshPipelineCommandCount = static_cast<std::uint32_t>(result.commands.size());
    result.stats.meshPipelineCommandCapacity = static_cast<std::uint32_t>(result.commands.capacity());
    result.stats.meshPipelineSortKeyCount = result.stats.meshPipelineCommandCount;
}
} // namespace


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
    case RenderPassKind::ScreenUIBlurH:
    case RenderPassKind::ScreenUIBlurV:
    case RenderPassKind::FinalComposite:
    case RenderPassKind::ScreenUIComposite:
    case RenderPassKind::EditorUiComposite:
    case RenderPassKind::EditorGizmoOverlay:
    case RenderPassKind::PostProcessMaterial0:
    case RenderPassKind::PostProcessMaterial1:
    case RenderPassKind::PostProcessMaterial2:
    case RenderPassKind::PostProcessMaterial3:
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
    mixed ^= key.reversedWinding ? 0x85ebca77c2b2ae63ULL : 0ULL;
    mixed ^= key.meshAssetId * 0x85ebca77c2b2ae63ULL;
    mixed ^= static_cast<std::uint64_t>(key.sectionIndex) * 0xc2b2ae3d27d4eb4fULL;
    return static_cast<std::size_t>(mixed);
}

MeshPipelineBuildResult MeshPipelineProcessor::Build(const MeshPipelineBuildDesc& desc) noexcept {
    MeshPipelineBuildResult result{};
    BuildInto(desc, result);
    return result;
}

void MeshPipelineProcessor::BuildInto(const MeshPipelineBuildDesc& desc, MeshPipelineBuildResult& result) noexcept {
    auto* batchCache = !desc.gpuDrivenSupport.meshletSubmitSupported &&
        desc.pass != MeshPassType::BaseTransparent && desc.selectedEntityIds.empty() &&
        desc.visibilityBlockers.empty() && desc.portalVisibility == nullptr ? desc.batchCommandCache : nullptr;
    if (batchCache == nullptr && desc.batchCommandCache != nullptr) desc.batchCommandCache->Reset();
    if (batchCache != nullptr) batchCache->BeginBuild(desc.pass, result);
    else for (MeshDrawCommand& command : result.commands) command.instances.clear();
    result.gpuDrivenInputRecords.clear();
    result.transparentInstanceScratch.clear();
    ClearKeepingNodes(result.commandLookupScratch, result.commandLookupNodes);
    ClearKeepingNodes(result.materialResolutionScratch, result.materialResolutionNodes);
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
        if (batchCache != nullptr) batchCache->EndBuild(result);
        return;
    }

    if (desc.resourceValidation == MeshPipelineResourceValidation::ResolveAndValidate &&
        (desc.resources == nullptr || desc.resourceMap == nullptr)) {
        result.commands.clear();
        if (batchCache != nullptr) batchCache->EndBuild(result);
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
        .portalVisibility = desc.portalVisibility,
        .diagnostics = desc.diagnostics,
        .maxDrawCommands = desc.maxDrawCommands,
        .maxVisibleInstances = desc.maxVisibleInstances,
        .selectedEntityIds = desc.selectedEntityIds,
        .resourceValidation = desc.resourceValidation,
        .terrainLayersOnly = desc.terrainLayersOnly,
        .batchCommandCache = batchCache,
        .gpuDrivenSupport = desc.gpuDrivenSupport,
    }, result);
    ExpandMeshletCommands(desc, result);
    SceneDrawCommandCache::EndBuild(result.drawCommandCache, desc.pass, result.stats);
    result.meshBatchScratch.clear();
    MeshPipelineGpuDrivenRecorder::Finalize(result, desc.gpuDrivenSupport, desc.maxDroppedInstances);
    if (batchCache != nullptr) batchCache->EndBuild(result);
}

void MeshPipelineProcessor::CountCommandsAsSubmitted(SceneRenderSubmitStats& stats, const std::vector<MeshDrawCommand>& commands) noexcept {
    MeshPipelineCommandBuilder::CountCommandsAsSubmitted(stats, commands);
}

} // namespace kb::render
