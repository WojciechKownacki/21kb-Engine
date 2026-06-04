#include "kb/render/scene/MeshPipeline.hpp"

#include "kb/render/frame/RenderPassKind.hpp"
#include "scene/pipeline/MeshPipelineCommandBuilder.hpp"
#include "scene/pipeline/MeshPipelineGpuDrivenRecorder.hpp"
#include "scene/pipeline/MeshPipelinePassPolicy.hpp"
#include "scene/pipeline/MeshPipelineResourceResolver.hpp"
#include "scene/pipeline/MeshPipelineVisibility.hpp"

#include <algorithm>
#include <cstdint>

namespace kb::render {
namespace {

void EmitInstanceDiagnostic(
    SceneRenderDiagnostics* diagnostics,
    SceneRenderDiagnosticKind kind,
    SceneRenderDiagnosticSeverity severity,
    const SceneRenderMeshInstance& instance,
    std::uint64_t materialAssetId) {
    if (diagnostics == nullptr) {
        return;
    }

    diagnostics->events.push_back(SceneRenderDiagnosticEvent{
        .severity = severity,
        .kind = kind,
        .entityId = instance.entityId,
        .meshAssetId = instance.meshAssetId,
        .materialAssetId = materialAssetId,
        .instanceCount = 1U,
    });
}

} // namespace

const char* MeshPassTypeName(MeshPassType pass) noexcept {
    switch (pass) {
    case MeshPassType::Depth:
        return "Depth";
    case MeshPassType::BaseOpaque:
        return "BaseOpaque";
    case MeshPassType::BaseTransparent:
        return "BaseTransparent";
    case MeshPassType::ShadowDepth:
        return "ShadowDepth";
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
    case RenderPassKind::TransparentScene:
        return MeshPassType::BaseTransparent;
    case RenderPassKind::EditorSelectionMask:
        return MeshPassType::SelectionId;
    case RenderPassKind::SceneTargetSetup:
    case RenderPassKind::PostProcessBloomPrefilter:
    case RenderPassKind::PostProcessBloomBlurH:
    case RenderPassKind::PostProcessBloomBlurV:
    case RenderPassKind::PostProcessHdrCombine:
    case RenderPassKind::PostProcessHdrFinalize:
    case RenderPassKind::EditorSceneOverlays:
    case RenderPassKind::FinalComposite:
    case RenderPassKind::EditorUiComposite:
        return std::nullopt;
    }

    return std::nullopt;
}

std::size_t MeshCommandLookupKeyHash::operator()(MeshCommandLookupKey key) const noexcept {
    const std::uint64_t mixed = key.materialAssetId ^ (key.materialHandleValue + 0x9e3779b97f4a7c15ULL + (key.materialAssetId << 6U) + (key.materialAssetId >> 2U));
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
    result.stats = SceneRenderSubmitStats{};
    if (desc.drawGroups == nullptr) {
        result.commands.clear();
        return;
    }

    const bool validateResources = desc.resourceValidation == MeshPipelineResourceValidation::ResolveAndValidate;
    if (validateResources && (desc.resources == nullptr || desc.resourceMap == nullptr)) {
        result.commands.clear();
        return;
    }

    const MeshPipelineFrustum frustum = MeshPipelineVisibility::BuildFrustum(desc.camera);
    result.commands.reserve(desc.drawGroups->size());
    std::size_t writeCommandCount = 0U;
    std::uint32_t acceptedInstanceCount = 0U;
    for (const SceneRenderDrawGroup& group : *desc.drawGroups) {
        const std::uint32_t instanceCount = MeshPipelinePassPolicy::CountCandidateInstances(desc.pass, group, desc.selectedEntityIds);
        if (instanceCount == 0U) {
            continue;
        }

        RenderMeshHandle meshHandle{};
        const RenderMeshResource* meshResource = nullptr;
        if (validateResources) {
            const MeshPipelineResolvedMesh resolvedMesh = MeshPipelineResourceResolver::ResolveMeshGroup(
                desc.pass,
                group,
                instanceCount,
                *desc.resources,
                *desc.resourceMap,
                result.stats,
                desc.diagnostics,
                desc.selectedEntityIds);
            meshHandle = resolvedMesh.handle;
            meshResource = resolvedMesh.resource;
            if (meshResource == nullptr) {
                continue;
            }
        }
        if (!validateResources) {
            meshResource = desc.resolvedMeshResource;
        }

        const RenderMeshSection fallbackSection{
            .indexStart = 0U,
            .indexCount = meshResource == nullptr ? 0U : meshResource->indexCount,
            .materialSlot = 0U,
            .bounds = meshResource == nullptr ? RenderBoundsSphere{} : meshResource->bounds,
        };
        const std::vector<RenderMeshSection>* sections = meshResource == nullptr ? nullptr : &meshResource->sections;
        const std::uint32_t sectionCount = sections == nullptr || sections->empty() ? 1U : static_cast<std::uint32_t>(sections->size());
        for (std::uint32_t sectionIndex = 0U; sectionIndex < sectionCount; ++sectionIndex) {
            const RenderMeshSection& section = sections == nullptr || sections->empty() ? fallbackSection : (*sections)[sectionIndex];
            const std::pair<std::uint32_t, std::uint32_t> meshletRange = MeshPipelineVisibility::MeshletRangeForSection(meshResource, sectionIndex);
            result.commandLookupScratch.clear();
            result.commandLookupScratch.reserve(instanceCount);
            std::uint32_t culledForSection = 0U;
            for (SceneRenderMeshInstance instance : group.instances) {
                if (!MeshPipelinePassPolicy::CanEverContain(desc.pass, instance, desc.selectedEntityIds)) {
                    continue;
                }
                const std::uint8_t selectedLod = MeshPipelineVisibility::SelectLodLevel(meshResource, instance, desc.camera);
                if (meshResource != nullptr && !meshResource->lods.empty()) {
                    ++result.stats.lodSelectionCount;
                }
                if (selectedLod != section.lodLevel) {
                    continue;
                }
                const std::uint64_t materialAssetId = MeshPipelineResourceResolver::MaterialAssetForSectionInstance(group, instance, meshResource, section);
                RenderMaterialHandle materialHandle{};
                const RenderMaterialResource* materialResource = validateResources ? nullptr : desc.resolvedMaterialResource;
                if (validateResources) {
                    materialResource = MeshPipelineResourceResolver::ResolveMaterialOrFallback(instance, materialAssetId, *desc.resources, *desc.resourceMap, materialHandle, result.stats, desc.diagnostics);
                    MeshPipelineResourceResolver::ValidateMaterialTextureOrFallback(instance, materialAssetId, materialResource, *desc.resources, *desc.resourceMap, result.stats, desc.diagnostics);
                }
                if (!MeshPipelinePassPolicy::Accepts(desc.pass, instance, materialResource, desc.selectedEntityIds)) {
                    continue;
                }
                instance.worldBounds = MeshPipelineVisibility::TransformBounds(section.bounds.IsValid() ? section.bounds : (meshResource == nullptr ? RenderBoundsSphere{} : meshResource->bounds), instance.model);
                const bool gpuDrivenCandidate = MeshPipelineGpuDrivenRecorder::IsCandidate(meshResource);
                if (!MeshPipelineVisibility::IsInsideFrustum(frustum, instance.worldBounds)) {
                    if (gpuDrivenCandidate) {
                        MeshPipelineGpuDrivenRecorder::Record(result, instance, UINT32_MAX, selectedLod, meshletRange, false, false);
                    }
                    ++culledForSection;
                    continue;
                }
                instance.depthBucket = MeshPipelineVisibility::DepthBucket(MeshPipelineVisibility::ViewDepth(desc.camera, instance.worldBounds));
                const MeshCommandLookupKey commandKey{
                    .materialAssetId = materialAssetId,
                    .materialHandleValue = materialHandle.value,
                };
                const auto commandLookupIt = result.commandLookupScratch.find(commandKey);
                MeshDrawCommand* command = commandLookupIt == result.commandLookupScratch.end() ? nullptr : &result.commands[commandLookupIt->second];
                std::uint32_t drawCommandIndex = commandLookupIt == result.commandLookupScratch.end()
                    ? static_cast<std::uint32_t>(writeCommandCount)
                    : static_cast<std::uint32_t>(commandLookupIt->second);
                if (command == nullptr && desc.maxDrawCommands != 0U && writeCommandCount >= desc.maxDrawCommands) {
                    if (gpuDrivenCandidate) {
                        MeshPipelineGpuDrivenRecorder::Record(result, instance, UINT32_MAX, selectedLod, meshletRange, true, true);
                    }
                    ++result.stats.droppedInstanceCount;
                    EmitInstanceDiagnostic(desc.diagnostics, SceneRenderDiagnosticKind::DroppedInstances, SceneRenderDiagnosticSeverity::Warning, instance, materialAssetId);
                    continue;
                }
                if (desc.maxVisibleInstances != 0U && acceptedInstanceCount >= desc.maxVisibleInstances) {
                    if (gpuDrivenCandidate) {
                        MeshPipelineGpuDrivenRecorder::Record(result, instance, UINT32_MAX, selectedLod, meshletRange, true, true);
                    }
                    ++result.stats.droppedInstanceCount;
                    EmitInstanceDiagnostic(desc.diagnostics, SceneRenderDiagnosticKind::DroppedInstances, SceneRenderDiagnosticSeverity::Warning, instance, materialAssetId);
                    continue;
                }
                if (command == nullptr) {
                    command = &MeshPipelineCommandBuilder::WritableCommand(result, writeCommandCount);
                    command->pass = desc.pass;
                    command->meshAssetId = group.meshAssetId;
                    command->materialAssetId = materialAssetId;
                    command->sectionIndex = sectionIndex;
                    command->materialSlot = section.materialSlot;
                    command->firstMeshlet = meshletRange.first;
                    command->meshletCount = meshletRange.second;
                    command->indexStart = section.indexStart;
                    command->indexCount = section.indexCount;
                    command->lodLevel = section.lodLevel;
                    command->mesh = meshHandle;
                    command->material = materialHandle;
                    command->meshResource = meshResource;
                    command->materialResource = materialResource;
                    command->state = MeshPipelinePassPolicy::State(desc.pass, meshResource, materialResource);
                    command->instances.reserve(instanceCount);
                    result.stats.meshPipelineScratchInstanceCapacity += static_cast<std::uint32_t>(command->instances.capacity());
                    result.commandLookupScratch.emplace(commandKey, writeCommandCount);
                    result.stats.meshCommandLookupCapacity = std::max<std::uint32_t>(
                        result.stats.meshCommandLookupCapacity,
                        static_cast<std::uint32_t>(result.commandLookupScratch.bucket_count()));
                    drawCommandIndex = static_cast<std::uint32_t>(writeCommandCount);
                    ++writeCommandCount;
                }
                MeshPipelineGpuDrivenRecorder::AccumulateCandidateStats(result.stats, meshResource, meshletRange);
                if (gpuDrivenCandidate) {
                    MeshPipelineGpuDrivenRecorder::Record(result, instance, drawCommandIndex, selectedLod, meshletRange, true, false);
                }
                command->sortKey += instance.depthBucket;
                command->instances.push_back(instance);
                ++acceptedInstanceCount;
            }

            result.stats.culledInstanceCount += culledForSection;
        }
    }

    MeshPipelineCommandBuilder::FinalizeCommands(result, desc.pass, writeCommandCount);
    MeshPipelineGpuDrivenRecorder::Finalize(result, desc.gpuDrivenSupport, desc.maxDroppedInstances);
}

void MeshPipelineProcessor::CountCommandsAsSubmitted(SceneRenderSubmitStats& stats, const std::vector<MeshDrawCommand>& commands) noexcept {
    MeshPipelineCommandBuilder::CountCommandsAsSubmitted(stats, commands);
}

} // namespace kb::render
