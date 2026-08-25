#include "engine/scene/SceneRenderFeedback.hpp"

#include "engine/scene/Scene.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace kb::scene {
namespace {

// Camera basis axes in world space = the rows of the (rigid) view matrix's upper 3x3,
// column-major indexing - the exact rows kb::render's MeshPipelineVisibility::ViewDepth
// already dots against (row 2 = forward, positive in front of the camera).
[[nodiscard]] kb::math::Vec3 ViewRight(const std::array<float, 16>& view) noexcept {
    return kb::math::Vec3{ view[0], view[4], view[8] };
}

[[nodiscard]] kb::math::Vec3 ViewUp(const std::array<float, 16>& view) noexcept {
    return kb::math::Vec3{ view[1], view[5], view[9] };
}

[[nodiscard]] kb::math::Vec3 ViewForward(const std::array<float, 16>& view) noexcept {
    return kb::math::Vec3{ view[2], view[6], view[10] };
}

// For a rigid view matrix V = [R | t] (world -> view), the camera's world position P
// satisfies R*P + t = 0, so P = -transpose(R)*t = -(right*t.x + up*t.y + forward*t.z).
[[nodiscard]] kb::math::Vec3 CameraPosition(const std::array<float, 16>& view) noexcept {
    const kb::math::Vec3 right = ViewRight(view);
    const kb::math::Vec3 up = ViewUp(view);
    const kb::math::Vec3 forward = ViewForward(view);
    const float tx = view[12];
    const float ty = view[13];
    const float tz = view[14];
    return kb::math::Vec3{
        -(right.x * tx + up.x * ty + forward.x * tz),
        -(right.y * tx + up.y * ty + forward.y * tz),
        -(right.z * tx + up.z * ty + forward.z * tz),
    };
}

// clip = M * (p, 1) for a column-major float[16].
[[nodiscard]] kb::math::Vec4 TransformPoint(const std::array<float, 16>& m, const kb::math::Vec3& p) noexcept {
    return kb::math::Vec4{
        m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12],
        m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
        m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14],
        m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15],
    };
}

// An orthographic projection has w independent of z (projection[15] == 1); a perspective
// projection derives w from z (projection[15] == 0, projection[11] = +/-1). This is the
// only distinction the ray construction needs - x/y scale factors live in [0]/[5] for
// both (perspective: [5] = 1/tan(fovY/2), [0] = [5]/aspect; orthographic: [0] = 2/width,
// [5] = 2/height), regardless of the depth-range convention in [10]/[14].
[[nodiscard]] bool IsOrthographicProjection(const std::array<float, 16>& projection) noexcept {
    return projection[15] != 0.0F;
}

} // namespace

void SceneRenderFeedback::Publish(Scene& scene, SceneRenderVisibilityFrame& frame) {
    SceneState& state = SceneAccess::State(scene);
    SceneRenderVisibilityFrame& destination =
        state.renderVisibilityFrames[frame.localUser.value];
    std::swap(destination.entries, frame.entries);
    destination.frustumValid = frame.frustumValid;
    destination.cameraValid = frame.cameraValid;
    destination.viewportId = frame.viewportId;
    destination.localUser = frame.localUser;
    destination.viewportWidth = frame.viewportWidth;
    destination.viewportHeight = frame.viewportHeight;
    destination.frustumPlanes = frame.frustumPlanes;
    destination.view = frame.view;
    destination.projection = frame.projection;
    state.lastRenderVisibilityLocalUserId = frame.localUser.value;
    ++state.renderVisibilityPublishCount;
}

void SceneRenderFeedback::Clear(Scene& scene) noexcept {
    SceneState& state = SceneAccess::State(scene);
    state.renderVisibilityFrames.clear();
    state.lastRenderVisibilityLocalUserId = 0U;
    state.renderVisibilityPublishCount = 0U;
}

bool SceneRenderFeedback::HasFrame(const Scene& scene) noexcept {
    return SceneAccess::State(scene).renderVisibilityPublishCount != 0U;
}

bool SceneRenderFeedback::HasFrame(
    const Scene& scene, kb::input::LocalUserId localUser) noexcept {
    return FindFrame(scene, localUser) != nullptr;
}

