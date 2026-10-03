#include "ShadowCasterBoundsCollector.hpp"

#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"
#include "scene/pipeline/MeshPipelineVisibility.hpp"

#include <algorithm>
#include <cmath>

namespace kb::render {
namespace {

[[nodiscard]] RenderBoundsSphere TransformBoundsForShadow(const RenderBoundsSphere& localBounds, const std::array<float, 16>& model) noexcept {
    if (!localBounds.IsValid()) {
        return RenderBoundsSphere{
            .center = { model[12], model[13], model[14] },
            .radius = 1.0F,
        };
    }

    return MeshPipelineVisibility::TransformBounds(localBounds, model);
}

[[nodiscard]] RenderBoundsSphere MergeBounds(RenderBoundsSphere lhs, const RenderBoundsSphere& rhs) noexcept {
    if (!lhs.IsValid()) {
        return rhs;
    }
    if (!rhs.IsValid()) {
        return lhs;
    }

    const float dx = rhs.center[0] - lhs.center[0];
    const float dy = rhs.center[1] - lhs.center[1];
    const float dz = rhs.center[2] - lhs.center[2];
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (distance + rhs.radius <= lhs.radius) {
        return lhs;
    }
    if (distance + lhs.radius <= rhs.radius) {
        return rhs;
    }

    const float mergedRadius = (distance + lhs.radius + rhs.radius) * 0.5F;
    const float centerShift = distance > 0.0001F ? (mergedRadius - lhs.radius) / distance : 0.0F;
    return RenderBoundsSphere{
        .center = {
            lhs.center[0] + dx * centerShift,
            lhs.center[1] + dy * centerShift,
            lhs.center[2] + dz * centerShift,
        },
        .radius = mergedRadius,
    };
}

} // namespace

ShadowCasterBounds ShadowCasterBoundsCollector::Collect(
    const RenderScene& renderScene,
    const RenderResourceRegistry& resources,
    const SceneRenderResourceMap& resourceMap,
    std::uint32_t cameraCullingMask,
    const std::array<float, 16>* focusView,
    float focusHalfExtent) noexcept {
    ShadowCasterBounds result{};
    const auto accumulate = [&](const RenderBoundsSphere& worldBounds, std::uint32_t casters) {
        result.bounds = MergeBounds(result.bounds, worldBounds);
        result.casterCount += casters;
        if (focusView != nullptr) {
            const auto& view = *focusView;
            const auto& center = worldBounds.center;
            const float x = view[0] * center[0] + view[4] * center[1] + view[8] * center[2] + view[12];
            const float y = view[1] * center[0] + view[5] * center[1] + view[9] * center[2] + view[13];
            if (std::abs(x) <= focusHalfExtent + worldBounds.radius &&
                std::abs(y) <= focusHalfExtent + worldBounds.radius) {
                const float z = view[2] * center[0] + view[6] * center[1] + view[10] * center[2] + view[14];
                result.focusedDepthMinimum = std::min(result.focusedDepthMinimum, z - worldBounds.radius);
                result.focusedDepthMaximum = std::max(result.focusedDepthMaximum, z + worldBounds.radius);
                result.focusedCasterCount += casters;
            }
        }
    };
    // The existing draw groups own visible instances, including generated
    // vegetation and strokes. Resolve a mesh once per group, not per entity.
    for (const auto& group : renderScene.DrawGroups()) {
        const auto* resource = resources.FindMesh(resourceMap.ResolveMesh(group.meshAssetId));
        const auto meshBounds = resource == nullptr ? RenderBoundsSphere{} : resource->bounds;
        std::size_t clusterIndex = 0U;
        for (std::size_t index = 0U; index < group.instances.size(); ++index) {
            const auto& instance = group.instances[index];
            while (clusterIndex < group.visibilityClusters.size() &&
                group.visibilityClusters[clusterIndex].firstInstance < index) ++clusterIndex;
            if ((!instance.castsShadow && !instance.receivesShadow) || (instance.layer & cameraCullingMask) == 0U) continue;
            const auto localBounds = instance.boundsOverride.IsValid() ? instance.boundsOverride : meshBounds;
            if (clusterIndex < group.visibilityClusters.size() &&
                group.visibilityClusters[clusterIndex].firstInstance == index && localBounds.IsValid()) {
                const auto& cluster = group.visibilityClusters[clusterIndex];
                if (cluster.instanceCount != 0U && cluster.instanceCount <= group.instances.size()-index) {
                    auto bounds = cluster.origins;
                    const auto& center = localBounds.center;
                    bounds.radius += cluster.maximumScale * (localBounds.radius +
                        std::sqrt(center[0]*center[0] + center[1]*center[1] + center[2]*center[2]));
                    accumulate(bounds, instance.castsShadow ? cluster.instanceCount : 0U);
                    index += cluster.instanceCount-1U;
                    continue;
                }
            }
            accumulate(TransformBoundsForShadow(localBounds, instance.model), instance.castsShadow ? 1U : 0U);
        }
    }
    return result;
}

} // namespace kb::render
