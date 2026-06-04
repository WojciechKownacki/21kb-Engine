#pragma once

#include "kb/render/scene/MeshPipeline.hpp"

#include <cstdint>
#include <utility>

namespace kb::render {

class MeshPipelineGpuDrivenRecorder {
public:
    MeshPipelineGpuDrivenRecorder() = delete;

    [[nodiscard]] static bool IsCandidate(const RenderMeshResource* mesh) noexcept;
    static void AccumulateCandidateStats(
        SceneRenderSubmitStats& stats,
        const RenderMeshResource* mesh,
        std::pair<std::uint32_t, std::uint32_t> meshletRange) noexcept;
    static void Record(
        MeshPipelineBuildResult& result,
        const SceneRenderMeshInstance& instance,
        std::uint32_t drawCommandIndex,
        std::uint8_t lodLevel,
        std::pair<std::uint32_t, std::uint32_t> meshletRange,
        bool visible,
        bool dropped);
    static void Finalize(
        MeshPipelineBuildResult& result,
        SceneGpuDrivenFeatureSupport support,
        std::uint32_t droppedInstanceBudget) noexcept;
};

} // namespace kb::render
