#include "engine/scene/SceneAnimators.hpp"

#include "scene/SceneAnimatorService.hpp"

namespace kb::scene {

SceneAnimatorQueries::SceneAnimatorQueries(const Scene& scene) noexcept : scene_(scene) {}
bool SceneAnimatorQueries::Exists(SceneEntity entity) const noexcept { return SceneAnimatorService::Exists(scene_, entity); }
std::uint64_t SceneAnimatorQueries::Controller(SceneEntity entity) const noexcept { return SceneAnimatorService::Controller(scene_, entity); }
std::span<const AnimatorParameterValue> SceneAnimatorQueries::Parameters(SceneEntity entity) const noexcept { return SceneAnimatorService::Parameters(scene_, entity); }
float SceneAnimatorQueries::Speed(SceneEntity entity) const noexcept { return SceneAnimatorService::Speed(scene_, entity); }
std::uint64_t SceneAnimatorQueries::RuntimeBindingGeneration(SceneEntity entity) const noexcept { return SceneAnimatorService::RuntimeBindingGeneration(scene_, entity); }
std::optional<AnimatorInstanceSkeletonView> SceneAnimatorQueries::InstanceSkeleton(SceneEntity entity) const noexcept { return SceneAnimatorService::InstanceSkeleton(scene_, entity); }
std::optional<AnimatorAttachmentTransform> SceneAnimatorQueries::AttachmentTransform(SceneEntity entity, SkeletonBoneId boneId, const LocalTransform& localOffset) const noexcept { return SceneAnimatorService::AttachmentTransform(scene_, entity, boneId, localOffset); }
std::optional<AnimatorAttachmentTransform> SceneAnimatorQueries::SocketTransform(SceneEntity entity, std::string_view socketName) const noexcept { return SceneAnimatorService::SocketTransform(scene_, entity, socketName); }
std::optional<AnimatorStateInfo> SceneAnimatorQueries::State(SceneEntity entity, std::string_view layer) const { return SceneAnimatorService::State(scene_, entity, layer); }
std::shared_ptr<const AnimatorDebugSnapshot> SceneAnimatorQueries::DebugSnapshot() const { return SceneAnimatorService::DebugSnapshot(scene_); }

SceneAnimators::SceneAnimators(Scene& scene) noexcept : scene_(scene) {}
bool SceneAnimators::Exists(SceneEntity entity) const noexcept { return SceneAnimatorService::Exists(scene_, entity); }
std::uint64_t SceneAnimators::Controller(SceneEntity entity) const noexcept { return SceneAnimatorService::Controller(scene_, entity); }
std::span<const AnimatorParameterValue> SceneAnimators::Parameters(SceneEntity entity) const noexcept { return SceneAnimatorService::Parameters(scene_, entity); }
bool SceneAnimators::Play(SceneEntity entity, std::string_view layer, std::string_view state, float normalizedTime) noexcept { return SceneAnimatorService::Play(scene_, entity, layer, state, normalizedTime); }
bool SceneAnimators::SeekNormalized(SceneEntity entity, float normalizedTime) noexcept { return SceneAnimatorService::SeekNormalized(scene_, entity, normalizedTime); }
bool SceneAnimators::CrossFade(SceneEntity entity, std::string_view layer, std::string_view state, float durationSeconds, float normalizedTime) noexcept { return SceneAnimatorService::CrossFade(scene_, entity, layer, state, durationSeconds, normalizedTime); }
bool SceneAnimators::SetSpeed(SceneEntity entity, float speed) noexcept { return SceneAnimatorService::SetSpeed(scene_, entity, speed); }
float SceneAnimators::Speed(SceneEntity entity) const noexcept { return SceneAnimatorService::Speed(scene_, entity); }
float SceneAnimators::CurrentStateDuration(SceneEntity entity) const noexcept { return SceneAnimatorService::CurrentStateDuration(scene_, entity); }
std::uint64_t SceneAnimators::RuntimeBindingGeneration(SceneEntity entity) const noexcept { return SceneAnimatorService::RuntimeBindingGeneration(scene_, entity); }
std::optional<AnimatorInstanceSkeletonView> SceneAnimators::InstanceSkeleton(SceneEntity entity) const noexcept { return SceneAnimatorService::InstanceSkeleton(scene_, entity); }
std::optional<AnimatorAttachmentTransform> SceneAnimators::AttachmentTransform(SceneEntity entity, SkeletonBoneId boneId, const LocalTransform& localOffset) const noexcept { return SceneAnimatorService::AttachmentTransform(scene_, entity, boneId, localOffset); }
std::optional<AnimatorAttachmentTransform> SceneAnimators::SocketTransform(SceneEntity entity, std::string_view socketName) const noexcept { return SceneAnimatorService::SocketTransform(scene_, entity, socketName); }
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
std::shared_ptr<const AnimatorDebugSnapshot> SceneAnimators::DebugSnapshot() const { return SceneAnimatorService::DebugSnapshot(scene_); }
void SceneAnimators::WaitForDebugSnapshot() { SceneAnimatorService::WaitForDebugSnapshot(scene_); }
std::vector<AnimationEventRecord> SceneAnimators::DrainEvents() { return SceneAnimatorService::DrainEvents(scene_); }

} // namespace kb::scene
