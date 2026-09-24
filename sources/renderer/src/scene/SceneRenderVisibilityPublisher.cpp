#include "scene/SceneRenderVisibilityPublisher.hpp"

#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"
#include "scene/pipeline/MeshPipelineVisibility.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <span>

namespace kb::render {
namespace {

// An affine transform maps each local box axis to a column of its linear part.
// The axis-aligned world half-extent is the sum of their absolute contributions;
// translation and the local box centre do not change it.
[[nodiscard]] kb::math::Vec3 TransformBoxHalfExtents(
    const RenderBoundsBox& localBox,
    const std::array<float, 16>& model) noexcept {
    const float x = localBox.halfExtents[0];
    const float y = localBox.halfExtents[1];
    const float z = localBox.halfExtents[2];
    return kb::math::Vec3{
        std::abs(model[0]) * x + std::abs(model[4]) * y + std::abs(model[8]) * z,
        std::abs(model[1]) * x + std::abs(model[5]) * y + std::abs(model[9]) * z,
        std::abs(model[2]) * x + std::abs(model[6]) * y + std::abs(model[10]) * z,
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
    kb::scene::SceneRenderVisibilityFrame& outFrame,
    double* outSortMilliseconds) {
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
    const auto sortBegin = std::chrono::steady_clock::now();
    const std::span<const MeshRenderProxy* const> sortedProxies = renderScene.SortedMeshProxies();
    if (outSortMilliseconds != nullptr) {
        *outSortMilliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - sortBegin).count();
    }
    std::uint64_t cachedMeshAssetId = 0U;
    bool cachedMeshBounds = false;
    RenderBoundsSphere localBounds{};
    RenderBoundsBox localBox{};
    for (const MeshRenderProxy* proxy : sortedProxies) {
        if (!cachedMeshBounds || cachedMeshAssetId != proxy->desc.meshAssetId) {
            cachedMeshAssetId = proxy->desc.meshAssetId;
            cachedMeshBounds = true;
            localBounds = {};
            localBox = {};
            if (resources != nullptr && resourceMap != nullptr) {
                const RenderMeshHandle meshHandle = resourceMap->ResolveMesh(cachedMeshAssetId);
                const RenderMeshResource* meshResource = meshHandle.IsValid() ? resources->FindMesh(meshHandle) : nullptr;
                if (meshResource != nullptr) {
                    localBounds = meshResource->bounds;
                    localBox = meshResource->boundsBox;
                }
            }
        }
        const RenderBoundsSphere worldBounds = MeshPipelineVisibility::TransformBounds(
            proxy->desc.boundsOverride.IsValid() ? proxy->desc.boundsOverride : localBounds,
            proxy->desc.model);

        const bool passesMask = (proxy->desc.layer & cullingMask) != 0U;
        const bool insideFrustum = MeshPipelineVisibility::IsInsideFrustum(frustum, worldBounds);
        // The box is an addition, not a replacement: a mesh whose box the renderer could not
        // resolve keeps a valid sphere and zero half-extents, and consumers fall back to it.
        kb::math::Vec3 worldBoxHalfExtents{};
        if (localBox.IsValid()) {
            worldBoxHalfExtents = TransformBoxHalfExtents(localBox, proxy->desc.model);
        }
        outFrame.entries.push_back(kb::scene::SceneRenderVisibilityEntry{
            .entityId = proxy->desc.entityId,
            .worldBounds = kb::scene::SceneRenderBounds{
                .center = kb::math::Vec3{ worldBounds.center[0], worldBounds.center[1], worldBounds.center[2] },
                .radius = worldBounds.radius,
                .halfExtents = worldBoxHalfExtents,
            },
            .visible = proxy->desc.visible && passesMask && insideFrustum,
        });
    }

}

} // namespace kb::render
