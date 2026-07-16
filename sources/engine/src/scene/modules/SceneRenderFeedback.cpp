#include "engine/scene/SceneRenderFeedback.hpp"

#include "engine/scene/Scene.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

#include <algorithm>
#include <utility>

namespace kb::scene {

void SceneRenderFeedback::Publish(Scene& scene, SceneRenderVisibilityFrame& frame) noexcept {
    SceneState& state = SceneAccess::State(scene);
    std::swap(state.renderVisibilityFrame.entries, frame.entries);
    state.renderVisibilityFrame.frustumValid = frame.frustumValid;
    state.renderVisibilityFrame.viewportId = frame.viewportId;
    state.renderVisibilityFrame.frustumPlanes = frame.frustumPlanes;
    ++state.renderVisibilityPublishCount;
}

void SceneRenderFeedback::Clear(Scene& scene) noexcept {
    SceneState& state = SceneAccess::State(scene);
    state.renderVisibilityFrame = SceneRenderVisibilityFrame{};
    state.renderVisibilityPublishCount = 0U;
}

bool SceneRenderFeedback::HasFrame(const Scene& scene) noexcept {
    return SceneAccess::State(scene).renderVisibilityPublishCount != 0U;
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
    if (state.renderVisibilityPublishCount == 0U || !state.renderVisibilityFrame.frustumValid) {
        return false;
    }
    for (const SceneRenderFrustumPlane& plane : state.renderVisibilityFrame.frustumPlanes) {
        const float distance = plane.x * center.x + plane.y * center.y + plane.z * center.z + plane.w;
        if (distance < -radius) {
            return false;
        }
    }
    return true;
}

const SceneRenderVisibilityEntry* SceneRenderFeedback::FindEntry(const Scene& scene, SceneEntity entity) noexcept {
    if (!entity.IsValid()) {
        return nullptr;
    }
    const SceneState& state = SceneAccess::State(scene);
    if (state.renderVisibilityPublishCount == 0U) {
        return nullptr;
    }
    const std::vector<SceneRenderVisibilityEntry>& entries = state.renderVisibilityFrame.entries;
    const auto it = std::lower_bound(
        entries.begin(),
        entries.end(),
        entity.Id(),
        [](const SceneRenderVisibilityEntry& entry, std::uint64_t entityId) noexcept { return entry.entityId < entityId; });
    return it == entries.end() || it->entityId != entity.Id() ? nullptr : &(*it);
}

} // namespace kb::scene
