#include "TestSupport.hpp"
#include "TestSuites.hpp"

#include "engine/scene/AnimationAssetIO.hpp"
#include "engine/scene/AnimationChainIk.hpp"
#include "engine/scene/AnimationSpringBones.hpp"
#include "engine/scene/MotionSkeletonRuleComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAnimators.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneMotionSkeletonRuleComponents.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/SkeletonAssetIO.hpp"
#include "engine/scene/SkeletonBindingComponent.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
constexpr std::size_t kRootIndex = 0U;
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

    // Saves `controller` under Assets/Rig and returns its asset id.
    [[nodiscard]] std::uint64_t Register(std::string_view name, const kb::scene::AnimatorController& controller) {
        const std::string file = std::string{ name } + ".kbanimcontroller";
        Require(kb::scene::AnimationAssetIO::SaveController(root / "Assets" / "Rig" / file, controller),
            "A test controller must save");
        static_cast<void>(scene.Assets().Discover());
        return scene.Assets().Manager().Registry().FindByPath("/Game/Rig/" + file)->id.value;
    }

    void Move(kb::scene::SceneEntity entity, Vec3 position) {
        scene.Transforms().Set(entity, kb::scene::TransformComponent{ .localPosition = position });
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

using kb::scene::ChainIkSettings;
using kb::scene::ChainIkSolver;

[[nodiscard]] std::vector<Vec3> StraightChain(std::size_t count) {
    std::vector<Vec3> joints(count);
    for (std::size_t index = 0U; index < count; ++index) joints[index] = Vec3{ 0.0F, static_cast<float>(index), 0.0F };
    return joints;
}

[[nodiscard]] bool KeepsUnitLengths(std::span<const Vec3> joints) {
    for (std::size_t index = 0U; index + 1U < joints.size(); ++index) {
        if (std::fabs(kb::math::Length(joints[index + 1U] - joints[index]) - 1.0F) > 1.0e-4F) return false;
    }
    return true;
}

// The unit direction, across the root-to-effector axis, of the most bent joint; zero for a straight chain.
[[nodiscard]] Vec3 BendSide(std::span<const Vec3> joints) {
    const Vec3 root = joints.front();
    const Vec3 axis = kb::math::Normalize(joints.back() - root);
    Vec3 bent{};
    for (std::size_t index = 1U; index + 1U < joints.size(); ++index) {
        const Vec3 offset = (joints[index] - root) - axis * kb::math::Dot(joints[index] - root, axis);
        if (kb::math::Dot(offset, offset) > kb::math::Dot(bent, bent)) bent = offset;
    }
    return kb::math::Normalize(bent);
}

// How well the bend lies toward `pole`: 1 is exactly in the plane of the axis and the pole, on its side.
[[nodiscard]] float BendTowardsPole(std::span<const Vec3> joints, Vec3 pole) {
    const Vec3 axis = kb::math::Normalize(joints.back() - joints.front());
    const Vec3 side = kb::math::Normalize((pole - joints.front()) - axis * kb::math::Dot(pole - joints.front(), axis));
    const Vec3 bend = BendSide(joints);
    const bool sameCrossSign = kb::math::Dot(kb::math::Cross(axis, bend), kb::math::Cross(axis, side)) > 0.0F;
    return sameCrossSign ? kb::math::Dot(bend, side) : -1.0F;
}

void TestChainIkSolver() {
    const ChainIkSettings fabrik{ .solver = ChainIkSolver::Fabrik, .maxIterations = 64U, .tolerance = 1.0e-4F };
    const ChainIkSettings ccd{ .solver = ChainIkSolver::Ccd, .maxIterations = 256U, .tolerance = 1.0e-4F };
    std::array<float, 40> scratch{};

    for (const ChainIkSettings& settings : { fabrik, ccd }) {
        // A reachable target: the effector lands on it, the root stays, every bone keeps its length.
        std::vector<Vec3> reach = StraightChain(4U);
        const Vec3 target{ 1.5F, 1.5F, 1.0F };
        kb::scene::SolveChainIk(reach, scratch, target, std::nullopt, settings);
        Require(kb::math::Length(reach.back() - target) < 1.0e-3F, "Chain IK must bring a 4-bone chain's effector onto a reachable target");
        Require(kb::math::Length(reach.front()) == 0.0F && KeepsUnitLengths(reach), "Chain IK must keep the root and the bone lengths");

        // Out of reach: stretched straight toward the target.
        std::vector<Vec3> far = StraightChain(4U);
        kb::scene::SolveChainIk(far, scratch, Vec3{ 0.0F, 5.0F, 5.0F }, std::nullopt, settings);
        const Vec3 toward = kb::math::Normalize(Vec3{ 0.0F, 1.0F, 1.0F });
        for (std::size_t index = 0U; index < far.size(); ++index) {
            Require(kb::math::Length(far[index] - toward * static_cast<float>(index)) < 1.0e-4F,
                "A target out of reach must leave the chain straight toward it");
        }

        // The pole picks the side the chain bends to.
        for (const float side : { 5.0F, -5.0F }) {
            std::vector<Vec3> bent = StraightChain(4U);
            const Vec3 pole{ 0.0F, 1.5F, side };
            kb::scene::SolveChainIk(bent, scratch, Vec3{ 1.0F, 2.0F, 0.0F }, pole, settings);
            Require(BendTowardsPole(bent, pole) > 0.999F, "A pole must bend the chain toward its side");
            Require(kb::math::Length(bent.back() - Vec3{ 1.0F, 2.0F, 0.0F }) < 1.0e-3F && KeepsUnitLengths(bent),
                "A pole must not change where the effector lands or the bone lengths");
        }

        // A straight chain under a nearer target on its own line still folds.
        std::vector<Vec3> folded = StraightChain(4U);
        kb::scene::SolveChainIk(folded, scratch, Vec3{ 0.0F, 2.0F, 0.0F }, std::nullopt, settings);
        Require(kb::math::Length(folded.back() - Vec3{ 0.0F, 2.0F, 0.0F }) < 1.0e-3F && KeepsUnitLengths(folded),
            "A straight chain must fold onto a nearer target on its own line");

        // A long chain converges within the iteration limit.
        std::vector<Vec3> longChain = StraightChain(32U);
        const Vec3 longTarget{ 10.0F, 12.0F, 8.0F };
        kb::scene::SolveChainIk(longChain, scratch, longTarget, std::nullopt, settings);
        Require(kb::math::Length(longChain.back() - longTarget) < 1.0e-3F && KeepsUnitLengths(longChain),
            "A 32-bone chain must reach its target within the iteration limit");
    }

    std::vector<Vec3> bad = StraightChain(1U);
    bool refused = false;
    try {
        kb::scene::SolveChainIk(bad, scratch, Vec3{ 1.0F, 1.0F, 0.0F }, std::nullopt, fabrik);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    Require(refused, "A one-joint chain is not a chain");
    bad = { Vec3{}, Vec3{} };
    refused = false;
    try {
        kb::scene::SolveChainIk(bad, scratch, Vec3{ 1.0F, 1.0F, 0.0F }, std::nullopt, fabrik);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    Require(refused, "A zero-length bone must be refused");

    // A thousand 4-bone chains solve without touching the heap.
    std::array<Vec3, 4> joints{};
    std::array<float, 3> lengths{};
    float worst = 0.0F;
    BeginAllocationTally();
    for (int chain = 0; chain < 1000; ++chain) {
        joints = { Vec3{ 0.0F, 0.0F, 0.0F }, Vec3{ 0.0F, 1.0F, 0.0F }, Vec3{ 0.0F, 2.0F, 0.0F }, Vec3{ 0.0F, 3.0F, 0.0F } };
        const Vec3 target{ 1.0F + 0.001F * static_cast<float>(chain), 1.5F, 0.5F };
        kb::scene::SolveChainIk(joints, lengths, target, Vec3{ 0.0F, 1.0F, 3.0F }, fabrik);
        worst = std::max(worst, kb::math::Length(joints.back() - target));
    }
    const AllocationTally tally = EndAllocationTally();
    Require(tally.count == 0U, "Solving chains must not allocate");
    Require(worst < 1.0e-3F, "Every chain of the batch must reach its target");
}

// The chain Root -> Upper -> Lower -> Hand driven by a ChainIK rig constraint, with the Twist helper hanging off Lower.
void TestChainIkConstraint() {
    RuleFixture fixture;
    const auto controllerFor = [](ChainIkSettings settings, float weight) {
        kb::scene::AnimatorController value{};
        value.layers = { { .name = "Base", .defaultState = "Pose", .states = { { .name = "Pose", .clipReference = "/Game/Rig/Static.kbanim" } } } };
        value.rigConstraints.push_back({ .name = "Reach", .type = kb::scene::AnimatorRigConstraintType::ChainIK, .target = "Reach",
            .poleTarget = "Bend", .weight = weight, .constrainedBoneId = kRoot, .tipBoneId = kHand, .chainIk = settings });
        return value;
    };
    const auto discover = [&fixture](std::string_view name, const kb::scene::AnimatorController& controller) {
        return fixture.Register(name, controller);
    };

    // Settings survive a save and a load; invalid ones are refused.
    const ChainIkSettings ccd{ .solver = ChainIkSolver::Ccd, .maxIterations = 256U, .tolerance = 1.0e-4F };
    static_cast<void>(discover("ChainSaved", controllerFor(ccd, 0.75F)));
    const auto loaded = kb::scene::AnimationAssetIO::LoadController(fixture.root / "Assets" / "Rig" / "ChainSaved.kbanimcontroller");
    Require(loaded.has_value() && loaded->rigConstraints.size() == 1U, "A ChainIK controller must load");
    const kb::scene::AnimatorRigConstraint& saved = loaded->rigConstraints[0];
    Require(saved.type == kb::scene::AnimatorRigConstraintType::ChainIK && saved.constrainedBoneId == kRoot && saved.tipBoneId == kHand &&
            saved.midBoneId == 0U && saved.target == "Reach" && saved.poleTarget == "Bend" && saved.weight == 0.75F &&
            saved.chainIk.solver == ChainIkSolver::Ccd && saved.chainIk.maxIterations == 256U && saved.chainIk.tolerance == 1.0e-4F,
        "ChainIK settings must survive a save and a load");
    Require(!kb::scene::AnimationAssetIO::SaveController(fixture.root / "Assets" / "Rig" / "Bad.kbanimcontroller",
                controllerFor(ChainIkSettings{ .maxIterations = 0U }, 1.0F)),
        "A ChainIK constraint with no iterations must be refused");

    const kb::scene::SceneEntity fabrikOwner = fixture.Owner(
        discover("ChainFabrik", controllerFor(ChainIkSettings{ .solver = ChainIkSolver::Fabrik, .maxIterations = 64U, .tolerance = 1.0e-4F }, 1.0F)),
        kb::scene::MotionSkeletonRuleComponent{});
    const kb::scene::SceneEntity ccdOwner = fixture.Owner(discover("ChainCcd", controllerFor(ccd, 1.0F)), kb::scene::MotionSkeletonRuleComponent{});
    fixture.Update(0.0F);
    Require(kb::math::Length(fixture.Pose(fabrikOwner).currentComponentPose.positions[kHandIndex] - Vec3{ 0.0F, 3.0F, 0.0F }) < 1.0e-4F,
        "A ChainIK constraint without a target must leave the animated pose alone");

    const Vec3 reach{ 1.5F, 1.5F, 1.0F };
    const Vec3 bendTo{ 0.0F, 1.5F, 5.0F };
    const auto aim = [&fixture, reach, bendTo](kb::scene::SceneEntity owner, float positionWeight, float rotationWeight, Quat rotation) {
        Require(fixture.scene.Animators().SetIkTarget(owner, "Reach", kb::scene::AnimatorIkTarget{ .worldPosition = reach, .worldRotation = rotation,
                    .positionWeight = positionWeight, .rotationWeight = rotationWeight }) &&
                fixture.scene.Animators().SetIkTarget(owner, "Bend", kb::scene::AnimatorIkTarget{ .worldPosition = bendTo }),
            "ChainIK targets must be accepted");
        fixture.Update(0.0F);
    };
    const auto chainOf = [](const kb::scene::AnimatorInstanceSkeletonView& pose) {
        const std::span<const Vec3> positions = pose.currentComponentPose.positions;
        return std::array<Vec3, 4>{ positions[kRootIndex], positions[kUpperIndex], positions[kLowerIndex], positions[kHandIndex] };
    };

    aim(fabrikOwner, 1.0F, 0.0F, Quat{});
    aim(ccdOwner, 1.0F, 0.0F, Quat{});
    for (const kb::scene::SceneEntity owner : { fabrikOwner, ccdOwner }) {
        const kb::scene::AnimatorInstanceSkeletonView pose = fixture.Pose(owner);
        const std::array<Vec3, 4> chain = chainOf(pose);
        Require(kb::math::Length(chain[3] - reach) < 1.0e-3F, "The ChainIK constraint must bring the tip bone onto its target");
        Require(kb::math::Length(chain[0]) < 1.0e-4F, "The ChainIK constraint must keep the chain root in place");
        Require(KeepsUnitLengths(chain), "The ChainIK constraint must keep the bone lengths");
        const std::span<const Vec3> positions = pose.currentComponentPose.positions;
        Require(std::fabs(kb::math::Length(positions[kTwistIndex] - positions[kLowerIndex]) - 0.5F) < 1.0e-4F &&
                kb::math::Length(positions[kTwistIndex] - Vec3{ 0.0F, 2.5F, 0.0F }) > 0.1F,
            "A bone hanging off the chain must follow it");
        Require(BendTowardsPole(chain, bendTo) > 0.999F, "The ChainIK constraint must bend toward its pole target");
    }

    // The tip can also take the target's orientation.
    aim(fabrikOwner, 1.0F, 1.0F, AboutY(45.0F));
    Require(SameRotation(fixture.Pose(fabrikOwner).currentComponentPose.rotations[kHandIndex], AboutY(45.0F)),
        "A rotation weight must turn the tip bone to the target's orientation");

    // Weight 0 is the animated pose; weight 0.5 turns every bone halfway to the solved one.
    aim(fabrikOwner, 0.0F, 0.0F, Quat{});
    const kb::scene::AnimatorInstanceSkeletonView animated = fixture.Pose(fabrikOwner);
    Require(kb::math::Length(chainOf(animated)[3] - Vec3{ 0.0F, 3.0F, 0.0F }) < 1.0e-4F &&
            SameRotation(animated.currentLocalPose.rotations[kLowerIndex], AboutY(120.0F)) &&
            SameRotation(animated.currentLocalPose.rotations[kHandIndex], AboutY(90.0F)),
        "A zero weight must leave the animated pose");
    const std::array<Quat, 3> animatedLocal{ animated.currentLocalPose.rotations[kRootIndex], animated.currentLocalPose.rotations[kUpperIndex],
        animated.currentLocalPose.rotations[kLowerIndex] };
    aim(fabrikOwner, 1.0F, 0.0F, Quat{});
    const kb::scene::AnimatorInstanceSkeletonView solved = fixture.Pose(fabrikOwner);
    const std::array<Quat, 3> solvedLocal{ solved.currentLocalPose.rotations[kRootIndex], solved.currentLocalPose.rotations[kUpperIndex],
        solved.currentLocalPose.rotations[kLowerIndex] };
    aim(fabrikOwner, 0.5F, 0.0F, Quat{});
    const kb::scene::AnimatorInstanceSkeletonView halfway = fixture.Pose(fabrikOwner);
    for (std::size_t bone = 0U; bone < 3U; ++bone) {
        Require(SameRotation(halfway.currentLocalPose.rotations[kRootIndex + bone], kb::math::Slerp(animatedLocal[bone], solvedLocal[bone], 0.5F)),
            "A half weight must interpolate each bone's rotation between the animated and the solved pose");
    }

    // A chain must follow the skeleton's parent links and may not share bones with another constraint.
    kb::scene::AnimatorController overlapping = controllerFor(ChainIkSettings{}, 1.0F);
    overlapping.rigConstraints.push_back({ .name = "Inner", .type = kb::scene::AnimatorRigConstraintType::ChainIK, .target = "Inner",
        .constrainedBoneId = kUpper, .tipBoneId = kLower });
    kb::scene::AnimatorController unrelated = controllerFor(ChainIkSettings{}, 1.0F);
    unrelated.rigConstraints[0].constrainedBoneId = kTwist;
    for (const auto& [name, controller] : { std::pair{ "ChainOverlap", overlapping }, std::pair{ "ChainUnrelated", unrelated } }) {
        const kb::scene::SceneEntity owner = fixture.Owner(discover(name, controller), kb::scene::MotionSkeletonRuleComponent{});
        static_cast<void>(fixture.scene.Runtime().Update(0.0F));
        const std::vector<std::string> errors = fixture.scene.Runtime().DrainSceneSystemErrors();
        Require(!fixture.scene.Animators().Exists(owner) &&
                std::any_of(errors.begin(), errors.end(), [](const std::string& error) { return error.find("cannot bind") != std::string::npos; }),
            "A ChainIK constraint with an unrelated tip or shared bones must be refused at binding");
        fixture.scene.Entities().Destroy(owner);
    }
}


using kb::scene::SpringBoneCollider;
using kb::scene::SpringBoneSettings;
using kb::scene::SpringColliderShape;

constexpr float kFrame = 1.0F / 60.0F;

// Upper -> Lower -> Hand driven by SpringBones; the Root bone stays out of the chain and can carry colliders.
[[nodiscard]] kb::scene::AnimatorController SpringController(SpringBoneSettings settings, float weight = 1.0F) {
    kb::scene::AnimatorController value{};
    value.layers = { { .name = "Base", .defaultState = "Pose", .states = { { .name = "Pose", .clipReference = "/Game/Rig/Static.kbanim" } } } };
    value.rigConstraints.push_back({ .name = "Hair", .type = kb::scene::AnimatorRigConstraintType::SpringBones, .weight = weight,
        .constrainedBoneId = kUpper, .tipBoneId = kHand, .spring = std::move(settings) });
    return value;
}

// How far the chain's tip is from where the animation puts it, in the character's space.
[[nodiscard]] float TipLag(const kb::scene::AnimatorInstanceSkeletonView& pose) {
    return kb::math::Length(pose.currentComponentPose.positions[kHandIndex] - Vec3{ 0.0F, 3.0F, 0.0F });
}

[[nodiscard]] float BoneLengthError(const kb::scene::AnimatorInstanceSkeletonView& pose) {
    const std::span<const Vec3> positions = pose.currentComponentPose.positions;
    return std::max(std::fabs(kb::math::Length(positions[kLowerIndex] - positions[kUpperIndex]) - 1.0F),
        std::fabs(kb::math::Length(positions[kHandIndex] - positions[kLowerIndex]) - 1.0F));
}

void TestSpringBonesReturnToPose() {
    RuleFixture fixture;
    float previousReturnSeconds = 1.0e9F;
    for (const float stiffness : { 25.0F, 100.0F, 400.0F }) {
        // Damping grows with stiffness so every chain settles the same way, only faster.
        const kb::scene::SceneEntity owner = fixture.Owner(
            fixture.Register("Return" + std::to_string(static_cast<int>(stiffness)),
                SpringController(SpringBoneSettings{ .stiffness = stiffness, .damping = 2.0F * std::sqrt(stiffness) })),
            kb::scene::MotionSkeletonRuleComponent{});
        fixture.Update(0.0F);
        Require(TipLag(fixture.Pose(owner)) < 1.0e-4F, "A spring chain must start on the animated pose");

        // A shove of one metre leaves the tip behind; it comes back sooner the stiffer the chain is.
        fixture.Move(owner, Vec3{ 1.0F, 0.0F, 0.0F });
        fixture.Update(kFrame);
        Require(TipLag(fixture.Pose(owner)) > 0.05F, "A shove of the character must leave the tip of a spring chain behind");
        float returnSeconds = 0.0F;
        float worstLength = BoneLengthError(fixture.Pose(owner));
        for (int frame = 1; frame <= 120; ++frame) {
            fixture.Update(kFrame);
            const kb::scene::AnimatorInstanceSkeletonView pose = fixture.Pose(owner);
            worstLength = std::max(worstLength, BoneLengthError(pose));
            if (returnSeconds == 0.0F && TipLag(pose) < 0.02F) returnSeconds = static_cast<float>(frame) * kFrame;
        }
        Require(returnSeconds > 0.0F && returnSeconds < previousReturnSeconds, "A stiffer spring chain must come back sooner");
        previousReturnSeconds = returnSeconds;
        Require(TipLag(fixture.Pose(owner)) < 1.0e-3F, "A spring chain of a motionless character must return to the animated pose within 2 seconds");
        Require(worstLength < 1.0e-4F, "A spring chain must keep its bone lengths");
    }
}

void TestSpringBonesFrameRate() {
    std::array<Vec3, 3> tips{};
    std::size_t slot = 0U;
    for (const int rate : { 30, 60, 144 }) {
        RuleFixture fixture;
        const kb::scene::SceneEntity owner = fixture.Owner(
            fixture.Register("Rate", SpringController(SpringBoneSettings{ .stiffness = 60.0F, .damping = 6.0F })),
            kb::scene::MotionSkeletonRuleComponent{});
        fixture.Update(0.0F);
        // The character walks along +X at 1 m/s for a second.
        for (int frame = 1; frame <= rate; ++frame) {
            fixture.Move(owner, Vec3{ static_cast<float>(frame) / static_cast<float>(rate), 0.0F, 0.0F });
            fixture.Update(1.0F / static_cast<float>(rate));
        }
        tips[slot++] = fixture.Pose(owner).currentComponentPose.positions[kHandIndex];
    }
    Require(kb::math::Length(tips[0] - Vec3{ 0.0F, 3.0F, 0.0F }) > 0.02F, "The tip of a walking character's spring chain must trail behind");
    Require(kb::math::Length(tips[0] - tips[1]) < 0.01F && kb::math::Length(tips[0] - tips[2]) < 0.01F &&
            kb::math::Length(tips[1] - tips[2]) < 0.01F,
        "A spring chain must end the same place at 30, 60 and 144 frames per second");
}

void TestSpringBonesColliders() {
    RuleFixture fixture;
    const SpringBoneSettings loose{ .stiffness = 20.0F, .damping = 5.0F, .collisionRadius = 0.05F };
    SpringBoneSettings guarded = loose;
    const Vec3 centre{ -0.2F, 3.0F, 0.0F };
    guarded.colliders = { SpringBoneCollider{ .boneId = kRoot, .shape = SpringColliderShape::Sphere, .offset = centre, .radius = 0.1F } };
    const kb::scene::SceneEntity free = fixture.Owner(fixture.Register("Loose", SpringController(loose)), kb::scene::MotionSkeletonRuleComponent{});
    const kb::scene::SceneEntity blocked = fixture.Owner(fixture.Register("Guarded", SpringController(guarded)), kb::scene::MotionSkeletonRuleComponent{});
    fixture.Update(0.0F);

    // The sphere sits where the chain trails to while the character walks along +X.
    const float reach = 0.1F + 0.05F;
    float freeDepth = 0.0F;
    float blockedDepth = 0.0F;
    float worstLength = 0.0F;
    for (int frame = 1; frame <= 120; ++frame) {
        for (const kb::scene::SceneEntity owner : { free, blocked }) {
            fixture.Move(owner, Vec3{ static_cast<float>(frame) * kFrame, 0.0F, 0.0F });
        }
        fixture.Update(kFrame);
        for (const std::size_t joint : { kLowerIndex, kHandIndex }) {
            freeDepth = std::max(freeDepth, reach - kb::math::Length(fixture.Pose(free).currentComponentPose.positions[joint] - centre));
            blockedDepth = std::max(blockedDepth, reach - kb::math::Length(fixture.Pose(blocked).currentComponentPose.positions[joint] - centre));
        }
        worstLength = std::max(worstLength, BoneLengthError(fixture.Pose(blocked)));
    }
    Require(freeDepth > 0.05F, "The unguarded chain must run into the sphere, or the test proves nothing");
    Require(blockedDepth < 1.0e-3F, "No joint of a spring chain may enter a collider deeper than a millimetre");
    Require(worstLength < 1.0e-4F, "Colliding must not change a spring chain's bone lengths");
}

void TestSpringBonesTeleport() {
    RuleFixture fixture;
    const kb::scene::SceneEntity owner = fixture.Owner(
        fixture.Register("Teleport", SpringController(SpringBoneSettings{})), kb::scene::MotionSkeletonRuleComponent{});
    fixture.Update(0.0F);
    fixture.Move(owner, Vec3{ 1.0F, 0.0F, 0.0F });
    fixture.Update(kFrame);
    Require(TipLag(fixture.Pose(owner)) > 0.05F, "A shove below the teleport distance must swing the chain");

    // A jump past the distance puts the chain straight back on the animation, with no swing to follow.
    fixture.Move(owner, Vec3{ 101.0F, 0.0F, 0.0F });
    fixture.Update(kFrame);
    Require(TipLag(fixture.Pose(owner)) < 1.0e-4F, "A teleport must reset the spring chain onto the animated pose");
    fixture.Update(kFrame);
    Require(TipLag(fixture.Pose(owner)) < 1.0e-4F, "A reset spring chain must stay at rest");
}

void TestSpringBonesLimitAndWeight() {
    RuleFixture fixture;
    const auto spawn = [&fixture](std::string_view name, SpringBoneSettings settings, float weight) {
        return fixture.Owner(fixture.Register(name, SpringController(std::move(settings), weight)), kb::scene::MotionSkeletonRuleComponent{});
    };
    const kb::scene::SceneEntity limited = spawn("Limited", SpringBoneSettings{ .stiffness = 20.0F, .damping = 5.0F, .maxAngleDegrees = 10.0F }, 1.0F);
    const kb::scene::SceneEntity half = spawn("Half", SpringBoneSettings{ .stiffness = 20.0F, .damping = 5.0F }, 0.5F);
    const kb::scene::SceneEntity full = spawn("Full", SpringBoneSettings{ .stiffness = 20.0F, .damping = 5.0F }, 1.0F);
    fixture.Update(0.0F);
    float limitedAngle = 0.0F;
    for (int frame = 1; frame <= 30; ++frame) {
        for (const kb::scene::SceneEntity owner : { limited, half, full }) {
            fixture.Move(owner, Vec3{ static_cast<float>(frame) * kFrame * 2.0F, 0.0F, 0.0F });
        }
        fixture.Update(kFrame);
        // Both bones of the chain keep their segments within the limit of the animated direction (+Y).
        const std::span<const Vec3> positions = fixture.Pose(limited).currentComponentPose.positions;
        const Vec3 upper = kb::math::Normalize(positions[kLowerIndex] - positions[kUpperIndex]);
        const Vec3 lower = kb::math::Normalize(positions[kHandIndex] - positions[kLowerIndex]);
        limitedAngle = std::max({ limitedAngle, std::acos(std::min(upper.y, 1.0F)), std::acos(std::min(lower.y, 1.0F)) });
    }
    Require(limitedAngle <= 10.0F * kb::math::kPi / 180.0F + 1.0e-3F && limitedAngle > 0.1F,
        "A spring chain's bones must stay within the angle limit and use it");
    const float fullLag = TipLag(fixture.Pose(full));
    const float halfLag = TipLag(fixture.Pose(half));
    Require(fullLag > 0.05F && halfLag > 0.2F * fullLag && halfLag < 0.8F * fullLag,
        "A half weight must leave a spring chain about half way between the animation and the simulation");
}

void TestSpringBonesController() {
    RuleFixture fixture;
    SpringBoneSettings settings{ .stiffness = 80.5F, .damping = 3.25F, .gravity = 9.81F, .movementInfluence = 0.5F,
        .collisionRadius = 0.02F, .maxAngleDegrees = 40.0F, .teleportDistance = 2.5F };
    settings.colliders = {
        SpringBoneCollider{ .boneId = kRoot, .shape = SpringColliderShape::Sphere, .offset = { 0.1F, 0.2F, 0.3F }, .radius = 0.25F },
        SpringBoneCollider{ .boneId = kTwist, .shape = SpringColliderShape::Capsule, .offset = { 0.0F, 0.1F, 0.0F },
            .tailOffset = { 0.0F, 0.6F, 0.0F }, .radius = 0.05F },
    };
    const std::filesystem::path saved = fixture.root / "Assets" / "Rig" / "SpringSaved.kbanimcontroller";
    Require(kb::scene::AnimationAssetIO::SaveController(saved, SpringController(settings, 0.75F)), "A spring bones controller must save");
    const auto loaded = kb::scene::AnimationAssetIO::LoadController(saved);
    Require(loaded.has_value() && loaded->rigConstraints.size() == 1U, "A spring bones controller must load");
    const kb::scene::AnimatorRigConstraint& constraint = loaded->rigConstraints[0];
    const SpringBoneSettings& back = constraint.spring;
    Require(constraint.type == kb::scene::AnimatorRigConstraintType::SpringBones && constraint.name == "Hair" &&
            constraint.constrainedBoneId == kUpper && constraint.tipBoneId == kHand && constraint.midBoneId == 0U &&
            constraint.target.empty() && constraint.poleTarget.empty() && constraint.weight == 0.75F,
        "The spring bones chain and weight must survive a save and a load");
    Require(back.stiffness == 80.5F && back.damping == 3.25F && back.gravity == 9.81F && back.movementInfluence == 0.5F &&
            back.collisionRadius == 0.02F && back.maxAngleDegrees == 40.0F && back.teleportDistance == 2.5F,
        "The spring bones settings must survive a save and a load");
    Require(back.colliders.size() == 2U && back.colliders[0].boneId == kRoot && back.colliders[0].shape == SpringColliderShape::Sphere &&
            back.colliders[0].offset.x == 0.1F && back.colliders[0].offset.y == 0.2F && back.colliders[0].offset.z == 0.3F &&
            back.colliders[0].radius == 0.25F && back.colliders[1].boneId == kTwist &&
            back.colliders[1].shape == SpringColliderShape::Capsule && back.colliders[1].offset.y == 0.1F &&
            back.colliders[1].tailOffset.y == 0.6F && back.colliders[1].radius == 0.05F,
        "The spring bones colliders must survive a save and a load");

    // Invalid settings are refused.
    const auto refused = [&fixture](SpringBoneSettings bad) {
        return !kb::scene::AnimationAssetIO::SaveController(fixture.root / "Assets" / "Rig" / "Bad.kbanimcontroller", SpringController(std::move(bad)));
    };
    SpringBoneSettings negative;
    negative.stiffness = -1.0F;
    SpringBoneSettings influence;
    influence.movementInfluence = 1.5F;
    SpringBoneSettings angle;
    angle.maxAngleDegrees = 200.0F;
    SpringBoneSettings flat;
    flat.colliders = { SpringBoneCollider{ .boneId = kRoot, .radius = 0.0F } };
    SpringBoneSettings loose;
    loose.colliders = { SpringBoneCollider{ .boneId = 0U } };
    Require(refused(negative) && refused(influence) && refused(angle) && refused(flat) && refused(loose),
        "Out-of-range spring bones settings must be refused");
    kb::scene::AnimatorController targeted = SpringController(SpringBoneSettings{});
    targeted.rigConstraints[0].target = "Reach";
    Require(!kb::scene::AnimationAssetIO::SaveController(fixture.root / "Assets" / "Rig" / "Bad.kbanimcontroller", targeted),
        "Spring bones take no target");

    // The controller records schema 6; a file of schema 5 still loads, but cannot hold a spring record.
    const auto rewritten = [](const std::filesystem::path& from, const std::filesystem::path& to, int schema) {
        std::ifstream input(from, std::ios::binary);
        const std::string text{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
        const std::size_t headerEnd = text.find('\n');
        Require(headerEnd != std::string::npos && text.starts_with("21kb AnimatorController 6"), "A controller must open with its schema header");
        std::ofstream output(to, std::ios::binary);
        output << "21kb AnimatorController " << schema << text.substr(headerEnd);
    };
    kb::scene::AnimatorController chained{};
    chained.layers = { { .name = "Base", .defaultState = "Pose", .states = { { .name = "Pose", .clipReference = "/Game/Rig/Static.kbanim" } } } };
    chained.rigConstraints.push_back({ .name = "Reach", .type = kb::scene::AnimatorRigConstraintType::ChainIK, .target = "Reach",
        .constrainedBoneId = kRoot, .tipBoneId = kHand });
    const std::filesystem::path chainedPath = fixture.root / "Assets" / "Rig" / "Chained.kbanimcontroller";
    const std::filesystem::path oldChained = fixture.root / "Assets" / "Rig" / "OldChained.kbanimcontroller";
    const std::filesystem::path oldSpring = fixture.root / "Assets" / "Rig" / "OldSpring.kbanimcontroller";
    Require(kb::scene::AnimationAssetIO::SaveController(chainedPath, chained), "A ChainIK controller must save");
    rewritten(chainedPath, oldChained, 5);
    rewritten(saved, oldSpring, 5);
    const auto old = kb::scene::AnimationAssetIO::LoadController(oldChained);
    Require(old.has_value() && old->rigConstraints.size() == 1U &&
            old->rigConstraints[0].type == kb::scene::AnimatorRigConstraintType::ChainIK,
        "A controller saved before spring bones existed must still load");
    Require(!kb::scene::AnimationAssetIO::LoadController(oldSpring).has_value(),
        "A spring bones record must not load from a file of an older schema");
}

// The chain simulation steps a thousand chains of four joints without touching the heap, and an animator
// holding a thousand such chains does not allocate when it updates.
void TestSpringBonesDoNotAllocate() {
    using kb::scene::SpringCapsule;
    constexpr std::size_t kChains = 1000U;
    std::vector<kb::scene::SpringChainState> chains(kChains);
    for (kb::scene::SpringChainState& chain : chains) kb::scene::PrepareSpringChain(chain, 4U);
    const std::array<Vec3, 4> targets{ Vec3{ 0.0F, 0.0F, 0.0F }, Vec3{ 0.0F, 1.0F, 0.0F }, Vec3{ 0.0F, 2.0F, 0.0F }, Vec3{ 0.0F, 3.0F, 0.0F } };
    SpringBoneSettings settings{ .stiffness = 30.0F, .damping = 4.0F, .gravity = 9.81F, .maxAngleDegrees = 60.0F };
    const std::array<SpringCapsule, 1> colliders{ SpringCapsule{ Vec3{ -0.4F, 1.5F, 0.0F }, Vec3{ -0.4F, 3.0F, 0.0F }, 0.3F } };
    kb::math::DVec3 origin{};
    for (kb::scene::SpringChainState& chain : chains) kb::scene::StepSpringChain(chain, targets, origin, Quat{}, 0.0, settings, colliders);
    BeginAllocationTally();
    for (int frame = 0; frame < 10; ++frame) {
        origin.x += 0.05;
        for (kb::scene::SpringChainState& chain : chains) {
            kb::scene::StepSpringChain(chain, targets, origin, Quat{}, 1.0 / 60.0, settings, colliders);
        }
    }
    const AllocationTally stepped = EndAllocationTally();
    Require(stepped.count == 0U, "Stepping spring chains must not allocate");
    float worst = 0.0F;
    float swing = 0.0F;
    for (const kb::scene::SpringChainState& chain : chains) {
        for (std::size_t joint = 0U; joint + 1U < 4U; ++joint) {
            worst = std::max(worst, std::fabs(kb::math::Length(chain.position[joint + 1U] - chain.position[joint]) - 1.0F));
        }
        swing = std::max(swing, kb::math::Length(chain.position[3] - targets[3]));
    }
    Require(worst < 1.0e-4F && swing > 0.1F, "Every chain of the batch must swing and keep its bone lengths");

    // The same chains on one skeleton, updated by the scene.
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb-spring-bones-allocation-tests";
    std::filesystem::remove_all(root);
    kb::scene::SkeletonAsset skeleton{};
    skeleton.bones.push_back({ .id = 1U, .parentIndex = -1, .name = "Root", .referencePose = {}, .inverseBind = {} });
    for (std::size_t chain = 0U; chain < kChains; ++chain) {
        for (std::size_t joint = 0U; joint < 4U; ++joint) {
            const std::size_t index = skeleton.bones.size();
            skeleton.bones.push_back({ .id = 10U + index, .parentIndex = joint == 0U ? 0 : static_cast<std::int32_t>(index - 1U),
                .name = "Bone" + std::to_string(index), .referencePose = { .position = { joint == 0U ? 0.01F * static_cast<float>(chain) : 0.0F, 1.0F, 0.0F } },
                .inverseBind = {} });
        }
    }
    kb::scene::Scene scene;
    Require(kb::scene::SkeletonAssetIO::Save(root / "Assets" / "Rig" / "Hair.kbskeleton", skeleton) &&
            scene.Assets().MountProject(root) && scene.Assets().Discover() == 1U,
        "The spring bones allocation test could not save its skeleton");
    const std::uint64_t skeletonId = scene.Assets().Manager().Registry().FindByPath("/Game/Rig/Hair.kbskeleton")->id.value;
    const std::uint64_t signature = kb::scene::SkeletonCompatibilitySignature(skeleton);
    kb::scene::AnimationClip clip{ .durationSeconds = 1.0F, .looping = true };
    clip.targetSkeletonAssetId = skeletonId;
    clip.targetSkeletonCompatibilitySignature = signature;
    const kb::scene::AnimationBoneKeyframe key{ .timeSeconds = 0.0F, .transform = {} };
    clip.skeletalTracks = { { .boneId = 1U, .keyframes = { key, kb::scene::AnimationBoneKeyframe{ .timeSeconds = 1.0F, .transform = {} } } } };
    kb::scene::AnimatorController plain{};
    plain.layers = { { .name = "Base", .defaultState = "Pose", .states = { { .name = "Pose", .clipReference = "/Game/Rig/Hair.kbanim" } } } };
    kb::scene::AnimatorController sprung = plain;
    for (std::size_t chain = 0U; chain < kChains; ++chain) {
        sprung.rigConstraints.push_back({ .name = "Chain" + std::to_string(chain), .type = kb::scene::AnimatorRigConstraintType::SpringBones,
            .constrainedBoneId = skeleton.bones[1U + chain * 4U].id, .tipBoneId = skeleton.bones[4U + chain * 4U].id,
            .spring = SpringBoneSettings{ .stiffness = 30.0F, .damping = 4.0F, .gravity = 9.81F } });
    }
    Require(kb::scene::AnimationAssetIO::SaveClip(root / "Assets" / "Rig" / "Hair.kbanim", clip) &&
            kb::scene::AnimationAssetIO::SaveController(root / "Assets" / "Rig" / "Plain.kbanimcontroller", plain) &&
            kb::scene::AnimationAssetIO::SaveController(root / "Assets" / "Rig" / "Sprung.kbanimcontroller", sprung),
        "The spring bones allocation test could not save its animation");
    static_cast<void>(scene.Assets().Discover());
    const auto update = [&scene](float seconds) {
        static_cast<void>(scene.Runtime().Update(seconds));
        const std::vector<std::string> errors = scene.Runtime().DrainSceneSystemErrors();
        Require(errors.empty(), errors.empty() ? "" : errors.front().c_str());
    };
    // Heap allocations of ten updates of a walking character, and how far the last chain's tip trails.
    const auto measure = [&](std::string_view controllerPath) {
        const std::uint64_t controllerId = scene.Assets().Manager().Registry().FindByPath(controllerPath)->id.value;
        const kb::scene::SceneObject object = scene.Entities().CreateObject({ .name = "Hair" });
        Require(scene.Components().SkeletonBindings().Set(object.Entity(), kb::scene::SkeletonBindingComponent{
                    .skeletonAssetId = skeletonId, .skeletonCompatibilitySignature = signature, .enabled = true }),
            "The spring bones allocation test could not bind its skeleton");
        scene.Components().Animators().Set(object.Entity(), kb::scene::Animator{ .controllerAssetId = controllerId, .speed = 1.0F, .enabled = true });
        update(0.0F);
        Require(scene.Animators().Exists(object.Entity()), "The spring bones animator must bind its chains");
        std::size_t allocations = 0U;
        for (int frame = 1; frame <= 13; ++frame) {
            scene.Transforms().Set(object.Entity(), kb::scene::TransformComponent{ .localPosition = { 0.05F * static_cast<float>(frame), 0.0F, 0.0F } });
            BeginAllocationTally();
            static_cast<void>(scene.Runtime().Update(kFrame));
            const AllocationTally tally = EndAllocationTally();
            if (frame > 3) allocations += tally.count;
        }
        const std::optional<kb::scene::AnimatorInstanceSkeletonView> pose = scene.Animators().InstanceSkeleton(object.Entity());
        Require(pose.has_value(), "The spring bones animator has no pose");
        const float lag = kb::math::Length(pose->currentComponentPose.positions[skeleton.bones.size() - 1U] -
            Vec3{ 0.0F, 4.0F, 0.0F } - Vec3{ 0.01F * static_cast<float>(kChains - 1U), 0.0F, 0.0F });
        scene.Entities().Destroy(object.Entity());
        return std::pair{ allocations, lag };
    };
    const auto [plainAllocations, plainLag] = measure("/Game/Rig/Plain.kbanimcontroller");
    const auto [springAllocations, springLag] = measure("/Game/Rig/Sprung.kbanimcontroller");
    Require(plainLag < 1.0e-4F && springLag > 0.05F, "The thousand spring chains must trail behind the moving character");
    Require(springAllocations <= plainAllocations, "A thousand spring chains must not make an animator allocate when it updates");
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

} // namespace

void RunMotionSkeletonRuleTests() {
    TestTargetRules();
    TestLocalRules();
    TestSpringRule();
    TestChainIkSolver();
    TestChainIkConstraint();
    TestSpringBonesReturnToPose();
    TestSpringBonesFrameRate();
    TestSpringBonesColliders();
    TestSpringBonesTeleport();
    TestSpringBonesLimitAndWeight();
    TestSpringBonesController();
    TestSpringBonesDoNotAllocate();
}

} // namespace kb::tests
