#pragma once

#include "engine/scene/AnimationAssets.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SocketAttachmentComponent.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {

class Scene;

struct AnimatorPoseSoaView {
    std::span<const Vec3> positions;
    std::span<const Quat> rotations;
    std::span<const Vec3> scales;
};

struct AnimatorMorphWeightsView {
    std::span<const std::string> targetNames;
    std::span<const float> currentWeights;
    std::span<const float> previousWeights;
};

struct AnimatorInstanceSkeletonView {
    std::uint64_t skeletonAssetId = 0U;
    std::uint64_t compatibilitySignature = 0U;
    std::span<const SkeletonBoneId> boneIds;
    AnimatorPoseSoaView currentLocalPose;
    AnimatorPoseSoaView previousLocalPose;
    AnimatorPoseSoaView currentComponentPose;
    AnimatorPoseSoaView previousComponentPose;
    std::span<const kb::math::Mat4> currentSkinMatrices;
    std::span<const kb::math::Mat4> previousSkinMatrices;
    AnimatorMorphWeightsView morphWeights;
    std::uint64_t evaluationCount = 0U;
    std::uint64_t hierarchySolveCount = 0U;
};

struct AnimatorAttachmentTransform {
    LocalTransform componentSpace;
    WorldTransform worldSpace;
};

// Where a SocketAttachment holds its object: the character it found above it and whether its
// socket (or bone) exists there - `resolvedName` is that socket's or bone's own name. Before Play
// it is judged against the character's authored skeleton. `error` says in plain words what to fix.
struct SocketAttachmentStatus {
    SceneEntity character{};
    bool attached = false;
    std::string resolvedName;
    std::string error;
};

class SceneAnimatorQueries {
public:
    explicit SceneAnimatorQueries(const Scene& scene) noexcept;
    [[nodiscard]] bool Exists(SceneEntity entity) const noexcept;
    [[nodiscard]] std::uint64_t Controller(SceneEntity entity) const noexcept;
    [[nodiscard]] std::span<const AnimatorParameterValue> Parameters(SceneEntity entity) const noexcept;
    [[nodiscard]] float Speed(SceneEntity entity) const noexcept;
    // Derived attachment identity for runtime backends which retain work
    // across frames. Zero means no valid Animator runtime is attached.
    [[nodiscard]] std::uint64_t RuntimeBindingGeneration(SceneEntity entity) const noexcept;
    [[nodiscard]] std::optional<AnimatorInstanceSkeletonView> InstanceSkeleton(
        SceneEntity entity) const noexcept;
    [[nodiscard]] std::optional<AnimatorAttachmentTransform> AttachmentTransform(
        SceneEntity entity, SkeletonBoneId boneId,
        const LocalTransform& localOffset = {}) const noexcept;
    [[nodiscard]] std::optional<AnimatorAttachmentTransform> SocketTransform(
        SceneEntity entity, std::string_view socketName) const noexcept;
    // Socket names of the skeleton the entity animates (or is authored with), then its bone names
    // (both work as sockets).
    [[nodiscard]] std::vector<std::string> SocketNames(SceneEntity entity) const;
    [[nodiscard]] SocketAttachmentStatus SocketAttachment(SceneEntity attachedEntity) const;
    [[nodiscard]] std::optional<AnimatorStateInfo> State(SceneEntity entity, std::string_view layer) const;
    [[nodiscard]] std::shared_ptr<const AnimatorDebugSnapshot> DebugSnapshot() const;

private:
    const Scene& scene_;
};