std::uint64_t SceneRenderFeedback::PublishCount(const Scene& scene) noexcept {
    return SceneAccess::State(scene).renderVisibilityPublishCount;
}

bool SceneRenderFeedback::IsVisible(const Scene& scene, SceneEntity entity) noexcept {
    const SceneRenderVisibilityEntry* entry = FindEntry(scene, entity);
    return entry != nullptr && entry->visible;
}

SceneRenderBounds SceneRenderFeedback::WorldBounds(const Scene& scene, SceneEntity entity) noexcept {
    const SceneRenderVisibilityEntry* entry = FindEntry(scene, entity);
    return entry == nullptr ? SceneRenderBounds{} : entry->worldBounds;
}

bool SceneRenderFeedback::TestFrustum(const Scene& scene, const kb::math::Vec3& center, float radius) noexcept {
    const SceneState& state = SceneAccess::State(scene);
    const auto frameIt =
        state.renderVisibilityFrames.find(state.lastRenderVisibilityLocalUserId);
    if (state.renderVisibilityPublishCount == 0U ||
        frameIt == state.renderVisibilityFrames.end() ||
        !frameIt->second.frustumValid) {
        return false;
    }
    for (const SceneRenderFrustumPlane& plane : frameIt->second.frustumPlanes) {
        const float distance = plane.x * center.x + plane.y * center.y + plane.z * center.z + plane.w;
        if (distance < -radius) {
            return false;
        }
    }
    return true;
}

SceneRenderScreenPoint SceneRenderFeedback::WorldToScreen(const Scene& scene, const kb::math::Vec3& worldPoint) noexcept {
    const SceneState& state = SceneAccess::State(scene);
    const auto frameIt =
        state.renderVisibilityFrames.find(state.lastRenderVisibilityLocalUserId);
    if (state.renderVisibilityPublishCount == 0U ||
        frameIt == state.renderVisibilityFrames.end()) {
        return {};
    }
    const SceneRenderVisibilityFrame& frame = frameIt->second;
    if (!frame.cameraValid || frame.viewportWidth == 0U || frame.viewportHeight == 0U) {
        return {};
    }

    const kb::math::Vec4 viewPoint = TransformPoint(frame.view, worldPoint);
    const kb::math::Vec4 clip = TransformPoint(frame.projection, kb::math::Vec3{ viewPoint.x, viewPoint.y, viewPoint.z });
    SceneRenderScreenPoint result{};
    result.valid = true;
    result.viewDepth = viewPoint.z;
    // A non-positive clip w means the point is on or behind the camera plane (perspective) -
    // there is no meaningful pixel for it. The editor's own WorldToScreen returns false for
    // exactly this case; `valid` stays true because the camera itself was known.
    if (clip.w <= 0.000001F) {
        return result;
    }
    const float ndcX = clip.x / clip.w;
    const float ndcY = clip.y / clip.w;
    const float width = static_cast<float>(frame.viewportWidth);
    const float height = static_cast<float>(frame.viewportHeight);
    result.screenX = (ndcX * 0.5F + 0.5F) * width;
    result.screenY = (1.0F - (ndcY * 0.5F + 0.5F)) * height;
    result.onScreen = result.screenX >= 0.0F && result.screenX <= width && result.screenY >= 0.0F && result.screenY <= height;
    return result;
}

SceneRenderCameraRay SceneRenderFeedback::ScreenPointToRay(const Scene& scene, float screenX, float screenY) noexcept {
    const SceneState& state = SceneAccess::State(scene);
    return ScreenPointToRay(
        scene,
        kb::input::LocalUserId{state.lastRenderVisibilityLocalUserId},
        screenX,
        screenY);
}

