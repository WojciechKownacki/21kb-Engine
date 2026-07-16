#pragma once

#include "engine/math/EngineMath.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace kb::scene {

class Scene;

// LIB-144: one entity's renderer-computed world-space bounding sphere. The renderer's ONLY
// bounds representation is a sphere (kb::render::RenderBoundsSphere - center + radius,
// computed from mesh vertices at asset-build time and transformed by the entity's model
// matrix every frame); kb::scene deliberately mirrors that single source of truth as a plain
// value type instead of inventing a second AABB representation the renderer never computes.
struct SceneRenderBounds {
    kb::math::Vec3 center{};
    float radius = 0.0F;

    [[nodiscard]] constexpr bool IsValid() const noexcept { return radius > 0.0F; }
};

// LIB-144: one camera frustum plane in ax + by + cz + w >= 0 half-space form (a point is
// inside the frustum when that expression is >= 0 for all six planes; a sphere when it is
// >= -radius). Mirrors kb::render's MeshPipelineFrustumPlane exactly - the same
// "two parallel plain types across the kb::scene/kb::render boundary" convention
// CameraClearMode/RenderCameraClearMode (LIB-136) and MaterialParameterType (LIB-140)
// already establish, because kb::scene never includes kb::render headers.
struct SceneRenderFrustumPlane {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 0.0F;
};

// LIB-144: one mesh-rendering entity's visibility result from the camera the renderer
// actually used for the scene's most recent submit. `visible` reproduces exactly the checks
// the mesh pipeline applies when deciding whether the entity's instances can reach a base
// pass: the authored VisibilityComponent flag, the camera cullingMask vs MeshRenderer layer
// test, and the CPU sphere-vs-frustum cull (kb::render::MeshPipelineVisibility) - never a
// GPU occlusion query or readback.
struct SceneRenderVisibilityEntry {
    std::uint64_t entityId = 0;
    SceneRenderBounds worldBounds{};
    bool visible = false;
};

// LIB-144: the per-scene visibility feedback frame the renderer publishes at every
// SubmitScene. Entries are sorted by entityId (deterministic regardless of the renderer's
// internal proxy-map iteration order, and binary-searchable). `frustumValid` is false when
// the submit had no resolvable camera - IsInsideFrustum-style culling never ran, matching
// the mesh pipeline's own "no camera = nothing is culled" behavior.
struct SceneRenderVisibilityFrame {
    bool frustumValid = false;
    std::uint32_t viewportId = 0;
    std::array<SceneRenderFrustumPlane, 6> frustumPlanes{};
    std::vector<SceneRenderVisibilityEntry> entries;
};

// LIB-144: Renderer.IsVisible/GetBounds/TestFrustum's engine-side owner - the scene-held,
// renderer-published visibility feedback table, mirroring ScenePostProcessAccess's
// static-access shape (plain fields on SceneState, no per-entity component).
//
// Data flow and latency contract: kb::render::Renderer computes this frame on the CPU
// during SubmitScene (reusing the exact frustum/bounds math its mesh pipeline culls with)
// and publishes it here; scripts running the NEXT frame read the result. That one-frame
// latency is inherent to any "was it rendered" query that avoids forcing GPU/pipeline
// synchronization, and matches the industry-standard semantics of Unity's
// Renderer.isVisible. When the same scene is submitted to several viewports in one frame
// (e.g. docked + detached editor panels), the last submit in the frame's deterministic
// submission-plan order wins; in the editor every scene-panel submit uses the editor's own
// viewport camera (cameraOverride), while the standalone player/game runtime submits with
// the scene's primary CameraComponent.
//
// Before the first publish (or after Clear), HasFrame is false and every query returns its
// honest empty result: IsVisible false, WorldBounds invalid, TestFrustum false (fail-closed
// - "not known to be inside" rather than pretending visibility).
class SceneRenderFeedback {
public:
    SceneRenderFeedback() = delete;

    // Swaps `frame`'s entries into the scene (the caller's vector keeps the previous
    // frame's capacity for reuse - no per-frame steady-state allocation) and bumps
    // PublishCount. `frame.entries` must already be sorted by entityId ascending.
    static void Publish(Scene& scene, SceneRenderVisibilityFrame& frame) noexcept;
    static void Clear(Scene& scene) noexcept;

    [[nodiscard]] static bool HasFrame(const Scene& scene) noexcept;
    [[nodiscard]] static std::uint64_t PublishCount(const Scene& scene) noexcept;
    // False for an entity with no entry in the last published frame (no MeshRenderer proxy,
    // destroyed after the submit, or no frame published yet) - never an error.
    [[nodiscard]] static bool IsVisible(const Scene& scene, SceneEntity entity) noexcept;
    // Invalid (radius 0) bounds for an untracked entity, an entity whose mesh resource had
    // no valid bounds at submit time, or when no frame was published yet.
    [[nodiscard]] static SceneRenderBounds WorldBounds(const Scene& scene, SceneEntity entity) noexcept;
    // Sphere-vs-frustum test against the last published camera frustum (radius 0 = point
    // test). False when no frame was published yet or the last submit had no camera.
    [[nodiscard]] static bool TestFrustum(const Scene& scene, const kb::math::Vec3& center, float radius) noexcept;

private:
    [[nodiscard]] static const SceneRenderVisibilityEntry* FindEntry(const Scene& scene, SceneEntity entity) noexcept;
};

} // namespace kb::scene
