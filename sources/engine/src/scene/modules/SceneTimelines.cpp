#include "engine/scene/SceneTimelines.hpp"

#include "scene/SceneTimelineService.hpp"

namespace kb::scene {

SceneTimelineQueries::SceneTimelineQueries(const Scene& scene) noexcept
    : scene_(scene) {}
bool SceneTimelineQueries::Exists(std::uint64_t instance) const noexcept {
    return SceneTimelineService::Exists(scene_, instance);
}
bool SceneTimelineQueries::IsPlaying(std::uint64_t instance) const noexcept {
    return SceneTimelineService::IsPlaying(scene_, instance);
}
float SceneTimelineQueries::Time(std::uint64_t instance) const noexcept {
    return SceneTimelineService::Time(scene_, instance);
}
std::uint64_t SceneTimelineQueries::Asset(std::uint64_t instance) const noexcept {
    return SceneTimelineService::Asset(scene_, instance);
}

SceneTimelines::SceneTimelines(Scene& scene) noexcept : scene_(scene) {}
std::uint64_t SceneTimelines::Create(
    std::uint64_t assetId, SceneEntity owner) {
    return SceneTimelineService::Create(scene_, assetId, owner);
}
bool SceneTimelines::Release(std::uint64_t instance) noexcept {
    return SceneTimelineService::Release(scene_, instance);
}
bool SceneTimelines::Exists(std::uint64_t instance) const noexcept {
    return SceneTimelineService::Exists(scene_, instance);
}
bool SceneTimelines::Play(std::uint64_t instance) noexcept {
    return SceneTimelineService::Play(scene_, instance);
}
bool SceneTimelines::Pause(std::uint64_t instance) noexcept {
    return SceneTimelineService::Pause(scene_, instance);
}
bool SceneTimelines::IsPlaying(std::uint64_t instance) const noexcept {
    return SceneTimelineService::IsPlaying(scene_, instance);
}
bool SceneTimelines::Seek(std::uint64_t instance, float timeSeconds) {
    return SceneTimelineService::Seek(scene_, instance, timeSeconds);
}
bool SceneTimelines::Skip(
    std::uint64_t instance, float targetTimeSeconds,
    TimelineSkipMarkerPolicy markerPolicy) {
    return SceneTimelineService::Skip(
        scene_, instance, targetTimeSeconds, markerPolicy);
}
bool SceneTimelines::Bind(
    std::uint64_t instance, const std::string& binding,
    SceneEntity target) {
    return SceneTimelineService::Bind(scene_, instance, binding, target);
}
float SceneTimelines::Time(std::uint64_t instance) const noexcept {
    return SceneTimelineService::Time(scene_, instance);
}
std::uint64_t SceneTimelines::Asset(std::uint64_t instance) const noexcept {
    return SceneTimelineService::Asset(scene_, instance);
}
std::vector<TimelineMarkerEvent> SceneTimelines::DrainMarkerEvents() {
    return SceneTimelineService::DrainMarkerEvents(scene_);
}

} // namespace kb::scene