SceneRenderCameraRay SceneRenderFeedback::ScreenPointToRay(
    const Scene& scene,
    kb::input::LocalUserId localUser,
    float screenX,
    float screenY) noexcept {
    const SceneRenderVisibilityFrame* framePtr = FindFrame(scene, localUser);
    if (framePtr == nullptr) {
        return {};
    }
    const SceneRenderVisibilityFrame& frame = *framePtr;
    if (!frame.cameraValid || frame.viewportWidth == 0U ||
        frame.viewportHeight == 0U) {
        return {};
    }
    const float xScale = frame.projection[0];
    const float yScale = frame.projection[5];
    if (xScale == 0.0F || yScale == 0.0F) {
        return {};
    }

    const float width = static_cast<float>(frame.viewportWidth);
    const float height = static_cast<float>(frame.viewportHeight);
    const float ndcX = ((screenX / width) * 2.0F) - 1.0F;
    const float ndcY = 1.0F - ((screenY / height) * 2.0F);
    const kb::math::Vec3 right = ViewRight(frame.view);
    const kb::math::Vec3 up = ViewUp(frame.view);
    const kb::math::Vec3 forward = ViewForward(frame.view);
    const kb::math::Vec3 position = CameraPosition(frame.view);

    SceneRenderCameraRay result{};
    result.valid = true;
    if (IsOrthographicProjection(frame.projection)) {
        // Orthographic: every pixel's ray points along forward; the pixel only offsets the
        // origin laterally by the half-extents encoded in the projection's x/y scales.
        const float halfWidth = 1.0F / xScale;
        const float halfHeight = 1.0F / yScale;
        result.ray.origin = position + (right * (ndcX * halfWidth)) + (up * (ndcY * halfHeight));
        result.ray.direction = kb::math::Normalize(forward);
        return result;
    }
    // Perspective: mirror the editor's EditorSceneViewportHitResolver::BuildRay math -
    // tan(fovY/2) = 1/projection[5], aspect-scaled x. ndcX*aspect*tanHalfFov collapses to
    // ndcX/projection[0].
    result.ray.origin = position;
    result.ray.direction = kb::math::Normalize(forward + (right * (ndcX / xScale)) + (up * (ndcY / yScale)));
    return result;
}

bool SceneRenderFeedback::ScreenToWorld(const Scene& scene, float screenX, float screenY, float distance, kb::math::Vec3& outWorldPoint) noexcept {
    const SceneRenderCameraRay cameraRay = ScreenPointToRay(scene, screenX, screenY);
    if (!cameraRay.valid) {
        return false;
    }
    outWorldPoint = cameraRay.ray.origin + (cameraRay.ray.direction * distance);
    return true;
}

std::uint64_t SceneRenderFeedback::RequestScreenCapture(Scene& scene, std::string_view path) {
    SceneState& state = SceneAccess::State(scene);
    if (path.empty() || state.pendingScreenCaptureId != 0U) {
        return 0U;
    }
    const std::uint64_t id = state.nextScreenCaptureId++;
    state.pendingScreenCaptureId = id;
    state.pendingScreenCapturePath.assign(path);
    state.pendingScreenCaptureReturnsPixels = false;
    state.pendingScreenCaptureConsumed = false;
    return id;
}

std::uint64_t SceneRenderFeedback::RequestScreenCapturePixels(Scene& scene) {
    SceneState& state = SceneAccess::State(scene);
    if (state.pendingScreenCaptureId != 0U) return 0U;
    const std::uint64_t id = state.nextScreenCaptureId++;
    state.pendingScreenCaptureId = id;
    state.pendingScreenCapturePath.clear();
    state.pendingScreenCaptureReturnsPixels = true;
    state.pendingScreenCaptureConsumed = false;
    return id;
}

SceneScreenCaptureStatus SceneRenderFeedback::ScreenCaptureStatus(const Scene& scene, std::uint64_t id) noexcept {
    const SceneState& state = SceneAccess::State(scene);
    if (id == 0U) {
        return SceneScreenCaptureStatus::Unknown;
    }
    if (id == state.pendingScreenCaptureId) {
        return SceneScreenCaptureStatus::Pending;
    }
    if (id == state.lastScreenCaptureId) {
        return state.lastScreenCaptureSucceeded ? SceneScreenCaptureStatus::Completed : SceneScreenCaptureStatus::Failed;
    }
    return SceneScreenCaptureStatus::Unknown;
}

std::optional<SceneScreenCapturePixels>
SceneRenderFeedback::TakeScreenCapturePixels(
    Scene& scene, std::uint64_t id) {
    SceneState& state = SceneAccess::State(scene);
    if (id == 0U || id != state.lastScreenCaptureId ||
        !state.lastScreenCaptureSucceeded ||
        !state.lastScreenCapturePixels.has_value()) {
        return std::nullopt;
    }
    std::optional<SceneScreenCapturePixels> pixels{
        std::move(*state.lastScreenCapturePixels)};
    state.lastScreenCapturePixels.reset();
    return pixels;
}

