#include "ShadowCasterBoundsCollector.hpp"

#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"

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

    const float sx = std::sqrt(model[0] * model[0] + model[1] * model[1] + model[2] * model[2]);
    const float sy = std::sqrt(model[4] * model[4] + model[5] * model[5] + model[6] * model[6]);
    const float sz = std::sqrt(model[8] * model[8] + model[9] * model[9] + model[10] * model[10]);
    return RenderBoundsSphere{
        .center = {
            model[0] * localBounds.center[0] + model[4] * localBounds.center[1] + model[8] * localBounds.center[2] + model[12],
            model[1] * localBounds.center[0] + model[5] * localBounds.center[1] + model[9] * localBounds.center[2] + model[13],
            model[2] * localBounds.center[0] + model[6] * localBounds.center[1] + model[10] * localBounds.center[2] + model[14],
        },
        .radius = localBounds.radius * std::max(std::max(sx, sy), sz),
    };
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
    const SceneRenderResourceMap& resourceMap) noexcept {
    ShadowCasterBounds result{};
    for (const auto& [entityId, proxy] : renderScene.MeshProxies()) {
        static_cast<void>(entityId);
        const MeshRenderProxyDesc& mesh = proxy.desc;
        if (!mesh.visible || !mesh.castsShadow) {
            continue;
        }

        RenderBoundsSphere localBounds{};
        const RenderMeshHandle meshHandle = resourceMap.ResolveMesh(mesh.meshAssetId);
        if (const RenderMeshResource* meshResource = resources.FindMesh(meshHandle); meshResource != nullptr) {
            localBounds = meshResource->bounds;
        }
        result.bounds = MergeBounds(result.bounds, TransformBoundsForShadow(localBounds, mesh.model));
        ++result.casterCount;
    }
    return result;
}

} // namespace kb::render
