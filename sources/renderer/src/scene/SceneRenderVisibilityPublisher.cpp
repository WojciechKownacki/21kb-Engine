#include "scene/SceneRenderVisibilityPublisher.hpp"

#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"
#include "scene/pipeline/MeshPipelineVisibility.hpp"

#include <bx/bounds.h>
#include <bx/math.h>

#include <algorithm>
#include <cstdint>

namespace kb::render {
namespace {

// The world-space box of a local box under the instance transform. bx already owns this:
// an Obb is just the matrix that maps the unit cube, and toAabb reduces it to the axis-aligned
// box - so there is no hand-rolled corner or abs-matrix math here to drift from the library
// the renderer already links.
// The centre is deliberately not returned: the box and the sphere share one local origin, so
// under the same model matrix they land on the same world point. Only the extent differs.
[[nodiscard]] kb::math::Vec3 TransformBoxHalfExtents(
    const RenderBoundsBox& localBox,
    const std::array<float, 16>& model) noexcept {
    const bx::Vec3 halfExtents{ localBox.halfExtents[0], localBox.halfExtents[1], localBox.halfExtents[2] };
    float boxToLocal[16];
    bx::mtxSRT(
        boxToLocal,
        halfExtents.x, halfExtents.y, halfExtents.z,
        0.0F, 0.0F, 0.0F,
        localBox.center[0], localBox.center[1], localBox.center[2]);

    bx::Obb obb{};
    bx::mtxMul(obb.mtx, boxToLocal, model.data());

    bx::Aabb aabb{};
    bx::toAabb(aabb, obb);
    return kb::math::Vec3{
        (aabb.max.x - aabb.min.x) * 0.5F,
        (aabb.max.y - aabb.min.y) * 0.5F,
        (aabb.max.z - aabb.min.z) * 0.5F,
    };
}

} // namespace

void SceneRenderVisibilityPublisher::BuildFrame(
    const RenderScene& renderScene,
    const SceneRenderCamera* camera,
    std::uint32_t viewportId,
    std::uint32_t localUserId,
    std::uint32_t viewportWidth,
    std::uint32_t viewportHeight,
    const RenderResourceRegistry* resources,
    const SceneRenderResourceMap* resourceMap,
    kb::scene::SceneRenderVisibilityFrame& outFrame) {
    const MeshPipelineFrustum frustum = MeshPipelineVisibility::BuildFrustum(camera);
    outFrame.frustumValid = frustum.valid;
    outFrame.viewportId = viewportId;
    outFrame.localUser = kb::input::LocalUserId{localUserId};
    outFrame.viewportWidth = viewportWidth;
    outFrame.viewportHeight = viewportHeight;
    outFrame.cameraValid = camera != nullptr;
    outFrame.view = camera != nullptr ? camera->view : std::array<float, 16>{};
    outFrame.projection = camera != nullptr ? camera->projection : std::array<float, 16>{};
    for (std::size_t planeIndex = 0U; planeIndex < outFrame.frustumPlanes.size(); ++planeIndex) {
        const MeshPipelineFrustumPlane& plane = frustum.planes[planeIndex];
        outFrame.frustumPlanes[planeIndex] = kb::scene::SceneRenderFrustumPlane{ plane.x, plane.y, plane.z, plane.w };
    }

    // Same default MeshPassProcessor::BuildCommandsInto uses when a pass runs without a
    // camera: an all-bits cullingMask, so no instance is ever mask-rejected.
    const std::uint32_t cullingMask = camera != nullptr ? camera->cullingMask : 0xFFFFFFFFU;

    outFrame.entries.clear();
    outFrame.entries.reserve(renderScene.MeshProxyCount());
    for (const auto& [entityId, proxy] : renderScene.MeshProxies()) {
        RenderBoundsSphere localBounds{};
        RenderBoundsBox localBox{};
        if (resources != nullptr && resourceMap != nullptr) {
            const RenderMeshHandle meshHandle = resourceMap->ResolveMesh(proxy.desc.meshAssetId);
            const RenderMeshResource* meshResource = meshHandle.IsValid() ? resources->FindMesh(meshHandle) : nullptr;
            if (meshResource != nullptr) {
                localBounds = meshResource->bounds;
                localBox = meshResource->boundsBox;
            }
        }
        const RenderBoundsSphere worldBounds = MeshPipelineVisibility::TransformBounds(
            proxy.desc.boundsOverride.IsValid() ? proxy.desc.boundsOverride : localBounds,
            proxy.desc.model);

        const bool passesMask = (proxy.desc.layer & cullingMask) != 0U;
        const bool insideFrustum = MeshPipelineVisibility::IsInsideFrustum(frustum, worldBounds);
        // The box is an addition, not a replacement: a mesh whose box the renderer could not
        // resolve keeps a valid sphere and zero half-extents, and consumers fall back to it.
        kb::math::Vec3 worldBoxHalfExtents{};
        if (localBox.IsValid()) {
            worldBoxHalfExtents = TransformBoxHalfExtents(localBox, proxy.desc.model);
        }
        outFrame.entries.push_back(kb::scene::SceneRenderVisibilityEntry{
            .entityId = entityId,
            .worldBounds = kb::scene::SceneRenderBounds{
                .center = kb::math::Vec3{ worldBounds.center[0], worldBounds.center[1], worldBounds.center[2] },
                .radius = worldBounds.radius,
                .halfExtents = worldBoxHalfExtents,
            },
            .visible = proxy.desc.visible && passesMask && insideFrustum,
        });
    }

    std::sort(
        outFrame.entries.begin(),
        outFrame.entries.end(),
        [](const kb::scene::SceneRenderVisibilityEntry& lhs, const kb::scene::SceneRenderVisibilityEntry& rhs) noexcept {
            return lhs.entityId < rhs.entityId;
        });
}

} // namespace kb::render
