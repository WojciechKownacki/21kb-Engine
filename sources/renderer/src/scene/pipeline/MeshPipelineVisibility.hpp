#pragma once

#include "engine/scene/ScenePortalVisibility.hpp"
#include "kb/render/scene/MeshPipeline.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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
    // The corners of the frustum with every plane moved out by `reach`: `cornerBase + reach * cornerSlope`. Whatever the
    // planes let through once moved out lies within the box of these corners; a set that is clear of that box is turned away
    // by the planes together, though none of them turns it away alone. Not valid when three planes do not meet.
    std::array<std::array<float, 3>, 8> cornerBase{};
    std::array<std::array<float, 3>, 8> cornerSlope{};
    bool cornersValid = false;
};

// How a set of instances, whose origins lie in a box, stands to the frustum.
enum class MeshPipelineBoundsVisibility : std::uint8_t {
    // IsInsideFrustum would turn every instance away.
    Outside,
    // Some might be turned away, some might not.
    Partial,
    // IsInsideFrustum would turn none away.
    Inside,
};

class MeshPipelineVisibility {
public:
    MeshPipelineVisibility() = delete;

    [[nodiscard]] static MeshPipelineFrustum BuildFrustum(const SceneRenderCamera* camera) noexcept;
    [[nodiscard]] static MeshPipelineFrustum BuildFrustum(const SceneRenderCamera* camera, bool homogeneousDepth) noexcept;
    [[nodiscard]] static bool IsInsideFrustum(const MeshPipelineFrustum& frustum, const RenderBoundsSphere& bounds) noexcept;
    // IsInsideFrustum for every instance whose origin lies in `bounds` and whose bounds, which a model scaled by at most
    // `bounds.maximumScale` makes, reach `reach` beyond the origin at most. The answers are never wrong in the direction
    // that matters: Outside only when every such instance is outside, Inside only when none is.
    [[nodiscard]] static MeshPipelineBoundsVisibility ClassifyBounds(
        const MeshPipelineFrustum& frustum,
        const SceneMeshVisibilityBounds& bounds,
        float reach) noexcept;
    // ClassifyBounds for one instance: true only when IsInsideFrustum would turn away an instance at `origin` whose bounds
    // reach `reach` beyond it at most.
    [[nodiscard]] static bool IsReachOutsideFrustum(
        const MeshPipelineFrustum& frustum,
        const std::array<float, 3>& origin,
        float reach) noexcept;
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
    // Max World Position Offset Displacement: the world units a graph material may move its
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
    // `coverageScale` multiplies the instance's screen coverage before the level is chosen; the hysteresis of a
    // skinned mesh asks for the level a little nearer and a little farther than it is.
    [[nodiscard]] static std::uint8_t SelectLodLevel(
        const RenderMeshResource* mesh,
        const SceneRenderMeshInstance& instance,
        const SceneRenderCamera* camera,
        float coverageScale = 1.0F) noexcept;
    [[nodiscard]] static std::pair<std::uint32_t, std::uint32_t> MeshletRangeForSection(
        const RenderMeshResource* mesh,
        std::uint32_t sectionIndex) noexcept;
};

// The arithmetic of the two tests below and that of IsInsideFrustum round differently: a margin in proportion to the size of
// the terms keeps a set they turn away from holding an instance that IsInsideFrustum would let through. The planes are
// unit planes, so no term of a distance is larger than its operand.
inline constexpr float kBoundsRoundingMargin = 1.0e-5F;

inline MeshPipelineBoundsVisibility MeshPipelineVisibility::ClassifyBounds(
    const MeshPipelineFrustum& frustum,
    const SceneMeshVisibilityBounds& bounds,
    float reach) noexcept {
    if (!frustum.valid || bounds.unbounded) {
        return MeshPipelineBoundsVisibility::Partial;
    }
    const float terms = std::abs(bounds.center[0]) + std::abs(bounds.center[1]) + std::abs(bounds.center[2]) +
        bounds.halfExtent[0] + bounds.halfExtent[1] + bounds.halfExtent[2] + reach;
    bool inside = true;
    for (const MeshPipelineFrustumPlane& plane : frustum.planes) {
        const float distance = plane.x * bounds.center[0] + plane.y * bounds.center[1] + plane.z * bounds.center[2] + plane.w;
        const float spread = std::abs(plane.x) * bounds.halfExtent[0] + std::abs(plane.y) * bounds.halfExtent[1] +
            std::abs(plane.z) * bounds.halfExtent[2];
        const float margin = kBoundsRoundingMargin * (terms + std::abs(plane.w));
        if (distance + spread < -reach - margin) {
            return MeshPipelineBoundsVisibility::Outside;
        }
        inside = inside && distance - spread - reach >= margin;
    }
    if (inside) {
        return MeshPipelineBoundsVisibility::Inside;
    }
    if (frustum.cornersValid) {
        for (std::size_t axis = 0U; axis < 3U; ++axis) {
            float low = frustum.cornerBase[0][axis] + reach * frustum.cornerSlope[0][axis];
            float high = low;
            for (std::size_t corner = 1U; corner < frustum.cornerBase.size(); ++corner) {
                const float value = frustum.cornerBase[corner][axis] + reach * frustum.cornerSlope[corner][axis];
                low = std::min(low, value);
                high = std::max(high, value);
            }
            const float tolerance = kBoundsRoundingMargin * (std::abs(low) + std::abs(high) + terms);
            if (bounds.center[axis] - bounds.halfExtent[axis] > high + tolerance ||
                bounds.center[axis] + bounds.halfExtent[axis] < low - tolerance) {
                return MeshPipelineBoundsVisibility::Outside;
            }
        }
    }
    return MeshPipelineBoundsVisibility::Partial;
}

inline bool MeshPipelineVisibility::IsReachOutsideFrustum(
    const MeshPipelineFrustum& frustum,
    const std::array<float, 3>& origin,
    float reach) noexcept {
    if (!frustum.valid) {
        return false;
    }
    const float terms = std::abs(origin[0]) + std::abs(origin[1]) + std::abs(origin[2]) + reach;
    for (const MeshPipelineFrustumPlane& plane : frustum.planes) {
        const float distance = plane.x * origin[0] + plane.y * origin[1] + plane.z * origin[2] + plane.w;
        if (distance < -reach - kBoundsRoundingMargin * (terms + std::abs(plane.w))) {
            return true;
        }
    }
    return false;
}

} // namespace kb::render
