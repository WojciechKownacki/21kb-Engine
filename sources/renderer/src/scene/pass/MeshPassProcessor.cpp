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
#include <array>
#include <cmath>
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

[[nodiscard]] bool PassNeedsPerInstanceCandidateFilter(MeshPassType pass) noexcept {
    switch (pass) {
    case MeshPassType::ShadowDepth:
    case MeshPassType::SelectionId:
    case MeshPassType::EditorSelection:
        return true;
    case MeshPassType::Depth:
    case MeshPassType::BaseOpaque:
    case MeshPassType::GBuffer:
    case MeshPassType::BaseTransparent:
    case MeshPassType::MotionVectors:
    case MeshPassType::Gizmo:
        return false;
    }
    return true;
}

[[nodiscard]] std::uint64_t DetailSwitchKey(const SceneRenderMeshInstance& instance) noexcept {
    return instance.detailSwitchGroupId != 0U ? instance.detailSwitchGroupId : (instance.entityId ^ 0x9e3779b97f4a7c15ULL);
}

[[nodiscard]] bool ReversesWinding(const std::array<float, 16>& m) noexcept {
    const double determinant = double(m[0]) * (double(m[5]) * m[10] - double(m[9]) * m[6])
        - double(m[4]) * (double(m[1]) * m[10] - double(m[9]) * m[2])
        + double(m[8]) * (double(m[1]) * m[6] - double(m[5]) * m[2]);
    return determinant < 0.0;
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
        if (mesh == nullptr || mesh->lods.size() <= 1U) continue;
        for (const SceneRenderMeshInstance& instance : batch.instances) {
            if (!instance.detailSwitchEnabled) continue;
            const std::uint64_t key = DetailSwitchKey(instance);
            DetailSwitchCandidate& candidate = candidates[key];
            const std::uint8_t desired = MeshPipelineVisibility::SelectLodLevel(mesh, instance, desc.camera);
            candidate.desiredLod = std::min(candidate.desiredLod, desired);
            const RenderBoundsSphere bounds = MeshPipelineVisibility::TransformBounds(
                instance.boundsOverride.IsValid() ? instance.boundsOverride : mesh->bounds, instance.model);
            candidate.coverage = std::max(candidate.coverage, MeshPipelineVisibility::ScreenCoverage(desc.camera, bounds));
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
        const bool wholeBatchIsCandidate = cullingMask == 0xFFFFFFFFU && !PassNeedsPerInstanceCandidateFilter(desc.pass);
        const std::uint32_t instanceCount = wholeBatchIsCandidate
            ? static_cast<std::uint32_t>(batch.instances.size())
            : MeshPipelinePassPolicy::CountCandidateInstances(desc.pass, batch, desc.selectedEntityIds, cullingMask);
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
            std::uint32_t culledForSection = 0U;
            std::uint64_t lastMaterialAssetId = 0U;
            MeshPipelineMaterialResolution lastMaterialResolution{};
            std::size_t clusterIndex = 0U;
            for (std::size_t instanceIndex = 0U; instanceIndex < batch.instances.size(); ++instanceIndex) {
                const SceneRenderMeshInstance& instance = batch.instances[instanceIndex];
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
                    if (materialAssetId == lastMaterialAssetId && lastMaterialResolution.resource != nullptr) {
                        materialHandle = lastMaterialResolution.handle;
                        materialResource = lastMaterialResolution.resource;
                    } else {
                        const auto cached = result.materialResolutionScratch.find(materialAssetId);
                        if (cached != result.materialResolutionScratch.end()) {
                            materialHandle = cached->second.handle;
                            materialResource = cached->second.resource;
                        } else {
                            materialResource = MeshPipelineResourceResolver::ResolveMaterialOrFallback(instance, materialAssetId, *desc.resources, *desc.resourceMap, materialHandle, result.stats, desc.diagnostics);
                            MeshPipelineResourceResolver::ValidateMaterialTextureOrFallback(instance, materialAssetId, materialResource, *desc.resources, *desc.resourceMap, result.stats, desc.diagnostics);
                            if (materialHandle.IsValid() && materialResource != nullptr) {
                                result.materialResolutionScratch.emplace(materialAssetId, MeshPipelineMaterialResolution{
                                    .handle = materialHandle,
                                    .resource = materialResource,
                                });
                            }
                        }
                        lastMaterialAssetId = materialAssetId;
                        lastMaterialResolution = MeshPipelineMaterialResolution{
                            .handle = materialHandle,
                            .resource = materialHandle.IsValid() ? materialResource : nullptr,
                        };
                    }
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
                while (clusterIndex < batch.visibilityClusters.size() &&
                    batch.visibilityClusters[clusterIndex].firstInstance < instanceIndex) ++clusterIndex;
                if (clusterIndex < batch.visibilityClusters.size() &&
                    batch.visibilityClusters[clusterIndex].firstInstance == instanceIndex &&
                    localBounds.IsValid() && meshResource != nullptr && meshResource->lods.size() <= 1U &&
                    !instance.detailSwitchEnabled && !batch.hasMaterialSlotOverrides) {
                    const auto& cluster = batch.visibilityClusters[clusterIndex];
                    auto bounds = cluster.origins;
                    const auto& center = localBounds.center;
                    bounds.radius += cluster.maximumScale * (localBounds.radius +
                        std::sqrt(center[0]*center[0] + center[1]*center[1] + center[2]*center[2]));
                    if (cluster.instanceCount != 0U && cluster.instanceCount <= batch.instances.size()-instanceIndex &&
                        !MeshPipelineVisibility::IsInsideFrustum(frustum, bounds)) {
                        culledForSection += cluster.instanceCount;
                        instanceIndex += cluster.instanceCount - 1U;
                        continue;
                    }
                }
                const RenderBoundsSphere worldBounds = MeshPipelineVisibility::TransformBounds(localBounds, instance.model);
                const bool gpuDrivenCandidate = MeshPipelineGpuDrivenRecorder::IsCandidate(meshResource);
                if (!MeshPipelineVisibility::IsInsideFrustum(frustum, worldBounds)) {
                    ++culledForSection;
                    continue;
                }
                if (desc.pass != MeshPassType::ShadowDepth && desc.pass != MeshPassType::Gizmo &&
                    MeshPipelineVisibility::IsOccludedByVisibilityBlockers(desc.camera, worldBounds, desc.visibilityBlockers)) {
                    ++culledForSection;
                    continue;
                }
                const std::uint16_t depthBucket = MeshPipelineVisibility::DepthBucket(MeshPipelineVisibility::ViewDepth(desc.camera, worldBounds));
                const MeshCommandLookupKey commandKey{
                    .materialAssetId = materialAssetId,
                    .materialHandleValue = materialHandle.value,
                    .currentSkinningPalette = instance.currentSkinningPalette,
                    .previousSkinningPalette = instance.previousSkinningPalette,
                    .reversedWinding = !(meshResource != nullptr && meshResource->doubleSided) &&
                        !(materialResource != nullptr && materialResource->doubleSided) && ReversesWinding(instance.model),
                };
                const auto commandLookupIt = result.commandLookupScratch.find(commandKey);
                MeshDrawCommand* command = commandLookupIt == result.commandLookupScratch.end() ? nullptr : &result.commands[commandLookupIt->second];
                std::uint32_t drawCommandIndex = commandLookupIt == result.commandLookupScratch.end()
                    ? static_cast<std::uint32_t>(writeCommandCount)
                    : static_cast<std::uint32_t>(commandLookupIt->second);
                if (command == nullptr && desc.maxDrawCommands != 0U && writeCommandCount >= desc.maxDrawCommands) {
                    if (gpuDrivenCandidate) {
                        MeshPipelineGpuDrivenRecorder::Record(result, instance.entityId, worldBounds, UINT32_MAX, selectedLod, meshletRange, true);
                    }
                    ++result.stats.droppedInstanceCount;
                    EmitInstanceDiagnostic(desc.diagnostics, SceneRenderDiagnosticKind::DroppedInstances, SceneRenderDiagnosticSeverity::Warning, instance, materialAssetId);
                    continue;
                }
                if (desc.maxVisibleInstances != 0U && acceptedInstanceCount >= desc.maxVisibleInstances) {
                    if (gpuDrivenCandidate) {
                        MeshPipelineGpuDrivenRecorder::Record(result, instance.entityId, worldBounds, UINT32_MAX, selectedLod, meshletRange, true);
                    }
                    ++result.stats.droppedInstanceCount;
                    EmitInstanceDiagnostic(desc.diagnostics, SceneRenderDiagnosticKind::DroppedInstances, SceneRenderDiagnosticSeverity::Warning, instance, materialAssetId);
                    continue;
                }
                if (command == nullptr) {
                    std::uint64_t commandState = MeshPipelinePassPolicy::State(desc.pass, meshResource, materialResource, &section);
                    if (commandKey.reversedWinding) {
                        const auto cull = commandState & BGFX_STATE_CULL_MASK;
                        if (cull == BGFX_STATE_CULL_CCW || cull == BGFX_STATE_CULL_CW) {
                            commandState = (commandState & ~BGFX_STATE_CULL_MASK) |
                                (cull == BGFX_STATE_CULL_CCW ? BGFX_STATE_CULL_CW : BGFX_STATE_CULL_CCW);
                        }
                    }
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
                    result.commandLookupScratch.emplace(commandKey, writeCommandCount);
                    result.stats.meshCommandLookupCapacity = std::max<std::uint32_t>(
                        result.stats.meshCommandLookupCapacity,
                        static_cast<std::uint32_t>(result.commandLookupScratch.bucket_count()));
                    drawCommandIndex = static_cast<std::uint32_t>(writeCommandCount);
                    ++writeCommandCount;
                }
                MeshPipelineGpuDrivenRecorder::AccumulateCandidateStats(result.stats, meshResource, meshletRange);
                const std::uint32_t gpuDrivenRecordIndex = gpuDrivenCandidate
                    ? static_cast<std::uint32_t>(result.gpuDrivenInputRecords.size()) : UINT32_MAX;
                if (gpuDrivenCandidate) {
                    MeshPipelineGpuDrivenRecorder::Record(result, instance.entityId, worldBounds, drawCommandIndex, selectedLod, meshletRange, false);
                }
                command->sortKey += depthBucket;
                SceneRenderMeshInstance& accepted = command->instances.emplace_back(instance);
                accepted.worldBounds = worldBounds;
                accepted.depthBucket = depthBucket;
                if (desc.pass == MeshPassType::BaseTransparent) {
                    const float viewDepth = std::abs(MeshPipelineVisibility::ViewDepth(desc.camera, worldBounds));
                    result.transparentInstanceScratch.push_back(MeshPipelineTransparentInstanceRef{
                        .commandIndex = drawCommandIndex,
                        .instanceIndex = static_cast<std::uint32_t>(command->instances.size() - 1U),
                        .gpuDrivenRecordIndex = gpuDrivenRecordIndex,
                        .viewDepth = std::isfinite(viewDepth) ? viewDepth : 0.0F,
                    });
                }
                ++acceptedInstanceCount;
            }

            result.stats.culledInstanceCount += culledForSection;
        }
    }

    MeshPipelineCommandBuilder::FinalizeCommands(result, desc.pass, writeCommandCount,
        desc.maxDrawCommands, desc.diagnostics);
}

} // namespace kb::render