SceneScreenCaptureRequest SceneRenderFeedback::PeekScreenCaptureRequest(const Scene& scene) noexcept {
    const SceneState& state = SceneAccess::State(scene);
    if (state.pendingScreenCaptureId == 0U || state.pendingScreenCaptureConsumed) {
        return {};
    }
    return SceneScreenCaptureRequest{
        .id = state.pendingScreenCaptureId,
        .path = state.pendingScreenCapturePath,
        .returnPixels = state.pendingScreenCaptureReturnsPixels,
    };
}

void SceneRenderFeedback::ConsumeScreenCaptureRequest(Scene& scene, std::uint64_t id) noexcept {
    SceneState& state = SceneAccess::State(scene);
    if (id != 0U && id == state.pendingScreenCaptureId) {
        state.pendingScreenCaptureConsumed = true;
    }
}

void SceneRenderFeedback::CompleteScreenCapture(Scene& scene, std::uint64_t id, bool succeeded) noexcept {
    SceneState& state = SceneAccess::State(scene);
    if (id == 0U || id != state.pendingScreenCaptureId) {
        return;
    }
    state.pendingScreenCaptureId = 0U;
    state.pendingScreenCapturePath.clear();
    state.pendingScreenCaptureReturnsPixels = false;
    state.pendingScreenCaptureConsumed = false;
    state.lastScreenCaptureId = id;
    state.lastScreenCaptureSucceeded = succeeded;
    state.lastScreenCapturePixels.reset();
}

void SceneRenderFeedback::CompleteScreenCapturePixels(
    Scene& scene,
    std::uint64_t id,
    SceneScreenCapturePixels pixels) noexcept {
    SceneState& state = SceneAccess::State(scene);
    if (id == 0U || id != state.pendingScreenCaptureId) {
        return;
    }
    if (!state.pendingScreenCaptureReturnsPixels || pixels.width == 0U ||
        pixels.height == 0U || pixels.bytes.empty()) {
        CompleteScreenCapture(scene, id, false);
        return;
    }
    state.pendingScreenCaptureId = 0U;
    state.pendingScreenCapturePath.clear();
    state.pendingScreenCaptureReturnsPixels = false;
    state.pendingScreenCaptureConsumed = false;
    state.lastScreenCaptureId = id;
    state.lastScreenCaptureSucceeded = true;
    state.lastScreenCapturePixels = std::move(pixels);
}

const SceneRenderVisibilityEntry* SceneRenderFeedback::FindEntry(const Scene& scene, SceneEntity entity) noexcept {
    if (!entity.IsValid()) {
        return nullptr;
    }
    const SceneState& state = SceneAccess::State(scene);
    if (state.renderVisibilityPublishCount == 0U) {
        return nullptr;
    }
    const auto frameIt =
        state.renderVisibilityFrames.find(state.lastRenderVisibilityLocalUserId);
    if (frameIt == state.renderVisibilityFrames.end()) {
        return nullptr;
    }
    const std::vector<SceneRenderVisibilityEntry>& entries =
        frameIt->second.entries;
    const auto it = std::lower_bound(
        entries.begin(),
        entries.end(),
        entity.Id(),
        [](const SceneRenderVisibilityEntry& entry, std::uint64_t entityId) noexcept { return entry.entityId < entityId; });
    return it == entries.end() || it->entityId != entity.Id() ? nullptr : &(*it);
}

const SceneRenderVisibilityFrame* SceneRenderFeedback::FindFrame(
    const Scene& scene, kb::input::LocalUserId localUser) noexcept {
    const SceneState& state = SceneAccess::State(scene);
    if (state.renderVisibilityPublishCount == 0U) {
        return nullptr;
    }
    const auto frameIt = state.renderVisibilityFrames.find(localUser.value);
    return frameIt == state.renderVisibilityFrames.end() ? nullptr
                                                         : &frameIt->second;
}

} // namespace kb::scene
