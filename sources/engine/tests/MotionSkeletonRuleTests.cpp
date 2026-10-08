#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/scene/AnimationAssetIO.hpp"
#include "engine/scene/MotionSkeletonRuleComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAnimators.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneMotionSkeletonRuleComponents.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SkeletonAssetIO.hpp"
#include "engine/scene/SkeletonBindingComponent.hpp"

#include <cmath>
#include <filesystem>
#include <string>
#include <string_view>

namespace kb::tests {
namespace {

using kb::math::Quat;
using kb::math::Vec3;

constexpr kb::scene::SkeletonBoneId kRoot = 700U;
constexpr kb::scene::SkeletonBoneId kUpper = 101U;
constexpr kb::scene::SkeletonBoneId kLower = 102U;
constexpr kb::scene::SkeletonBoneId kHand = 900U;
constexpr kb::scene::SkeletonBoneId kTwist = 950U;
// Pose indices follow the skeleton's bone order.
constexpr std::size_t kUpperIndex = 1U;
constexpr std::size_t kLowerIndex = 2U;
constexpr std::size_t kHandIndex = 3U;
constexpr std::size_t kTwistIndex = 4U;

[[nodiscard]] Quat AboutY(float degrees) {
    const float half = degrees * kb::math::kPi / 360.0F;
    return Quat{ 0.0F, std::sin(half), 0.0F, std::cos(half) };
}

[[nodiscard]] bool SameRotation(Quat a, Quat b) {
    const float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    return std::fabs(std::fabs(dot) - 1.0F) <= 1.0e-4F;
}

// Root -> Upper -> Lower -> Hand straight up +Y, one unit apart, with a Twist helper beside the hand.
struct RuleFixture {
    std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb-motion-skeleton-rule-tests";
    kb::scene::Scene scene;
    std::uint64_t skeletonId = 0U;
    std::uint64_t signature = 0U;
    std::uint64_t staticController = 0U;
    std::uint64_t swingController = 0U;

