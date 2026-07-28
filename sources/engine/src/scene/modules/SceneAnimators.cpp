#include "engine/scene/SceneAnimators.hpp"

#include "scene/SceneAnimatorService.hpp"

namespace kb::scene {

SceneAnimatorQueries::SceneAnimatorQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneAnimatorQueries::Exists(SceneEntity entity) const noexcept { return SceneAnimatorService::Exists(scene_, entity); }
std::uint64_t SceneAnimatorQueries::Controller(SceneEntity entity) const noexcept { return SceneAnimatorService::Controller(scene_, entity); }
std::span<const AnimatorParameterValue> SceneAnimatorQueries::Parameters(SceneEntity entity) const noexcept { return SceneAnimatorService::Parameters(scene_, entity); }
float SceneAnimatorQueries::Speed(SceneEntity entity) const noexcept { return SceneAnimatorService::Speed(scene_, entity); }
std::optional<AnimatorStateInfo> SceneAnimatorQueries::State(SceneEntity entity, std::string_view layer) const { return SceneAnimatorService::State(scene_, entity, layer); }

SceneAnimators::SceneAnimators(Scene& scene) noexcept : scene_(scene) {}
bool SceneAnimators::Exists(SceneEntity entity) const noexcept { return SceneAnimatorService::Exists(scene_, entity); }
std::uint64_t SceneAnimators::Controller(SceneEntity entity) const noexcept { return SceneAnimatorService::Controller(scene_, entity); }
std::span<const AnimatorParameterValue> SceneAnimators::Parameters(SceneEntity entity) const noexcept { return SceneAnimatorService::Parameters(scene_, entity); }
bool SceneAnimators::Play(SceneEntity entity, std::string_view layer, std::string_view state, float normalizedTime) noexcept { return SceneAnimatorService::Play(scene_, entity, layer, state, normalizedTime); }
bool SceneAnimators::CrossFade(SceneEntity entity, std::string_view layer, std::string_view state, float durationSeconds, float normalizedTime) noexcept { return SceneAnimatorService::CrossFade(scene_, entity, layer, state, durationSeconds, normalizedTime); }
bool SceneAnimators::SetSpeed(SceneEntity entity, float speed) noexcept { return SceneAnimatorService::SetSpeed(scene_, entity, speed); }
float SceneAnimators::Speed(SceneEntity entity) const noexcept { return SceneAnimatorService::Speed(scene_, entity); }
bool SceneAnimators::SetBool(SceneEntity entity, std::string_view name, bool value) noexcept { return SceneAnimatorService::SetBool(scene_, entity, name, value); }
bool SceneAnimators::SetInt(SceneEntity entity, std::string_view name, std::int32_t value) noexcept { return SceneAnimatorService::SetInt(scene_, entity, name, value); }
bool SceneAnimators::SetFloat(SceneEntity entity, std::string_view name, float value) noexcept { return SceneAnimatorService::SetFloat(scene_, entity, name, value); }
bool SceneAnimators::SetTrigger(SceneEntity entity, std::string_view name) noexcept { return SceneAnimatorService::SetTrigger(scene_, entity, name, true); }
bool SceneAnimators::ResetTrigger(SceneEntity entity, std::string_view name) noexcept { return SceneAnimatorService::SetTrigger(scene_, entity, name, false); }
bool SceneAnimators::SetIkTarget(
    SceneEntity entity, std::string_view name,
    const AnimatorIkTarget& target) noexcept {
    return SceneAnimatorService::SetIkTarget(scene_, entity, name, target);
}
bool SceneAnimators::ClearIkTarget(SceneEntity entity, std::string_view name) noexcept {
    return SceneAnimatorService::ClearIkTarget(scene_, entity, name);
}
std::optional<AnimatorStateInfo> SceneAnimators::State(SceneEntity entity, std::string_view layer) const { return SceneAnimatorService::State(scene_, entity, layer); }
std::vector<AnimationEventRecord> SceneAnimators::DrainEvents() { return SceneAnimatorService::DrainEvents(scene_); }

} // namespace kb::scene
