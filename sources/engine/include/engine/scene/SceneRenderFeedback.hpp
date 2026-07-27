#pragma once

#include "engine/math/EngineMath.hpp"
#include "engine/input/InputLocalUser.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <array>
#include <cstdint>
#include <string_view>
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
//
// LIB-145: the frame also carries the submit's pre-jitter camera matrices and viewport
// pixel extent (`cameraValid` mirrors frustumValid - both are false for a camera-less
// submit), the inputs for the CPU screen/world conversions below. Raw column-major
// float[16] arrays, not kb::math::Mat4 - they arrive from the renderer's own
// SceneRenderCamera representation and are consumed by array-indexing math, so converting
// through Mat4 would add copies without adding meaning.
struct SceneRenderVisibilityFrame {
    bool frustumValid = false;
    bool cameraValid = false;
    std::uint32_t viewportId = 0;
    kb::input::LocalUserId localUser = kb::input::kPrimaryLocalUser;
    std::uint32_t viewportWidth = 0;
    std::uint32_t viewportHeight = 0;
    std::array<SceneRenderFrustumPlane, 6> frustumPlanes{};
    std::array<float, 16> view{};
    std::array<float, 16> projection{};
    std::vector<SceneRenderVisibilityEntry> entries;
};

// LIB-145: WorldToScreen's result. `valid` is false when no frame/camera was published;
// `onScreen` additionally requires the point to be in FRONT of the camera and inside the
// viewport rectangle. screenX/screenY are viewport-local pixels (top-left origin, +Y down -
// the same convention the editor's own EditorSceneViewportMath::WorldToScreen uses);
// `viewDepth` is the distance along the camera's forward axis (negative = behind), useful
// for depth-sorting UI markers without a second query.
struct SceneRenderScreenPoint {
    float screenX = 0.0F;
    float screenY = 0.0F;
    float viewDepth = 0.0F;
    bool onScreen = false;
    bool valid = false;
};

// LIB-145: ScreenPointToRay's result. `ray.direction` is normalized; `valid` is false when
// no frame/camera was published. The ray feeds kb::scene's Physics.Raycast (same
// origin+direction convention) with no conversion.
struct SceneRenderCameraRay {
    kb::math::Ray ray{};
    bool valid = false;
};

// LIB-145: one async screen capture's observable lifecycle. Unknown = the id never named a
// request in this scene (or is 0); Pending = requested, not yet finished (covers both
// "renderer has not seen it yet" and "GPU readback in flight"); Completed/Failed = the
// terminal result of the most recently finished capture. Only the latest terminal result
// is retained - an id older than the last finished capture reports Unknown again, which is
// honest ("no longer known"), bounded (no per-id history map), and sufficient for the
// request-then-poll usage the API is built for.
enum class SceneScreenCaptureStatus : std::uint8_t {
    Unknown,
    Pending,
    Completed,
    Failed,
};

// LIB-145: a pending screen-capture request as the renderer sees it. `id` is 0 when there
// is nothing to take.
struct SceneScreenCaptureRequest {
    std::uint64_t id = 0;
    std::string_view path{};
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
// Renderer.isVisible. Feedback is retained independently per local user so split-screen
// views cannot overwrite each other's active camera. Legacy overloads without a local-user
// argument continue to address the most recently submitted view.
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
    static void Publish(Scene& scene, SceneRenderVisibilityFrame& frame);
    static void Clear(Scene& scene) noexcept;

    [[nodiscard]] static bool HasFrame(const Scene& scene) noexcept;
    [[nodiscard]] static bool HasFrame(const Scene& scene, kb::input::LocalUserId localUser) noexcept;
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

    // LIB-145: projects a world point through the last published camera's view*projection
    // (works for perspective and orthographic alike - no matrix decomposition involved).
    // `valid=false` (all-default result) when no frame/camera was published.
    [[nodiscard]] static SceneRenderScreenPoint WorldToScreen(const Scene& scene, const kb::math::Vec3& worldPoint) noexcept;
    // LIB-145: builds the world-space ray through a viewport-local pixel (top-left origin),
    // mirroring the editor's own EditorSceneViewportHitResolver ray math: camera basis axes
    // extracted from the view matrix plus tan(fov/2)/aspect extracted from the projection
    // matrix (perspective), or a forward-directed, laterally-offset ray (orthographic,
    // detected via projection[15]). Never inverts a matrix - kb::math deliberately has no
    // Mat4 inverse and none is needed.
    [[nodiscard]] static SceneRenderCameraRay ScreenPointToRay(const Scene& scene, float screenX, float screenY) noexcept;
    [[nodiscard]] static SceneRenderCameraRay ScreenPointToRay(
        const Scene& scene,
        kb::input::LocalUserId localUser,
        float screenX,
        float screenY) noexcept;
    // LIB-145: convenience composition - the point `distance` units along
    // ScreenPointToRay's ray. False (outWorldPoint untouched) when no camera was published.
    [[nodiscard]] static bool ScreenToWorld(const Scene& scene, float screenX, float screenY, float distance, kb::math::Vec3& outWorldPoint) noexcept;

    // LIB-145: requests an async screen capture of this scene's next rendered frame,
    // written as a PNG to `path`. Returns the capture id, or 0 when `path` is empty or
    // another capture is still pending (single in-flight per scene). The capture itself is
    // performed by the renderer across subsequent frames through the same frame-gated,
    // never-stalling GPU readback pattern the auto-exposure meter uses - poll
    // ScreenCaptureStatus for the terminal result.
    [[nodiscard]] static std::uint64_t RequestScreenCapture(Scene& scene, std::string_view path);
    [[nodiscard]] static SceneScreenCaptureStatus ScreenCaptureStatus(const Scene& scene, std::uint64_t id) noexcept;

    // Renderer-facing half of the capture channel (called during SubmitScene through the
    // same scene-mutable-during-its-own-submit convention Publish uses). Peek returns the
    // pending, not-yet-consumed request (id 0 otherwise); Consume marks it taken so a
    // second submit does not start it twice; Complete records the terminal result and
    // frees the single pending slot.
    [[nodiscard]] static SceneScreenCaptureRequest PeekScreenCaptureRequest(const Scene& scene) noexcept;
    static void ConsumeScreenCaptureRequest(Scene& scene, std::uint64_t id) noexcept;
    static void CompleteScreenCapture(Scene& scene, std::uint64_t id, bool succeeded) noexcept;

private:
    [[nodiscard]] static const SceneRenderVisibilityFrame* FindFrame(
        const Scene& scene,
        kb::input::LocalUserId localUser) noexcept;
    [[nodiscard]] static const SceneRenderVisibilityEntry* FindEntry(const Scene& scene, SceneEntity entity) noexcept;
};

} // namespace kb::scene
