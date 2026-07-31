#pragma once

#include "kb/render/scene/MeshPipeline.hpp"

#include <array>
#include <cstdint>
#include <utility>
#include <span>

namespace kb::render {

struct MeshPipelineFrustumPlane {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 0.0F;
};

struct MeshPipelineFrustum {
    std::array<MeshPipelineFrustumPlane, 6> planes{};
    bool valid = false;
};

class MeshPipelineVisibility {
public:
    MeshPipelineVisibility() = delete;

    [[nodiscard]] static MeshPipelineFrustum BuildFrustum(const SceneRenderCamera* camera) noexcept;
    [[nodiscard]] static bool IsInsideFrustum(const MeshPipelineFrustum& frustum, const RenderBoundsSphere& bounds) noexcept;
    [[nodiscard]] static RenderBoundsSphere TransformBounds(
        const RenderBoundsSphere& localBounds,
        const std::array<float, 16>& model) noexcept;
    [[nodiscard]] static float ViewDepth(const SceneRenderCamera* camera, const RenderBoundsSphere& bounds) noexcept;
    [[nodiscard]] static bool IsOccludedByVisibilityBlockers(
        const SceneRenderCamera* camera,
        const RenderBoundsSphere& bounds,
        std::span<const SceneRenderVisibilityBlocker> blockers) noexcept;
    [[nodiscard]] static std::uint16_t DepthBucket(float depth) noexcept;
    [[nodiscard]] static std::uint8_t SelectLodLevel(
        const RenderMeshResource* mesh,
        const SceneRenderMeshInstance& instance,
        const SceneRenderCamera* camera) noexcept;
    [[nodiscard]] static std::pair<std::uint32_t, std::uint32_t> MeshletRangeForSection(
        const RenderMeshResource* mesh,
        std::uint32_t sectionIndex) noexcept;
};

} // namespace kb::render