class SceneAnimators {
public:
    static constexpr std::size_t kMaxPendingEvents = 4096U;
    explicit SceneAnimators(Scene& scene) noexcept;
    [[nodiscard]] bool Exists(SceneEntity entity) const noexcept;
    [[nodiscard]] std::uint64_t Controller(SceneEntity entity) const noexcept;
    [[nodiscard]] std::span<const AnimatorParameterValue> Parameters(SceneEntity entity) const noexcept;
    [[nodiscard]] bool Play(SceneEntity entity, std::string_view layer, std::string_view state, float normalizedTime = 0.0F) noexcept;
    // Re-samples every active controller layer at one normalized instant. This
    // bypasses delta-time accumulation and cancels transitions, making editor
    // and cinematic scrubbing independent of the path taken to reach a pose.
    [[nodiscard]] bool SeekNormalized(SceneEntity entity, float normalizedTime) noexcept;
    [[nodiscard]] bool CrossFade(SceneEntity entity, std::string_view layer, std::string_view state, float durationSeconds, float normalizedTime = 0.0F) noexcept;
    [[nodiscard]] bool SetSpeed(SceneEntity entity, float speed) noexcept;
    [[nodiscard]] float Speed(SceneEntity entity) const noexcept;
    [[nodiscard]] float CurrentStateDuration(SceneEntity entity) const noexcept;
    [[nodiscard]] std::uint64_t RuntimeBindingGeneration(SceneEntity entity) const noexcept;
    [[nodiscard]] std::optional<AnimatorInstanceSkeletonView> InstanceSkeleton(
        SceneEntity entity) const noexcept;
    [[nodiscard]] std::optional<AnimatorAttachmentTransform> AttachmentTransform(
        SceneEntity entity, SkeletonBoneId boneId,
        const LocalTransform& localOffset = {}) const noexcept;
    [[nodiscard]] std::optional<AnimatorAttachmentTransform> SocketTransform(
        SceneEntity entity, std::string_view socketName) const noexcept;
    [[nodiscard]] std::vector<std::string> SocketNames(SceneEntity entity) const;
    [[nodiscard]] SocketAttachmentStatus SocketAttachment(SceneEntity attachedEntity) const;
    // Moves every SocketAttachment to its socket now; the animator system does this each frame
    // after posing, so call it only after changing poses or attachments outside the frame.
    void ApplySocketAttachments();
    // Hangs `object` on `character`'s socket (or bone; "hand_r" also finds "hand.R"): parents it
    // under the character unless it is below it already and places it at once. On failure nothing
    // changes and `error` says why (an unknown socket, no skeleton above it, ...).
    [[nodiscard]] bool AttachToSocket(SceneEntity object, SceneEntity character, std::string_view socket,
        const SocketAttachmentOffset& offset = {}, std::string* error = nullptr);
    // Stops following the socket; the object stays where it is, under the same parent.
    void DetachFromSocket(SceneEntity object);
    [[nodiscard]] bool SetBool(SceneEntity entity, std::string_view name, bool value) noexcept;
    [[nodiscard]] bool SetInt(SceneEntity entity, std::string_view name, std::int32_t value) noexcept;
    [[nodiscard]] bool SetFloat(SceneEntity entity, std::string_view name, float value) noexcept;
    [[nodiscard]] bool SetTrigger(SceneEntity entity, std::string_view name) noexcept;
    [[nodiscard]] bool ResetTrigger(SceneEntity entity, std::string_view name) noexcept;
    [[nodiscard]] bool SetIkTarget(
        SceneEntity entity, std::string_view name,
        const AnimatorIkTarget& target) noexcept;
    [[nodiscard]] bool ClearIkTarget(
        SceneEntity entity, std::string_view name) noexcept;
    [[nodiscard]] std::optional<AnimatorStateInfo> State(SceneEntity entity, std::string_view layer) const;
    [[nodiscard]] std::shared_ptr<const AnimatorDebugSnapshot> DebugSnapshot() const;
    // Joins the in-flight asynchronous debug snapshot build, so the next
    // DebugSnapshot() read observes the newest published revision. Used by
    // headless automation and tests which need a deterministic read point;
    // live editor panels simply tolerate one snapshot of latency.
    void WaitForDebugSnapshot();
    [[nodiscard]] std::vector<AnimationEventRecord> DrainEvents();

private:
    Scene& scene_;
};

} // namespace kb::scene