    RuleFixture() {
        std::filesystem::remove_all(root);
        kb::scene::SkeletonAsset skeleton{};
        skeleton.bones = {
            { .id = kRoot, .parentIndex = -1, .name = "Root", .referencePose = {}, .inverseBind = {} },
            { .id = kUpper, .parentIndex = 0, .name = "Upper", .referencePose = { .position = { 0.0F, 1.0F, 0.0F } }, .inverseBind = {} },
            { .id = kLower, .parentIndex = 1, .name = "Lower", .referencePose = { .position = { 0.0F, 1.0F, 0.0F } }, .inverseBind = {} },
            { .id = kHand, .parentIndex = 2, .name = "Hand", .referencePose = { .position = { 0.0F, 1.0F, 0.0F } }, .inverseBind = {} },
            { .id = kTwist, .parentIndex = 2, .name = "Twist", .referencePose = { .position = { 0.0F, 0.5F, 0.0F } }, .inverseBind = {} },
        };
        signature = kb::scene::SkeletonCompatibilitySignature(skeleton);
        Require(kb::scene::SkeletonAssetIO::Save(root / "Assets" / "Rig" / "Arm.kbskeleton", skeleton),
            "Motion skeleton rule fixture could not save its skeleton");
        Require(scene.Assets().MountProject(root) && scene.Assets().Discover() == 1U,
            "Motion skeleton rule fixture could not discover its skeleton");
        skeletonId = scene.Assets().Manager().Registry().FindByPath("/Game/Rig/Arm.kbskeleton")->id.value;

        // Static: Lower turned 120 degrees and Hand 90 degrees about Y. Swing: Hand turns from 0 to 90 degrees
        // over one second.
        const auto key = [](float time, Quat rotation, Vec3 position) {
            return kb::scene::AnimationBoneKeyframe{ .timeSeconds = time, .transform = { .position = position, .rotation = rotation } };
        };
        kb::scene::AnimationClip staticClip{ .durationSeconds = 1.0F, .looping = true };
        staticClip.targetSkeletonAssetId = skeletonId;
        staticClip.targetSkeletonCompatibilitySignature = signature;
        staticClip.skeletalTracks = {
            { .boneId = kLower, .keyframes = { key(0.0F, AboutY(120.0F), { 0.0F, 1.0F, 0.0F }), key(1.0F, AboutY(120.0F), { 0.0F, 1.0F, 0.0F }) } },
            { .boneId = kHand, .keyframes = { key(0.0F, AboutY(90.0F), { 0.0F, 1.0F, 0.0F }), key(1.0F, AboutY(90.0F), { 0.0F, 1.0F, 0.0F }) } },
        };
        kb::scene::AnimationClip swingClip{ .durationSeconds = 1.0F, .looping = false };
        swingClip.targetSkeletonAssetId = skeletonId;
        swingClip.targetSkeletonCompatibilitySignature = signature;
        swingClip.skeletalTracks = {
            { .boneId = kHand, .keyframes = { key(0.0F, AboutY(0.0F), { 0.0F, 1.0F, 0.0F }), key(1.0F, AboutY(90.0F), { 0.0F, 1.0F, 0.0F }) } },
        };
        Require(kb::scene::AnimationAssetIO::SaveClip(root / "Assets" / "Rig" / "Static.kbanim", staticClip) &&
                kb::scene::AnimationAssetIO::SaveClip(root / "Assets" / "Rig" / "Swing.kbanim", swingClip),
            "Motion skeleton rule fixture could not save its clips");
        const auto controller = [](std::string clip) {
            kb::scene::AnimatorController value{};
            value.layers = { { .name = "Base", .defaultState = "Pose", .states = { { .name = "Pose", .clipReference = std::move(clip) } } } };
            return value;
        };
        Require(kb::scene::AnimationAssetIO::SaveController(root / "Assets" / "Rig" / "Static.kbanimcontroller", controller("/Game/Rig/Static.kbanim")) &&
                kb::scene::AnimationAssetIO::SaveController(root / "Assets" / "Rig" / "Swing.kbanimcontroller", controller("/Game/Rig/Swing.kbanim")),
            "Motion skeleton rule fixture could not save its controllers");
        static_cast<void>(scene.Assets().Discover());
        staticController = scene.Assets().Manager().Registry().FindByPath("/Game/Rig/Static.kbanimcontroller")->id.value;
        swingController = scene.Assets().Manager().Registry().FindByPath("/Game/Rig/Swing.kbanimcontroller")->id.value;
    }
    ~RuleFixture() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }

    [[nodiscard]] kb::scene::SceneEntity Owner(std::uint64_t controllerId, const kb::scene::MotionSkeletonRuleComponent& rule) {
        const kb::scene::SceneObject object = scene.Entities().CreateObject({ .name = "Arm" });
        Require(scene.Components().SkeletonBindings().Set(object.Entity(), kb::scene::SkeletonBindingComponent{
                    .skeletonAssetId = skeletonId, .skeletonCompatibilitySignature = signature, .enabled = true }),
            "Motion skeleton rule owner could not bind its skeleton");
        scene.Components().Animators().Set(object.Entity(), kb::scene::Animator{ .controllerAssetId = controllerId, .speed = 1.0F, .enabled = true });
        Require(scene.Components().MotionSkeletonRules().Set(object.Entity(), rule), "The motion skeleton rule was refused");
        return object.Entity();
    }

    [[nodiscard]] kb::scene::AnimatorInstanceSkeletonView Pose(kb::scene::SceneEntity owner) {
        const std::optional<kb::scene::AnimatorInstanceSkeletonView> pose = scene.Animators().InstanceSkeleton(owner);
        Require(pose.has_value(), "The rule owner has no evaluated skeleton pose");
        return *pose;
    }

