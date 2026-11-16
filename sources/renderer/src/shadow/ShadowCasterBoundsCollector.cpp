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
    float focusHalfExtent,
    const std::array<float, 3>* viewer) noexcept {
    ShadowCasterBounds result{};
    const auto accumulate = [&](const RenderBoundsSphere& worldBounds, std::uint32_t casters, bool displaced) {
        result.bounds = MergeBounds(result.bounds, worldBounds);
        result.casterCount += casters;
        if (focusView == nullptr && displaced) {
            result.displacedCasterCount += casters;
        }
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
                if (displaced) {
                    result.displacedCasterCount += casters;
                }
            }
        }
    };
    // The existing draw groups own visible instances, including generated
    // vegetation and strokes. Resolve a mesh once per group, not per entity.
    for (const auto& group : renderScene.DrawGroups()) {
        const auto* resource = resources.FindMesh(resourceMap.ResolveMesh(group.meshAssetId));
        const auto meshBounds = resource == nullptr ? RenderBoundsSphere{} : resource->bounds;
        // The group material's World Position Offset reach keeps moved casters inside the cascades.
        const float groupMargin = MeshPipelineVisibility::MaterialBoundsMargin(
            resources.FindMaterial(resourceMap.ResolveMaterial(group.materialAssetId)));
        std::size_t clusterIndex = 0U;
        const std::span<const SceneRenderMeshInstance> instances = group.Instances();
        const float reach = viewer != nullptr && focusView != nullptr && group.cullDistance > 0.0F
            ? std::min(group.cullDistance, focusHalfExtent) : 0.0F;
        const auto beyondReach = [&](const std::array<float, 3>& center, float radius) noexcept {
            const float x = center[0] - (*viewer)[0];
            const float y = center[1] - (*viewer)[1];
            const float z = center[2] - (*viewer)[2];
            return std::sqrt(x * x + y * y + z * z) - radius >= reach;
        };
        for (std::size_t index = 0U; index < instances.size(); ++index) {
            const auto& instance = instances[index];
            while (clusterIndex < group.visibilityClusters.size() &&
                group.visibilityClusters[clusterIndex].firstInstance < index) ++clusterIndex;
            if ((!instance.castsShadow && !instance.receivesShadow) || (instance.layer & cameraCullingMask) == 0U) continue;
            const auto localBounds = instance.boundsOverride.IsValid() ? instance.boundsOverride : meshBounds;
            const float boundsMargin = instance.materialSlotOverrideCount == 0U
                ? groupMargin
                : std::max(groupMargin, MeshPipelineVisibility::MaterialBoundsMargin(resources, resourceMap, instance.materialAssetId,
                      std::span<const std::uint64_t>{ instance.materialSlotAssetIds.data(), instance.materialSlotOverrideCount }));
            if (clusterIndex < group.visibilityClusters.size() &&
                group.visibilityClusters[clusterIndex].firstInstance == index && localBounds.IsValid()) {
                const auto& cluster = group.visibilityClusters[clusterIndex];
                if (cluster.instanceCount != 0U && cluster.instanceCount <= instances.size()-index) {
                    if (reach > 0.0F && beyondReach(cluster.origins.center, cluster.origins.radius)) {
                        index += cluster.instanceCount-1U;
                        continue;
                    }
                    auto bounds = cluster.origins;
                    const auto& center = localBounds.center;
                    bounds.radius += cluster.maximumScale * (localBounds.radius +
                        std::sqrt(center[0]*center[0] + center[1]*center[1] + center[2]*center[2])) + boundsMargin;
                    accumulate(bounds, instance.castsShadow ? cluster.instanceCount : 0U, boundsMargin > 0.0F);
                    index += cluster.instanceCount-1U;
                    continue;
                }
            }
            if (reach > 0.0F && beyondReach({ instance.model[12], instance.model[13], instance.model[14] }, 0.0F)) continue;
            accumulate(MeshPipelineVisibility::GrowBounds(TransformBoundsForShadow(localBounds, instance.model), boundsMargin),
                instance.castsShadow ? 1U : 0U, boundsMargin > 0.0F);
        }
    }
    return result;
}

} // namespace kb::render
