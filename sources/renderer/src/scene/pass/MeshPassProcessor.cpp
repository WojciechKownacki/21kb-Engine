#include "scene/pass/MeshPassProcessor.hpp"

#include "kb/render/scene/cache/SceneCachedDrawCommand.hpp"
#include "scene/cache/SceneCachedDrawCommandMaterializer.hpp"
#include "scene/cache/SceneMaterialTextureDependencySignature.hpp"
#include "scene/pipeline/MeshPipelineCommandBuilder.hpp"
#include "scene/pipeline/MeshPipelineGpuDrivenRecorder.hpp"
#include "scene/pipeline/MeshPipelinePassPolicy.hpp"
#include "scene/pipeline/MeshPipelineResourceResolver.hpp"
#include "scene/pipeline/MeshPipelineVisibility.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>

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

[[nodiscard]] bool PassUsesMaterialTextureDependencies(MeshPassType pass) noexcept {
    switch (pass) {
    case MeshPassType::SelectionId:
    case MeshPassType::EditorSelection:
        return false;
    case MeshPassType::Depth:
    case MeshPassType::BaseOpaque:
    case MeshPassType::BaseTransparent:
    case MeshPassType::ShadowDepth:
    case MeshPassType::Gizmo:
        return true;
    }
    return true;
}


[[nodiscard]] bool PassDisablesAlphaBlend(MeshPassType pass) noexcept {
    switch (pass) {
    case MeshPassType::Depth:
    case MeshPassType::BaseOpaque:
    case MeshPassType::ShadowDepth:
        return true;
    case MeshPassType::BaseTransparent:
        // The transparent pass IS the alpha-blend pass (MAT-80); it must not skip blended materials.
        return false;
    case MeshPassType::SelectionId:
    case MeshPassType::EditorSelection:
    case MeshPassType::Gizmo:
        return false;
    }
    return true;
}

} // namespace

void MeshPassProcessor::BuildCommandsInto(const MeshPassProcessorDesc& desc, MeshPipelineBuildResult& result) noexcept {
    const bool validateResources = desc.resourceValidation == MeshPipelineResourceValidation::ResolveAndValidate;
    const MeshPipelineFrustum frustum = MeshPipelineVisibility::BuildFrustum(desc.camera);
    result.commands.reserve(desc.meshBatches.size());

    std::size_t writeCommandCount = 0U;
    std::uint32_t acceptedInstanceCount = 0U;
    for (const SceneMeshBatch& batch : desc.meshBatches) {
        const std::uint32_t instanceCount = MeshPipelinePassPolicy::CountCandidateInstances(desc.pass, batch, desc.selectedEntityIds);
        if (instanceCount == 0U) {
            continue;
        }

        RenderMeshHandle meshHandle{};
        const RenderMeshResource* meshResource = nullptr;
        if (validateResources) {
            const MeshPipelineResolvedMesh resolvedMesh = MeshPipelineResourceResolver::ResolveMeshBatch(
                desc.pass,
                batch,
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
            for (SceneRenderMeshInstance instance : batch.instances) {
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
                const std::uint64_t materialAssetId = MeshPipelineResourceResolver::MaterialAssetForSectionInstance(batch, instance, meshResource, section);
                RenderMaterialHandle materialHandle{};
                const RenderMaterialResource* materialResource = validateResources ? nullptr : desc.resolvedMaterialResource;
                if (validateResources) {
                    materialResource = MeshPipelineResourceResolver::ResolveMaterialOrFallback(instance, materialAssetId, *desc.resources, *desc.resourceMap, materialHandle, result.stats, desc.diagnostics);
                    MeshPipelineResourceResolver::ValidateMaterialTextureOrFallback(instance, materialAssetId, materialResource, *desc.resources, *desc.resourceMap, result.stats, desc.diagnostics);
                }
                if (PassDisablesAlphaBlend(desc.pass) && MeshPipelinePassPolicy::UsesDisabledAlphaBlend(materialResource)) {
                    // Blended materials are skipped from opaque/depth/shadow and render in the transparent
                    // pass instead (MAT-80); this is correct routing, not an unsupported-material error.
                    continue;
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
                    const std::uint64_t commandState = MeshPipelinePassPolicy::State(desc.pass, meshResource, materialResource);
                    const std::uint64_t materialTextureDependencySignature = PassUsesMaterialTextureDependencies(desc.pass)
                        ? SceneMaterialTextureDependencySignature::Build(SceneMaterialTextureDependencyDesc{
                              .material = materialResource,
                              .resources = desc.resources,
                              .resourceMap = desc.resourceMap,
                          })
                        : 0U;
                    const SceneCachedDrawCommand& cachedCommand = SceneDrawCommandCache::Resolve(result.drawCommandCache, SceneCachedDrawCommandDesc{
                        .pass = desc.pass,
                        .meshAssetId = batch.meshAssetId,
                        .materialAssetId = materialAssetId,
                        .sectionIndex = sectionIndex,
                        .materialSlot = section.materialSlot,
                        .firstMeshlet = meshletRange.first,
                        .meshletCount = meshletRange.second,
                        .indexStart = section.indexStart,
                        .indexCount = section.indexCount,
                        .lodLevel = section.lodLevel,
                        .mesh = meshHandle,
                        .material = materialHandle,
                        .meshResource = meshResource,
                        .materialResource = materialResource,
                        .meshResourceVersion = meshResource == nullptr ? 0U : meshResource->version,
                        .materialResourceVersion = materialResource == nullptr ? 0U : materialResource->version,
                        .materialTextureDependencySignature = materialTextureDependencySignature,
                        .state = commandState,
                    }, result.stats);
                    command = &MeshPipelineCommandBuilder::WritableCommand(result, writeCommandCount);
                    SceneCachedDrawCommandMaterializer::ApplyTemplate(cachedCommand, *command);
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
}

} // namespace kb::render
