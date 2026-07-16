#pragma once

#include "engine/scene/SceneRenderFeedback.hpp"
#include "kb/render/scene/RenderScene.hpp"

#include <cstdint>

namespace kb::render {

class RenderResourceRegistry;
class SceneRenderResourceMap;
struct SceneRenderCamera;

// LIB-144: builds the per-scene, per-submit CPU visibility feedback frame
// (kb::scene::SceneRenderVisibilityFrame) the Renderer publishes into the scene at every
// SubmitSceneToViewport - the backing data for the script-facing
// Renderer.IsVisible/GetBounds/TestFrustum API. Reuses the mesh pipeline's own culling math
// verbatim (MeshPipelineVisibility::BuildFrustum/TransformBounds/IsInsideFrustum plus the
// MeshPipelinePassPolicy layer-vs-cullingMask bit test), so a published `visible` result is
// exactly "this entity's bounds survived the same CPU checks the base passes cull with" -
// no GPU occlusion query, readback, fence, or any other GPU synchronization anywhere.
//
// Static and stateless, mirroring MeshPipelineVisibility's own shape. Separated from the
// actual SceneRenderFeedback::Publish call so the frame construction is unit-testable
// against a hand-built RenderScene without a Scene or an initialized bgfx context.
class SceneRenderVisibilityPublisher {
public:
    SceneRenderVisibilityPublisher() = delete;

    // Fills `outFrame` from the render scene's current mesh proxies (entries sorted by
    // entityId; reuses the vector's existing capacity - no steady-state allocation).
    // Synthetic particle proxies (SceneParticleRenderSynchronizer::kSyntheticProxyIdBase
    // namespace) are skipped: scripts can never hold a synthetic id, so entries for them
    // would be dead weight. `camera` is the camera this submit actually renders with
    // (pre-temporal-jitter; nullptr when the submit resolved none - the frame is still
    // published with frustumValid=false, matching the mesh pipeline's own "no camera =
    // nothing is culled" behavior). `resources`/`resourceMap` resolve each proxy's
    // meshAssetId to its mesh resource for local bounds; a proxy whose mesh is not (yet)
    // resolvable keeps invalid bounds and is treated as never-culled, exactly like
    // MeshPipelineVisibility::IsInsideFrustum's own degenerate-bounds rule.
    static void BuildFrame(
        const RenderScene& renderScene,
        const SceneRenderCamera* camera,
        std::uint32_t viewportId,
        const RenderResourceRegistry* resources,
        const SceneRenderResourceMap* resourceMap,
        kb::scene::SceneRenderVisibilityFrame& outFrame);
};

} // namespace kb::render
