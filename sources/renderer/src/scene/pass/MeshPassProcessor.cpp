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
#include <unordered_map>
#include <unordered_set>
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
    case MeshPassType::MotionVectors:
        return false;
    case MeshPassType::Depth:
    case MeshPassType::BaseOpaque:
    case MeshPassType::GBuffer:
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
    case MeshPassType::GBuffer:
    case MeshPassType::ShadowDepth:
    case MeshPassType::MotionVectors:
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

[[nodiscard]] std::uint64_t DetailSwitchKey(const SceneRenderMeshInstance& instance) noexcept {
    return instance.detailSwitchGroupId != 0U ? instance.detailSwitchGroupId : (instance.entityId ^ 0x9e3779b97f4a7c15ULL);
}

struct DetailSwitchCandidate {
    std::uint64_t policyEntityId = UINT64_MAX;
    float coverage = 0.0F;
    std::uint8_t desiredLod = UINT8_MAX;
    std::uint32_t minimumLod = 0U;
    std::uint32_t maximumLod = 255U;
    float promoteCoverage = 0.20F;
    float demoteCoverage = 0.15F;
};

void ResolveDetailSwitchLevels(const MeshPassProcessorDesc& desc, MeshPipelineBuildResult& result) {
    result.detailSwitchLevels.clear();
    const bool advancesHistory = desc.pass == MeshPassType::BaseOpaque || desc.pass == MeshPassType::GBuffer;
    std::unordered_map<std::uint64_t, DetailSwitchCandidate> candidates;
    for (const SceneMeshBatch& batch : desc.meshBatches) {
        const RenderMeshResource* mesh = nullptr;
        if (desc.resourceValidation == MeshPipelineResourceValidation::ResolveAndValidate) {
            const RenderMeshHandle handle = desc.resourceMap->ResolveMesh(batch.meshAssetId);
            mesh = desc.resources->FindMesh(handle);
        } else {
            mesh = desc.resolvedMeshResource;
        }
        if (mesh == nullptr || mesh->lods.empty()) continue;
        for (const SceneRenderMeshInstance& instance : batch.instances) {
            if (!instance.detailSwitchEnabled) continue;
            const std::uint64_t key = DetailSwitchKey(instance);
            DetailSwitchCandidate& candidate = candidates[key];
            const std::uint8_t desired = MeshPipelineVisibility::SelectLodLevel(mesh, instance, desc.camera);
            candidate.desiredLod = std::min(candidate.desiredLod, desired);
            const RenderBoundsSphere bounds = MeshPipelineVisibility::TransformBounds(mesh->bounds, instance.model);
            const float depth = std::max(std::abs(MeshPipelineVisibility::ViewDepth(desc.camera, bounds)), bounds.radius);
            candidate.coverage = std::max(candidate.coverage, std::clamp(bounds.radius / std::max(depth, 0.0001F), 0.0F, 1.0F));
            if (instance.entityId < candidate.policyEntityId) {
                candidate.policyEntityId = instance.entityId;
                candidate.minimumLod = instance.detailSwitchMinimumLod;
                candidate.maximumLod = instance.detailSwitchMaximumLod;
                candidate.promoteCoverage = instance.detailSwitchPromoteCoverage;
                candidate.demoteCoverage = instance.detailSwitchDemoteCoverage;
            }
        }
    }
    if (advancesHistory) {
        std::unordered_set<std::uint64_t> activeKeys;
        activeKeys.reserve(candidates.size());
        for (const auto& [key, candidate] : candidates) {
            static_cast<void>(candidate);
            activeKeys.emplace(key);
        }
        for (auto previous = result.detailSwitchPreviousLevels.begin(); previous != result.detailSwitchPreviousLevels.end();) {
            if (!activeKeys.contains(previous->first)) {
                previous = result.detailSwitchPreviousLevels.erase(previous);
            } else {
                ++previous;
            }
        }
    }
    for (const auto& [key, candidate] : candidates) {
        const std::uint8_t requested = static_cast<std::uint8_t>(std::clamp<std::uint32_t>(candidate.desiredLod, candidate.minimumLod, candidate.maximumLod));
        std::uint8_t resolved = requested;
        if (const auto previous = result.detailSwitchPreviousLevels.find(key); previous != result.detailSwitchPreviousLevels.end()) {
            resolved = previous->second;
            if (advancesHistory) {
                if (requested < resolved && candidate.coverage >= candidate.promoteCoverage) resolved = requested;
                if (requested > resolved && candidate.coverage < candidate.demoteCoverage) resolved = requested;
            }
        }
        result.detailSwitchLevels.emplace(key, resolved);
        if (advancesHistory) {
            result.detailSwitchPreviousLevels.insert_or_assign(key, resolved);
        }
    }
}

} // namespace

