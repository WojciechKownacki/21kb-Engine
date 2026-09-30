#include "scene/pipeline/MeshPipelineGpuDrivenRecorder.hpp"

#include <algorithm>
#include <cstdint>

namespace kb::render {

bool MeshPipelineGpuDrivenRecorder::IsCandidate(const RenderMeshResource* mesh) noexcept {
    return mesh != nullptr &&
        (mesh->gpuCullingEnabled || mesh->indirectDrawsEnabled || mesh->meshletCullingEnabled);
}

void MeshPipelineGpuDrivenRecorder::AccumulateCandidateStats(
    SceneRenderSubmitStats& stats,
    const RenderMeshResource* mesh,
    std::pair<std::uint32_t, std::uint32_t> meshletRange) noexcept {
    if (mesh == nullptr) {
        return;
    }
    if (mesh->gpuCullingEnabled) {
        ++stats.gpuDrivenDrawCandidateCount;
    }
    if (mesh->indirectDrawsEnabled) {
        ++stats.indirectDrawCandidateCount;
    }
    if (mesh->meshletCullingEnabled) {
        stats.meshletCullingCandidateCount += std::max<std::uint32_t>(meshletRange.second, 1U);
    }
}

void MeshPipelineGpuDrivenRecorder::Record(
    MeshPipelineBuildResult& result,
    std::uint64_t entityId,
    const RenderBoundsSphere& worldBounds,
    std::uint32_t drawCommandIndex,
    std::uint8_t lodLevel,
    std::pair<std::uint32_t, std::uint32_t> meshletRange,
    bool dropped) {
    result.gpuDrivenInputRecords.push_back(SceneGpuDrivenInputRecord{
        .entityId = entityId,
        .worldBounds = {
            worldBounds.center[0],
            worldBounds.center[1],
            worldBounds.center[2],
            worldBounds.radius,
        },
        .drawCommandIndex = drawCommandIndex,
        .lodLevel = lodLevel,
        .firstMeshlet = meshletRange.first,
        .meshletCount = meshletRange.second,
    });
    result.stats.gpuDrivenParityCpuDroppedInstanceCount += dropped ? 1U : 0U;
}

void MeshPipelineGpuDrivenRecorder::Finalize(
    MeshPipelineBuildResult& result,
    SceneGpuDrivenFeatureSupport support,
    std::uint32_t droppedInstanceBudget) noexcept {
    result.stats.gpuDrivenInputInstanceCount = static_cast<std::uint32_t>(result.gpuDrivenInputRecords.size());
    const std::uint32_t droppedCount = result.stats.gpuDrivenParityCpuDroppedInstanceCount;
    result.stats.gpuDrivenParityCpuDroppedInstanceCount = 0U;
    const SceneGpuDrivenFeatureRequest gpuDrivenRequest{
        .gpuCullingRequested = result.stats.gpuDrivenDrawCandidateCount != 0U,
        .indirectDrawRequested = result.stats.indirectDrawCandidateCount != 0U,
        .meshletSubmitRequested = result.stats.meshletCullingCandidateCount != 0U,
    };
    if (!gpuDrivenRequest.HasAnyRequest()) {
        return;
    }

    const SceneGpuDrivenFeatureDecision gpuDrivenDecision = SceneGpuDrivenFeatureClassifier::Decide(
        gpuDrivenRequest,
        support);
    result.stats.gpuDrivenFeatureState = gpuDrivenDecision.state;
    result.stats.gpuDrivenCounterSource = gpuDrivenDecision.counterSource;
    result.stats.gpuDrivenFallbackReason = gpuDrivenDecision.fallbackReason;
    result.stats.gpuDrivenFallbackCount = gpuDrivenDecision.UsesFallback() ? 1U : 0U;
    if (gpuDrivenDecision.state != SceneGpuDrivenFeatureState::CpuValidationOnly) {
        return;
    }

    // CPU fallback has one record source. Comparing it with itself cannot find a GPU mismatch.
    result.stats.gpuDrivenParityValidationStatus = droppedInstanceBudget != 0U &&
        droppedCount > droppedInstanceBudget ?
        SceneGpuDrivenParityValidationStatus::DroppedInstanceBudgetExceeded :
        SceneGpuDrivenParityValidationStatus::Valid;
    result.stats.gpuDrivenParityMismatchEntityId = 0U;
    result.stats.gpuDrivenParityValidationCount = result.stats.gpuDrivenInputInstanceCount;
    result.stats.gpuDrivenParityCpuDroppedInstanceCount = droppedCount;
    result.stats.gpuDrivenParityGpuDroppedInstanceCount = droppedCount;
}

} // namespace kb::render