    void Update(float seconds) {
        static_cast<void>(scene.Runtime().Update(seconds));
        const std::vector<std::string> errors = scene.Runtime().DrainSceneSystemErrors();
        Require(errors.empty(), errors.empty() ? "" : errors.front().c_str());
    }
};

[[nodiscard]] kb::scene::MotionSkeletonRuleComponent Rule(kb::scene::MotionSkeletonRuleKind kind, kb::scene::SkeletonBoneId bone,
    std::string_view target = {}) {
    kb::scene::MotionSkeletonRuleComponent rule{ .kind = kind, .constrainedBoneId = bone, .enabled = true };
    kb::scene::SetMotionSkeletonRuleTargetText(rule, target);
    return rule;
}

void TestTargetRules() {
    RuleFixture fixture;
    const kb::scene::SceneEntity aim = fixture.Owner(fixture.staticController, Rule(kb::scene::MotionSkeletonRuleKind::Aim, kHand, "Look"));
    kb::scene::MotionSkeletonRuleComponent chain = Rule(kb::scene::MotionSkeletonRuleKind::ChainIk, kUpper, "Reach");
    chain.midBoneId = kLower;
    chain.tipBoneId = kHand;
    kb::scene::SetMotionSkeletonRulePoleTargetText(chain, "Elbow");
    const kb::scene::SceneEntity reach = fixture.Owner(fixture.staticController, chain);
    const kb::scene::SceneEntity corrected = fixture.Owner(fixture.staticController, Rule(kb::scene::MotionSkeletonRuleKind::SpaceCorrection, kLower, "Fix"));
    fixture.Update(0.0F);

    // Untargeted rules leave the animated pose alone.
    Require(SameRotation(fixture.Pose(aim).currentLocalPose.rotations[kHandIndex], AboutY(90.0F)),
        "A rule whose target is not set must not change the pose");
    Require(fixture.scene.Animators().SetIkTarget(aim, "Look", kb::scene::AnimatorIkTarget{ .worldPosition = { 3.0F, 3.0F, 0.0F } }) &&
            fixture.scene.Animators().SetIkTarget(reach, "Reach", kb::scene::AnimatorIkTarget{ .worldPosition = { 0.0F, 2.2F, 0.0F } }) &&
            fixture.scene.Animators().SetIkTarget(reach, "Elbow", kb::scene::AnimatorIkTarget{ .worldPosition = { -1.0F, 2.0F, 0.0F } }) &&
            fixture.scene.Animators().SetIkTarget(corrected, "Fix", kb::scene::AnimatorIkTarget{ .worldPosition = { 2.0F, 2.0F, 0.0F } }),
        "IK targets named by a motion skeleton rule must be accepted");
    Require(!fixture.scene.Animators().SetIkTarget(aim, "Elsewhere", kb::scene::AnimatorIkTarget{}),
        "IK target names no rule or constraint uses must still be refused");
    fixture.Update(0.0F);

    const kb::scene::AnimatorInstanceSkeletonView aimed = fixture.Pose(aim);
    const Vec3 forward = kb::math::Rotate(aimed.currentComponentPose.rotations[kHandIndex], Vec3{ 0.0F, 0.0F, 1.0F });
    Require(kb::math::Dot(forward, Vec3{ 1.0F, 0.0F, 0.0F }) >= 0.999F, "An Aim rule must point the bone's +Z at its target");

    const kb::scene::AnimatorInstanceSkeletonView reached = fixture.Pose(reach);
    Require(kb::math::Length(reached.currentComponentPose.positions[kHandIndex] - Vec3{ 0.0F, 2.2F, 0.0F }) <= 1.0e-3F,
        "A ChainIk rule must bring the chain's tip onto its target");
    Require(kb::math::Length(reached.currentComponentPose.positions[kLowerIndex] - Vec3{ -0.8F, 1.6F, 0.0F }) <= 1.0e-3F,
        "A ChainIk rule must bend its chain toward the pole target");
    Require(kb::math::Length(reached.currentComponentPose.positions[kUpperIndex] - Vec3{ 0.0F, 1.0F, 0.0F }) <= 1.0e-4F,
        "A ChainIk rule must keep the chain's root in place");

    const kb::scene::AnimatorInstanceSkeletonView fixed = fixture.Pose(corrected);
    Require(kb::math::Length(fixed.currentComponentPose.positions[kLowerIndex] - Vec3{ 2.0F, 2.0F, 0.0F }) <= 1.0e-4F &&
            kb::math::Length(fixed.currentComponentPose.positions[kHandIndex] - Vec3{ 2.0F, 3.0F, 0.0F }) <= 1.0e-4F,
        "A SpaceCorrection rule must move its bone onto the target and carry the children along");
}

void TestLocalRules() {
    RuleFixture fixture;
    kb::scene::MotionSkeletonRuleComponent twist = Rule(kb::scene::MotionSkeletonRuleKind::Twist, kTwist);
    twist.sourceBoneId = kHand;
    twist.axis = Vec3{ 0.0F, 1.0F, 0.0F };
    twist.weight = 0.5F;
    const kb::scene::SceneEntity twisted = fixture.Owner(fixture.staticController, twist);
    kb::scene::MotionSkeletonRuleComponent limit = Rule(kb::scene::MotionSkeletonRuleKind::Limit, kLower);
    limit.axis = Vec3{ 0.0F, 2.0F, 0.0F };
    limit.minAngleDegrees = -30.0F;
    limit.maxAngleDegrees = 30.0F;
    const kb::scene::SceneEntity limited = fixture.Owner(fixture.staticController, limit);
    const kb::scene::SceneEntity draft = fixture.Owner(fixture.staticController, kb::scene::MotionSkeletonRuleComponent{});
    fixture.Update(0.0F);

    Require(SameRotation(fixture.Pose(twisted).currentLocalPose.rotations[kTwistIndex], AboutY(45.0F)),
        "A Twist rule must turn its bone by the weighted turn of its source bone about the axis");
    Require(SameRotation(fixture.Pose(limited).currentLocalPose.rotations[kLowerIndex], AboutY(30.0F)),
        "A Limit rule must hold the bone's turn about its axis within the limits");
    Require(SameRotation(fixture.Pose(limited).currentComponentPose.rotations[kHandIndex], AboutY(120.0F)),
        "A limited bone's children must follow its limited rotation");
    Require(SameRotation(fixture.Pose(draft).currentLocalPose.rotations[kLowerIndex], AboutY(120.0F)),
        "A disabled draft rule must not drive the pose");

    // Loosening the limit at run time takes effect on the next update.
    kb::scene::MotionSkeletonRuleComponent loose = *fixture.scene.Components().MotionSkeletonRules().TryGet(limited);
    loose.maxAngleDegrees = 180.0F;
    Require(fixture.scene.Components().MotionSkeletonRules().Set(limited, loose), "The edited motion skeleton rule was refused");
    fixture.Update(0.0F);
    Require(SameRotation(fixture.Pose(limited).currentLocalPose.rotations[kLowerIndex], AboutY(120.0F)),
        "An edited rule must apply on the next update");
}

void TestSpringRule() {
    RuleFixture fixture;
    kb::scene::MotionSkeletonRuleComponent spring = Rule(kb::scene::MotionSkeletonRuleKind::Spring, kHand);
    spring.halfLifeSeconds = 0.1F;
    const kb::scene::SceneEntity sprung = fixture.Owner(fixture.swingController, spring);
    const kb::scene::SceneEntity free = fixture.Owner(fixture.swingController, kb::scene::MotionSkeletonRuleComponent{});
    fixture.Update(0.0F);
    fixture.Update(0.1F);
    Require(SameRotation(fixture.Pose(free).currentComponentPose.rotations[kHandIndex], AboutY(9.0F)),
        "The unsprung hand must follow the clip");
    Require(SameRotation(fixture.Pose(sprung).currentComponentPose.rotations[kHandIndex], AboutY(4.5F)),
        "A Spring rule must close half of the distance to the animated rotation per half-life");
    for (int frame = 0; frame < 20; ++frame) fixture.Update(0.1F);
    Require(SameRotation(fixture.Pose(sprung).currentComponentPose.rotations[kHandIndex], AboutY(90.0F)),
        "A Spring rule must settle on the animated rotation once it stops changing");
}

} // namespace

void RunMotionSkeletonRuleTests() {
    TestTargetRules();
    TestLocalRules();
    TestSpringRule();
}

} // namespace kb::tests
