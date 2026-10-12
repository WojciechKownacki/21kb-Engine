#pragma once

#include "engine/scene/ScenePortalVisibility.hpp"
#include "kb/render/scene/MeshPipeline.hpp"

#include <array>
#include <cstdint>
#include <utility>
#include <span>

namespace kb::render {

class RenderResourceRegistry;
class SceneRenderResourceMap;

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
    [[nodiscard]] static MeshPipelineFrustum BuildFrustum(const SceneRenderCamera* camera, bool homogeneousDepth) noexcept;
    [[nodiscard]] static bool IsInsideFrustum(const MeshPipelineFrustum& frustum, const RenderBoundsSphere& bounds) noexcept;
    [[nodiscard]] static RenderBoundsSphere TransformBounds(
        const RenderBoundsSphere& localBounds,
        const std::array<float, 16>& model) noexcept;
    [[nodiscard]] static float ViewDepth(const SceneRenderCamera* camera, const RenderBoundsSphere& bounds) noexcept;
    [[nodiscard]] static float ScreenCoverage(const SceneRenderCamera* camera, const RenderBoundsSphere& bounds) noexcept;
    [[nodiscard]] static bool IsOccludedByVisibilityBlockers(
        const SceneRenderCamera* camera,
        const RenderBoundsSphere& bounds,
        std::span<const SceneRenderVisibilityBlocker> blockers) noexcept;
    [[nodiscard]] static std::uint16_t DepthBucket(float depth) noexcept;
    // Unreal's Max World Position Offset Displacement: the world units a graph material may move its
    // vertices, which every culling path adds to the bounds of the objects drawn with it.
    [[nodiscard]] static float MaterialBoundsMargin(const RenderMaterialResource* material) noexcept;
    [[nodiscard]] static float MaterialBoundsMargin(
        const RenderResourceRegistry& resources,
        const SceneRenderResourceMap& resourceMap,
        std::uint64_t materialAssetId,
        std::span<const std::uint64_t> slotMaterialAssetIds) noexcept;
    [[nodiscard]] static RenderBoundsSphere GrowBounds(RenderBoundsSphere bounds, float margin) noexcept;
    // The camera's culling view-projection, position and layers as the portal visibility pass takes them.
    [[nodiscard]] static kb::scene::ScenePortalCamera PortalCamera(const SceneRenderCamera& camera) noexcept;
    [[nodiscard]] static std::uint8_t SelectLodLevel(
        const RenderMeshResource* mesh,
        const SceneRenderMeshInstance& instance,
        const SceneRenderCamera* camera) noexcept;
    [[nodiscard]] static std::pair<std::uint32_t, std::uint32_t> MeshletRangeForSection(
        const RenderMeshResource* mesh,
        std::uint32_t sectionIndex) noexcept;
};

} // namespace kb::render