void MeshPassProcessor::BuildCommandsInto(const MeshPassProcessorDesc& desc, MeshPipelineBuildResult& result) noexcept {
    const bool validateResources = desc.resourceValidation == MeshPipelineResourceValidation::ResolveAndValidate;
    const MeshPipelineFrustum frustum = MeshPipelineVisibility::BuildFrustum(desc.camera);
    const std::uint32_t cullingMask = desc.camera != nullptr ? desc.camera->cullingMask : 0xFFFFFFFFU;
    ResolveDetailSwitchLevels(desc, result);
    result.commands.reserve(desc.meshBatches.size());

    std::size_t writeCommandCount = 0U;
    std::uint32_t acceptedInstanceCount = 0U;
    for (const SceneMeshBatch& batch : desc.meshBatches) {
        const std::uint32_t instanceCount = MeshPipelinePassPolicy::CountCandidateInstances(desc.pass, batch, desc.selectedEntityIds, cullingMask);
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
                desc.selectedEntityIds,
                cullingMask);
            meshHandle = resolvedMesh.handle;
            meshResource = resolvedMesh.resource;
            if (meshResource == nullptr) {
                continue;
            }
        }
        if (!validateResources) {
            meshResource = desc.resolvedMeshResource;
        }
        if (desc.terrainLayersOnly &&
            (desc.pass != MeshPassType::BaseTransparent || meshResource == nullptr || meshResource->terrainLayerCount <= 1U)) {
            continue;
        }

        const RenderMeshSection fallbackSection{
            .indexStart = 0U,
            .indexCount = meshResource == nullptr ? 0U : meshResource->indexCount,
            .vertexStart = 0U,
            .vertexCount = meshResource == nullptr ? 0U : meshResource->vertexCount,
            .materialSlot = 0U,
            .bounds = meshResource == nullptr ? RenderBoundsSphere{} : meshResource->bounds,
        };
        const std::vector<RenderMeshSection>* sections = meshResource == nullptr ? nullptr : &meshResource->sections;
        const std::uint32_t sectionCount = sections == nullptr || sections->empty() ? 1U : static_cast<std::uint32_t>(sections->size());
        for (std::uint32_t sectionIndex = 0U; sectionIndex < sectionCount; ++sectionIndex) {
            const RenderMeshSection& section = sections == nullptr || sections->empty() ? fallbackSection : (*sections)[sectionIndex];
            const std::uint32_t sectionVertexCount = section.vertexCount != 0U
                ? section.vertexCount
                : (meshResource != nullptr && section.vertexStart < meshResource->vertexCount
                      ? meshResource->vertexCount - section.vertexStart
                      : 0U);
            if (desc.terrainLayersOnly &&
                (section.terrainLayerIndex == UINT8_MAX || section.terrainLayerIndex == 0U)) {
                continue;
            }
            const std::pair<std::uint32_t, std::uint32_t> meshletRange = MeshPipelineVisibility::MeshletRangeForSection(meshResource, sectionIndex);
            result.commandLookupScratch.clear();
            result.commandLookupScratch.reserve(instanceCount);
            std::uint32_t culledForSection = 0U;
            for (SceneRenderMeshInstance instance : batch.instances) {
                if (!MeshPipelinePassPolicy::CanEverContain(desc.pass, instance, desc.selectedEntityIds, cullingMask)) {
                    continue;
                }
                std::uint8_t selectedLod = MeshPipelineVisibility::SelectLodLevel(meshResource, instance, desc.camera);
                if (instance.detailSwitchEnabled) {
                    if (const auto selected = result.detailSwitchLevels.find(DetailSwitchKey(instance)); selected != result.detailSwitchLevels.end()) {
                        selectedLod = static_cast<std::uint8_t>(std::min<std::uint32_t>(selected->second, meshResource == nullptr || meshResource->lods.empty() ? 0U : static_cast<std::uint32_t>(meshResource->lods.size() - 1U)));
                    }
                }
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
                if (!MeshPipelinePassPolicy::Accepts(desc.pass, instance, materialResource, desc.selectedEntityIds, cullingMask, &section)) {
                    continue;
                }
                const RenderBoundsSphere localBounds = instance.boundsOverride.IsValid()
                    ? instance.boundsOverride
                    : (section.bounds.IsValid() ? section.bounds : (meshResource == nullptr ? RenderBoundsSphere{} : meshResource->bounds));
                instance.worldBounds = MeshPipelineVisibility::TransformBounds(localBounds, instance.model);
                const bool gpuDrivenCandidate = MeshPipelineGpuDrivenRecorder::IsCandidate(meshResource);
                if (!MeshPipelineVisibility::IsInsideFrustum(frustum, instance.worldBounds)) {
                    if (gpuDrivenCandidate) {
                        MeshPipelineGpuDrivenRecorder::Record(result, instance, UINT32_MAX, selectedLod, meshletRange, false, false);
                    }
                    ++culledForSection;
                    continue;
                }
                if (desc.pass != MeshPassType::ShadowDepth && desc.pass != MeshPassType::Gizmo &&
                    MeshPipelineVisibility::IsOccludedByVisibilityBlockers(desc.camera, instance.worldBounds, desc.visibilityBlockers)) {
                    if (gpuDrivenCandidate) MeshPipelineGpuDrivenRecorder::Record(result, instance, UINT32_MAX, selectedLod, meshletRange, false, false);
                    ++culledForSection;
                    continue;
                }
                instance.depthBucket = MeshPipelineVisibility::DepthBucket(MeshPipelineVisibility::ViewDepth(desc.camera, instance.worldBounds));
                const MeshCommandLookupKey commandKey{
                    .materialAssetId = materialAssetId,
                    .materialHandleValue = materialHandle.value,
                    .currentSkinningPalette = instance.currentSkinningPalette,
                    .previousSkinningPalette = instance.previousSkinningPalette,
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
                    const std::uint64_t commandState = MeshPipelinePassPolicy::State(desc.pass, meshResource, materialResource, &section);
                    const std::uint64_t materialTextureDependencySignature = PassUsesMaterialTextureDependencies(desc.pass)
                        ? SceneMaterialTextureDependencySignature::Build(SceneMaterialTextureDependencyDesc{
                              .material = materialResource,
                              .resources = desc.resources,
                              .resourceMap = desc.resourceMap,
                          })
                        : 0U;
                    const SceneCachedDrawCommandDesc cachedCommandDesc{
                        .pass = desc.pass,
                        .meshAssetId = batch.meshAssetId,
                        .materialAssetId = materialAssetId,
                        .sectionIndex = sectionIndex,
                        .materialSlot = section.materialSlot,
                        .firstMeshlet = meshletRange.first,
                        .meshletCount = meshletRange.second,
                        .indexStart = section.indexStart,
                        .indexCount = section.indexCount,
                        .vertexStart = section.vertexStart,
                        .vertexCount = sectionVertexCount,
                        .lodLevel = section.lodLevel,
                        .terrainLayerIndex = section.terrainLayerIndex,
                        .mesh = meshHandle,
                        .material = materialHandle,
                        .meshResource = meshResource,
                        .materialResource = materialResource,
                        .meshResourceVersion = meshResource == nullptr ? 0U : meshResource->version,
                        .materialResourceVersion = materialResource == nullptr ? 0U : materialResource->version,
                        .materialTextureDependencySignature = materialTextureDependencySignature,
                        .state = commandState,
                    };
                    const SceneCachedDrawCommand& cachedCommand =
                        SceneDrawCommandCache::Resolve(result.drawCommandCache, cachedCommandDesc, result.stats);
                    command = &MeshPipelineCommandBuilder::WritableCommand(result, writeCommandCount);
                    SceneCachedDrawCommandMaterializer::ApplyTemplate(cachedCommand, *command);
                    command->currentSkinningPalette = instance.currentSkinningPalette;
                    command->previousSkinningPalette = instance.previousSkinningPalette;
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
