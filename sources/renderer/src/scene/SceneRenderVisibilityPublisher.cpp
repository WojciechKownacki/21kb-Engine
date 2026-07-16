#include "scene/SceneRenderVisibilityPublisher.hpp"

#include "kb/render/resources/RenderResourceRegistry.hpp"
#include "kb/render/scene/SceneParticleRenderSynchronizer.hpp"
#include "kb/render/scene/SceneRenderResourceMap.hpp"
#include "scene/pipeline/MeshPipelineVisibility.hpp"

#include <algorithm>
#include <cstdint>

namespace kb::render {

void SceneRenderVisibilityPublisher::BuildFrame(
    const RenderScene& renderScene,
    const SceneRenderCamera* camera,
    std::uint32_t viewportId,
    std::uint32_t viewportWidth,
    std::uint32_t viewportHeight,
    const RenderResourceRegistry* resources,
    const SceneRenderResourceMap* resourceMap,
    kb::scene::SceneRenderVisibilityFrame& outFrame) {
    const MeshPipelineFrustum frustum = MeshPipelineVisibility::BuildFrustum(camera);
    outFrame.frustumValid = frustum.valid;
    outFrame.viewportId = viewportId;
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
        if (entityId >= SceneParticleRenderSynchronizer::kSyntheticProxyIdBase) {
            continue;
        }

        RenderBoundsSphere localBounds{};
        if (resources != nullptr && resourceMap != nullptr) {
            const RenderMeshHandle meshHandle = resourceMap->ResolveMesh(proxy.desc.meshAssetId);
            const RenderMeshResource* meshResource = meshHandle.IsValid() ? resources->FindMesh(meshHandle) : nullptr;
            if (meshResource != nullptr) {
                localBounds = meshResource->bounds;
            }
        }
        const RenderBoundsSphere worldBounds = MeshPipelineVisibility::TransformBounds(localBounds, proxy.desc.model);

        const bool passesMask = (proxy.desc.layer & cullingMask) != 0U;
        const bool insideFrustum = MeshPipelineVisibility::IsInsideFrustum(frustum, worldBounds);
        outFrame.entries.push_back(kb::scene::SceneRenderVisibilityEntry{
            .entityId = entityId,
            .worldBounds = kb::scene::SceneRenderBounds{
                .center = kb::math::Vec3{ worldBounds.center[0], worldBounds.center[1], worldBounds.center[2] },
                .radius = worldBounds.radius,
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
