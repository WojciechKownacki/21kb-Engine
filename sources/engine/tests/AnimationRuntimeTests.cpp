#include "TestSupport.hpp"

#include "engine/scene/AnimationAssetIO.hpp"
#include "engine/scene/AnimationRetargeting.hpp"
#include "engine/gameplay/AbilityAsset.hpp"
#include "engine/scene/SceneGameplay.hpp"
#include "engine/math/EngineMath.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/project/ProjectDescriptor.hpp"
#include "engine/scene/CharacterControllerComponent.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/DrawD3DeformedGeometryComponent.hpp"
#include "engine/scene/RigidbodyComponent.hpp"
#include "engine/scene/PhysicsBackend.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAnimators.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabInstance.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/SkeletonAssetIO.hpp"
#include "engine/scene/SkeletonBindingComponent.hpp"
#include "engine/scene/SocketAttachmentComponent.hpp"
#include "engine/script/PucLuaScriptRuntime.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <algorithm>
#include <cmath>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <thread>

#if !defined(KB_PHYSICS_JOLT_PLUGIN_PATH)
#define KB_PHYSICS_JOLT_PLUGIN_PATH ""
#endif

namespace kb::tests {
namespace {

[[nodiscard]] std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    return { std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
}

void WriteTextFile(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
}

} // namespace

// The state machine a character needs: locomotion blended from clips of different lengths, a
// hit reaction from any state, and a one-shot that returns when it ends with no condition.
void RunCharacterStateMachineTest(const std::filesystem::path& root) {
    std::filesystem::create_directories(root / "Assets" / "Animation");
    const auto clipMoving = [](float length, float distance, bool looping) {
        kb::scene::AnimationClip clip{};
        clip.durationSeconds = length;
        clip.looping = looping;
        clip.tracks = { kb::scene::AnimationTransformTrack{ .targetPath = "", .keyframes = {
            { .timeSeconds = 0.0F, .transform = {} },
            { .timeSeconds = length, .transform = { .position = { distance, 0.0F, 0.0F } } },
        } } };
        return clip;
    };
    Require(kb::scene::AnimationAssetIO::SaveClip(root / "Assets/Animation/Idle.kbanim", clipMoving(2.0F, 0.0F, true)) &&
            kb::scene::AnimationAssetIO::SaveClip(root / "Assets/Animation/Walk.kbanim", clipMoving(1.0F, 10.0F, true)) &&
            kb::scene::AnimationAssetIO::SaveClip(root / "Assets/Animation/Run.kbanim", clipMoving(0.5F, 10.0F, true)) &&
            kb::scene::AnimationAssetIO::SaveClip(root / "Assets/Animation/Hit.kbanim", clipMoving(0.25F, -1.0F, false)),
        "State machine test clips could not be saved");
    kb::scene::AnimatorController controller{};
    controller.parameters = {
        { .name = "Speed", .type = kb::scene::AnimatorParameterType::Float },
        { .name = "Hit", .type = kb::scene::AnimatorParameterType::Trigger },
    };
    controller.layers = { { .name = "Base", .defaultState = "Locomotion", .states = {
        { .id = 1U, .name = "Locomotion", .blendParameter = "Speed", .blendChildren = {
            { .threshold = 0.0F, .clipReference = "/Game/Animation/Walk.kbanim" },
            { .threshold = 1.0F, .clipReference = "/Game/Animation/Run.kbanim" },
        } },
        { .id = 2U, .name = "Hit", .clipReference = "/Game/Animation/Hit.kbanim" },
    }, .transitions = {
        { .id = 3U, .fromState = std::string{ kb::scene::kAnimatorAnyState }, .toState = "Hit", .durationSeconds = 0.0F,
          .conditions = { { .parameter = "Hit", .mode = kb::scene::AnimatorConditionMode::TriggerSet } } },
        { .id = 4U, .fromState = "Hit", .toState = "Locomotion", .durationSeconds = 0.0F, .exitNormalizedTime = 1.0F },
    } } };
    std::string error;
    Require(kb::scene::AnimationAssetIO::ValidateController(controller, &error),
        ("A controller with an any-state and an exit-time transition was refused: " + error).c_str());
    kb::scene::AnimatorController conditionless = controller;
    conditionless.layers[0].transitions[1].exitNormalizedTime = -1.0F;
    Require(!kb::scene::AnimationAssetIO::ValidateController(conditionless),
        "A transition with neither a condition nor an exit time was accepted");
    Require(kb::scene::AnimationAssetIO::SaveController(root / "Assets/Animation/Hero.kbanimcontroller", controller),
        "State machine test controller could not be saved");

    kb::scene::Scene scene;
    Require(scene.Assets().MountProject(root) && scene.Assets().Discover() >= 5U, "State machine test project could not be mounted");
    const kb::assets::AssetMetadata* metadata =
        scene.Assets().Manager().Registry().FindByPath("/Game/Animation/Hero.kbanimcontroller");
    Require(metadata != nullptr, "State machine test controller was not discovered");
    const kb::scene::SceneObject hero = scene.Entities().CreateObject({ .name = "Hero" });
    scene.Components().Animators().Set(hero.Entity(), kb::scene::Animator{ .controllerAssetId = metadata->id.value, .enabled = true });
    scene.Runtime().SetPlaying(true);
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(scene.Animators().Exists(hero.Entity()), "A blend of clips of different lengths was refused at binding");

    // Halfway between a 1 s walk and a 0.5 s run the cycle lasts 0.75 s: after 0.375 s both clips
    // are halfway through their own lengths.
    Require(scene.Animators().SetFloat(hero.Entity(), "Speed", 0.5F), "Speed could not be set");
    static_cast<void>(scene.Runtime().Update(0.375F));
    const auto locomotion = scene.Animators().State(hero.Entity(), "Base");
    Require(locomotion.has_value() && locomotion->state == "Locomotion" && std::abs(locomotion->normalizedTime - 0.5F) < 0.001F &&
            std::abs(scene.Transforms().Get(hero.Entity()).localPosition.x - 5.0F) < 0.001F,
        "A blend of clips of different lengths did not play them at one phase and the blended length");

    Require(scene.Animators().SetTrigger(hero.Entity(), "Hit"), "Hit could not be triggered");
    static_cast<void>(scene.Runtime().Update(0.01F));
    const auto hit = scene.Animators().State(hero.Entity(), "Base");
    Require(hit.has_value() && hit->state == "Hit", "An any-state transition did not leave the active state");
    static_cast<void>(scene.Runtime().Update(0.3F));
    static_cast<void>(scene.Runtime().Update(0.01F));
    const auto recovered = scene.Animators().State(hero.Entity(), "Base");
    Require(recovered.has_value() && recovered->state == "Locomotion",
        "A one-shot did not return through its exit-time transition");
}

// A sword on a socket and a shield on a bone (named the way another tool writes it), the shield
// through a scaled holder: both follow the animated hand, keep their own size, and a socket that
// does not exist is reported, not guessed.
void RunSocketAttachmentTest(const std::filesystem::path& root) {
    std::filesystem::create_directories(root / "Assets" / "Rig");
    std::filesystem::create_directories(root / "Assets" / "Animation");
    const kb::math::Mat4 identity{ .columns = {
        { 1.0F, 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F, 0.0F }, { 0.0F, 0.0F, 1.0F, 0.0F }, { 0.0F, 0.0F, 0.0F, 1.0F } } };
    kb::scene::SkeletonAsset skeleton{};
    skeleton.bones = {
        { .id = 1U, .parentIndex = -1, .name = "Root", .referencePose = {}, .inverseBind = identity },
        { .id = 2U, .parentIndex = 0, .name = "hand.R", .referencePose = { .position = { 1.0F, 0.0F, 0.0F } }, .inverseBind = { .columns = {
            { 1.0F, 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F, 0.0F }, { 0.0F, 0.0F, 1.0F, 0.0F }, { -1.0F, 0.0F, 0.0F, 1.0F } } } },
    };
    skeleton.sockets = { { .name = "weapon_r", .boneId = 2U, .localTransform = { .position = { 0.0F, 0.5F, 0.0F } } } };
    const std::uint64_t signature = kb::scene::SkeletonCompatibilitySignature(skeleton);
    Require(signature != 0U && kb::scene::SkeletonAssetIO::Save(root / "Assets/Rig/Hero.kbskeleton", skeleton),
        "Socket test skeleton could not be saved");
    std::string skeletonError;
    Require(kb::scene::SkeletonAssetIO::Load(root / "Assets/Rig/Hero.kbskeleton", &skeletonError).has_value(),
        ("Socket test skeleton could not be read back: " + skeletonError).c_str());

    kb::scene::Scene scene;
    Require(scene.Assets().MountProject(root) && scene.Assets().Discover() == 1U, "Socket test project could not be mounted");
    const kb::assets::AssetMetadata* skeletonMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Rig/Hero.kbskeleton");
    Require(skeletonMetadata != nullptr, "Socket test skeleton was not discovered");
    // Discover() below may move the registry's records: keep the id, not the record.
    const std::uint64_t skeletonId = skeletonMetadata->id.value;

    // The hand rises from y=0 to y=2 over one second.
    kb::scene::AnimationClip clip{};
    clip.durationSeconds = 1.0F;
    clip.looping = true;
    clip.targetSkeletonAssetId = skeletonId;
    clip.targetSkeletonCompatibilitySignature = signature;
    clip.skeletalTracks = { { .boneId = 2U, .bindingMask = 1U, .keyframes = {
        { .timeSeconds = 0.0F, .transform = { .position = { 1.0F, 0.0F, 0.0F } } },
        { .timeSeconds = 1.0F, .transform = { .position = { 1.0F, 2.0F, 0.0F } } },
    } } };
    kb::scene::AnimatorController controller{};
    controller.layers = { { .name = "Base", .defaultState = "Raise", .states = {
        { .id = 1U, .name = "Raise", .clipReference = "/Game/Animation/Raise.kbanim" } } } };
    Require(kb::scene::AnimationAssetIO::SaveClip(root / "Assets/Animation/Raise.kbanim", clip) &&
            kb::scene::AnimationAssetIO::SaveController(root / "Assets/Animation/Hero.kbanimcontroller", controller) &&
            scene.Assets().Discover() == 3U,
        "Socket test clip and controller could not be saved");
    const kb::assets::AssetMetadata* controllerMetadata =
        scene.Assets().Manager().Registry().FindByPath("/Game/Animation/Hero.kbanimcontroller");
    Require(controllerMetadata != nullptr, "Socket test controller was not discovered");

    const kb::scene::SceneObject hero = scene.Entities().CreateObject({ .name = "Hero" });
    scene.Transforms().Set(hero.Entity(), kb::scene::TransformComponent{ .localPosition = { 10.0F, 0.0F, 0.0F } });
    Require(scene.Components().SkeletonBindings().Set(hero.Entity(), kb::scene::SkeletonBindingComponent{
                .skeletonAssetId = skeletonId, .skeletonCompatibilitySignature = signature, .enabled = true }),
        "Socket test character could not bind its skeleton");
    scene.Components().Animators().Set(hero.Entity(), kb::scene::Animator{ .controllerAssetId = controllerMetadata->id.value, .enabled = true });

    const auto child = [&scene](const char* name, kb::scene::SceneObject parent, kb::scene::Vec3 position, kb::scene::Vec3 scale) {
        const kb::scene::SceneObject object = scene.Entities().CreateObject({ .name = name, .parent = parent });
        scene.Transforms().Set(object.Entity(), kb::scene::TransformComponent{ .localPosition = position, .localScale = scale });
        return object;
    };
    const kb::scene::SceneObject sword = child("Sword", hero, {}, { 0.1F, 2.0F, 0.1F });
    Require(scene.Components().SocketAttachments().Set(sword.Entity(), kb::scene::MakeSocketAttachment("weapon_r", { 0.0F, 0.0F, 1.0F })),
        "Socket test sword could not be attached");
    const kb::scene::SceneObject holder = child("Holder", hero, { 0.0F, 5.0F, 0.0F }, { 2.0F, 2.0F, 2.0F });
    const kb::scene::SceneObject shield = child("Shield", holder, {}, { 1.0F, 1.0F, 1.0F });
    Require(scene.Components().SocketAttachments().Set(shield.Entity(), kb::scene::MakeSocketAttachment("Hand_R")),
        "Socket test shield could not be attached");
    const kb::scene::SceneObject lost = child("Lost", hero, { 7.0F, 7.0F, 7.0F }, { 1.0F, 1.0F, 1.0F });
    Require(scene.Components().SocketAttachments().Set(lost.Entity(), kb::scene::MakeSocketAttachment("tail")),
        "Socket test bad attachment could not be authored");
    const kb::scene::SceneObject parked = child("Parked", hero, { 3.0F, 3.0F, 3.0F }, { 1.0F, 1.0F, 1.0F });
    kb::scene::SocketAttachmentComponent disabled = kb::scene::MakeSocketAttachment("weapon_r");
    disabled.enabled = false;
    Require(scene.Components().SocketAttachments().Set(parked.Entity(), disabled), "Socket test disabled attachment could not be authored");

    // Before anything is posed (the editor), names are judged against the authored skeleton.
    const kb::scene::SocketAttachmentStatus beforePlay = scene.Animators().SocketAttachment(shield.Entity());
    Require(beforePlay.attached && beforePlay.resolvedName == "hand.R" && beforePlay.character == hero.Entity() &&
            scene.Animators().SocketNames(hero.Entity()).size() == 3U &&
            !scene.Animators().SocketAttachment(lost.Entity()).attached,
        "Socket attachments must be judged against the authored skeleton before Play");

    scene.Runtime().SetPlaying(true);
    static_cast<void>(scene.Runtime().Update(0.0F));
    static_cast<void>(scene.Runtime().Update(0.5F));
    const std::vector<std::string> errors = scene.Runtime().DrainSceneSystemErrors();
    Require(errors.empty(), ("Socket attachments produced scene system errors: " + (errors.empty() ? std::string{} : errors.front())).c_str());

    const auto near = [](kb::scene::Vec3 actual, kb::scene::Vec3 expected) {
        return std::abs(actual.x - expected.x) < 0.001F && std::abs(actual.y - expected.y) < 0.001F && std::abs(actual.z - expected.z) < 0.001F;
    };
    // Hand at (1,1,0) halfway up, socket 0.5 above it, offset 1 forward - in the hero's space.
    const kb::scene::TransformComponent swordTransform = scene.Transforms().Get(sword.Entity());
    Require(near(swordTransform.localPosition, { 1.0F, 1.5F, 1.0F }) && near(swordTransform.localScale, { 0.1F, 2.0F, 0.1F }),
        "A socket attachment did not follow its animated socket, or lost its own scale");
    // The bone itself, seen from a holder 5 up and twice as large.
    const kb::scene::TransformComponent shieldTransform = scene.Transforms().Get(shield.Entity());
    Require(near(shieldTransform.localPosition, { 0.5F, -2.0F, 0.0F }),
        "A bone attachment named another way did not resolve, or ignored the objects between it and the character");
    const kb::scene::SocketAttachmentStatus shieldStatus = scene.Animators().SocketAttachment(shield.Entity());
    Require(shieldStatus.attached && shieldStatus.character == hero.Entity() && shieldStatus.error.empty(),
        "A resolved attachment must report its character");

    const kb::scene::SocketAttachmentStatus lostStatus = scene.Animators().SocketAttachment(lost.Entity());
    Require(!lostStatus.attached && lostStatus.error.find("'tail'") != std::string::npos &&
            near(scene.Transforms().Get(lost.Entity()).localPosition, { 7.0F, 7.0F, 7.0F }),
        "An attachment to a missing socket must stay put and say which name is missing");
    Require(near(scene.Transforms().Get(parked.Entity()).localPosition, { 3.0F, 3.0F, 3.0F }),
        "A disabled attachment must not move its object");
    const std::vector<std::string> names = scene.Animators().SocketNames(hero.Entity());
    Require(names.size() == 3U && names[0] == "weapon_r" && names[2] == "hand.R",
        "Socket names must list sockets first, then bones");
    const auto socketPose = scene.Animators().SocketTransform(hero.Entity(), "weapon_r");
    const auto bonePose = scene.Animators().SocketTransform(hero.Entity(), "HAND_R");
    Require(socketPose.has_value() && near(socketPose->componentSpace.position, { 1.0F, 1.5F, 0.0F }) &&
            near(socketPose->worldSpace.position, { 11.0F, 1.5F, 0.0F }) &&
            bonePose.has_value() && near(bonePose->componentSpace.position, { 1.0F, 1.0F, 0.0F }),
        "SocketTransform must resolve sockets, bones and other spellings of their names");

    // Picking an object up at runtime: a wrong name changes nothing, a right one parents and places it.
    const kb::scene::SceneObject loose = scene.Entities().CreateObject({ .name = "Loose" });
    std::string attachError;
    Require(!scene.Animators().AttachToSocket(loose.Entity(), hero.Entity(), "wing", {}, &attachError) &&
            attachError.find("'wing'") != std::string::npos && !scene.Hierarchy().Parent(loose.Entity()).IsValid() &&
            !scene.Components().SocketAttachments().Has(loose.Entity()),
        "A failed AttachToSocket must leave the object as it was and say which name is missing");
    Require(scene.Animators().AttachToSocket(loose.Entity(), hero.Entity(), "hand_r", {}, &attachError) &&
            scene.Hierarchy().Parent(loose.Entity()) == hero.Entity() &&
            near(scene.Transforms().Get(loose.Entity()).localPosition, { 1.0F, 1.0F, 0.0F }),
        ("AttachToSocket must parent the object under the character and place it at once: " + attachError).c_str());
    Require(!scene.Animators().AttachToSocket(hero.Entity(), loose.Entity(), "hand_r", {}, &attachError),
        "A character must not be hung on an object below it");
    scene.Animators().DetachFromSocket(loose.Entity());
    Require(!scene.Components().SocketAttachments().Has(loose.Entity()) && scene.Hierarchy().Parent(loose.Entity()) == hero.Entity(),
        "DetachFromSocket must stop following the socket and keep the object where it is");
}

// A Mixamo-named clip on an Unreal-named character twice as tall whose arm bone points another
// way at rest: the hips travel twice as far, the arm turns the same way in the world, and a
// character with too few matching bones is refused with the reason.
void RunAnimationRetargetTest(const std::filesystem::path& root) {
    Require(kb::scene::CanonicalBoneName("mixamorig:LeftForeArm") == "l:forearm" &&
            kb::scene::CanonicalBoneName("lowerarm_l") == "l:forearm" &&
            kb::scene::CanonicalBoneName("DEF-forearm.L") == "l:forearm" &&
            kb::scene::CanonicalBoneName("Bip01 L UpperArm") == "l:upperarm" &&
            kb::scene::CanonicalBoneName("mixamorig:LeftArm") == "l:upperarm" &&
            kb::scene::CanonicalBoneName("pelvis") == "hips" && kb::scene::CanonicalBoneName("Hips") == "hips" &&
            kb::scene::CanonicalBoneName("RightUpLeg") == "r:thigh" && kb::scene::CanonicalBoneName("thigh.R") == "r:thigh" &&
            kb::scene::CanonicalBoneName("LeftToeBase") == "l:toe" &&
            kb::scene::CanonicalBoneName("spine_01") == kb::scene::CanonicalBoneName("Spine1"),
        "Bone roles must agree across Mixamo, Unreal, Rigify and 3ds Max names");

    const auto inverseBindAt = [](kb::scene::Vec3 position) {
        return kb::math::Mat4{ .columns = { { 1.0F, 0.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F, 0.0F }, { 0.0F, 0.0F, 1.0F, 0.0F },
            { -position.x, -position.y, -position.z, 1.0F } } };
    };
    const kb::scene::Quat quarterZ = kb::math::FromEulerDegrees(kb::scene::Vec3{ 0.0F, 0.0F, 90.0F });
    kb::scene::SkeletonAsset source{};
    source.bones.push_back({ .id = 11U, .parentIndex = -1, .name = "mixamorig:Hips", .referencePose = { .position = { 0.0F, 1.0F, 0.0F } },
        .inverseBind = inverseBindAt({ 0.0F, 1.0F, 0.0F }) });
    source.bones.push_back({ .id = 12U, .parentIndex = 0, .name = "mixamorig:Spine", .referencePose = { .position = { 0.0F, 0.2F, 0.0F } },
        .inverseBind = inverseBindAt({ 0.0F, 1.2F, 0.0F }) });
    source.bones.push_back({ .id = 13U, .parentIndex = 1, .name = "mixamorig:LeftArm", .referencePose = { .position = { 0.2F, 0.3F, 0.0F } },
        .inverseBind = inverseBindAt({ 0.2F, 1.5F, 0.0F }) });
    source.bones.push_back({ .id = 14U, .parentIndex = 0, .name = "mixamorig:Tail", .referencePose = { .position = { 0.0F, 0.0F, -0.2F } },
        .inverseBind = inverseBindAt({ 0.0F, 1.0F, -0.2F }) });
    kb::scene::SkeletonAsset target{};
    target.bones.push_back({ .id = 21U, .parentIndex = -1, .name = "root", .referencePose = {}, .inverseBind = inverseBindAt({}) });
    target.bones.push_back({ .id = 22U, .parentIndex = 0, .name = "pelvis", .referencePose = { .position = { 0.0F, 2.0F, 0.0F } },
        .inverseBind = inverseBindAt({ 0.0F, 2.0F, 0.0F }) });
    target.bones.push_back({ .id = 23U, .parentIndex = 1, .name = "spine_01", .referencePose = { .position = { 0.0F, 0.4F, 0.0F } },
        .inverseBind = inverseBindAt({ 0.0F, 2.4F, 0.0F }) });
    // The arm points another way at rest (a quarter turn about Z).
    target.bones.push_back({ .id = 24U, .parentIndex = 2, .name = "upperarm_l",
        .referencePose = { .position = { 0.4F, 0.6F, 0.0F }, .rotation = quarterZ }, .inverseBind = inverseBindAt({ 0.4F, 3.0F, 0.0F }) });
    const std::uint64_t sourceSignature = kb::scene::SkeletonCompatibilitySignature(source);
    const std::uint64_t targetSignature = kb::scene::SkeletonCompatibilitySignature(target);
    Require(sourceSignature != 0U && targetSignature != 0U, "Retarget test skeletons are invalid");

    // The hips walk 1 m along X while the arm turns a quarter about X, both in one second.
    const kb::scene::Quat quarterX = kb::math::FromEulerDegrees(kb::scene::Vec3{ 90.0F, 0.0F, 0.0F });
    kb::scene::AnimationClip clip{};
    clip.durationSeconds = 1.0F;
    clip.looping = true;
    clip.targetSkeletonCompatibilitySignature = sourceSignature;
    clip.skeletalTracks = {
        { .boneId = 11U, .keyframes = {
            { .timeSeconds = 0.0F, .transform = { .position = { 0.0F, 1.0F, 0.0F } } },
            { .timeSeconds = 1.0F, .transform = { .position = { 1.0F, 1.0F, 0.0F } } } } },
        { .boneId = 13U, .keyframes = {
            { .timeSeconds = 0.0F, .transform = { .position = { 0.2F, 0.3F, 0.0F } } },
            { .timeSeconds = 1.0F, .transform = { .position = { 0.2F, 0.3F, 0.0F }, .rotation = quarterX } } } },
        { .boneId = 14U, .keyframes = {
            { .timeSeconds = 0.0F, .transform = { .position = { 0.0F, 0.0F, -0.2F } } },
            { .timeSeconds = 1.0F, .transform = { .position = { 0.0F, 0.0F, -0.3F } } } } },
    };

    kb::scene::AnimationRetargetReport report;
    std::string error;
    const std::optional<kb::scene::AnimationClip> retargeted =
        kb::scene::RetargetAnimationClip(clip, source, target, 77U, targetSignature, &report, &error);
    Require(retargeted.has_value() && retargeted->targetSkeletonAssetId == 77U &&
            retargeted->targetSkeletonCompatibilitySignature == targetSignature && report.mappedBones == 3U &&
            report.unmappedAnimatedBones == std::vector<std::string>{ "mixamorig:Tail" } &&
            std::abs(report.translationScale - 2.0F) < 0.0001F,
        ("A Mixamo clip did not retarget onto an Unreal-named character: " + error).c_str());
    const auto lastKey = [&](kb::scene::SkeletonBoneId bone) -> const kb::scene::LocalTransform* {
        for (const kb::scene::AnimationBoneTrack& track : retargeted->skeletalTracks) {
            if (track.boneId == bone && !track.keyframes.empty()) return &track.keyframes.back().transform;
        }
        return nullptr;
    };
    const kb::scene::LocalTransform* pelvis = lastKey(22U);
    Require(pelvis != nullptr && std::abs(pelvis->position.x - 2.0F) < 0.001F && std::abs(pelvis->position.y - 2.0F) < 0.001F,
        "Retargeted hips must travel by the source's motion scaled to the character's hip height");
    // The arm turned a quarter about X in the world: target global = quarterX * targetRest.
    const kb::scene::LocalTransform* arm = lastKey(24U);
    const kb::scene::Quat expected = kb::math::Normalize(quarterX * quarterZ);
    const float alignment = arm == nullptr ? 0.0F : std::abs(arm->rotation.x * expected.x + arm->rotation.y * expected.y +
        arm->rotation.z * expected.z + arm->rotation.w * expected.w);
    Require(arm != nullptr && alignment > 0.9999F && std::abs(arm->position.x - 0.4F) < 0.001F,
        "A retargeted bone must turn as the source bone turned in the world and keep its own proportions");

    // Too little in common: refused, with the numbers.
    kb::scene::SkeletonAsset stranger{};
    stranger.bones.push_back({ .id = 31U, .parentIndex = -1, .name = "Tentacle", .referencePose = {}, .inverseBind = inverseBindAt({}) });
    std::string strangerError;
    Require(!kb::scene::RetargetAnimationClip(clip, source, stranger, 78U, kb::scene::SkeletonCompatibilitySignature(stranger), nullptr, &strangerError) &&
            strangerError.find("animated bones") != std::string::npos,
        "A character with too few matching bones must be refused with the reason");

    // Played through an Animator: the clip names the Mixamo skeleton, the character binds its own.
    std::filesystem::create_directories(root / "Assets" / "Rig");
    std::filesystem::create_directories(root / "Assets" / "Animation");
    Require(kb::scene::SkeletonAssetIO::Save(root / "Assets/Rig/Mixamo.kbskeleton", source) &&
            kb::scene::SkeletonAssetIO::Save(root / "Assets/Rig/Knight.kbskeleton", target),
        "Retarget test skeletons could not be saved");
    kb::scene::Scene scene;
    Require(scene.Assets().MountProject(root) && scene.Assets().Discover() == 2U, "Retarget test project could not be mounted");
    const std::uint64_t sourceId = scene.Assets().Manager().Registry().FindByPath("/Game/Rig/Mixamo.kbskeleton")->id.value;
    const std::uint64_t targetId = scene.Assets().Manager().Registry().FindByPath("/Game/Rig/Knight.kbskeleton")->id.value;
    clip.targetSkeletonAssetId = sourceId;
    kb::scene::AnimatorController controller{};
    controller.layers = { { .name = "Base", .defaultState = "Walk", .states = {
        { .id = 1U, .name = "Walk", .clipReference = "/Game/Animation/MixamoWalk.kbanim" } } } };
    Require(kb::scene::AnimationAssetIO::SaveClip(root / "Assets/Animation/MixamoWalk.kbanim", clip) &&
            kb::scene::AnimationAssetIO::SaveController(root / "Assets/Animation/Knight.kbanimcontroller", controller) &&
            scene.Assets().Discover() == 4U,
        "Retarget test clip and controller could not be saved");
    const std::uint64_t controllerId = scene.Assets().Manager().Registry().FindByPath("/Game/Animation/Knight.kbanimcontroller")->id.value;
    const kb::scene::SceneObject knight = scene.Entities().CreateObject({ .name = "Knight" });
    Require(scene.Components().SkeletonBindings().Set(knight.Entity(), kb::scene::SkeletonBindingComponent{
                .skeletonAssetId = targetId, .skeletonCompatibilitySignature = targetSignature, .enabled = true }),
        "Retarget test character could not bind its skeleton");
    scene.Components().Animators().Set(knight.Entity(), kb::scene::Animator{ .controllerAssetId = controllerId, .enabled = true });
    scene.Runtime().SetPlaying(true);
    static_cast<void>(scene.Runtime().Update(0.0F));
    static_cast<void>(scene.Runtime().Update(0.5F));
    const std::vector<std::string> errors = scene.Runtime().DrainSceneSystemErrors();
    Require(errors.empty(), ("A clip of another skeleton did not bind through retargeting: " + (errors.empty() ? std::string{} : errors.front())).c_str());
    const auto pose = scene.Animators().InstanceSkeleton(knight.Entity());
    Require(pose.has_value() && pose->currentComponentPose.positions.size() == 4U &&
            std::abs(pose->currentComponentPose.positions[1].x - 1.0F) < 0.01F &&
            std::abs(pose->currentComponentPose.positions[1].y - 2.0F) < 0.01F,
        "A retargeted clip must pose the character halfway (hips 1 m along X at 0.5 s)");
}

// A hero (team 1) and two goblins (team 2) and a barrel (team 0): damage goes through
// resistances, death happens once, senses find the goblin in front and not the one behind,
// a fireball without a target picks the seen goblin, costs mana, cools down and splashes.
void RunCombatGameplayTest(const std::filesystem::path& root) {
    std::filesystem::create_directories(root / "Assets" / "Abilities");
    kb::gameplay::AbilityAsset fireball{ .name = "Fireball", .target = kb::gameplay::AbilityTarget::Enemy, .range = 12.0F, .radius = 3.0F,
        .amount = 40.0F, .damageType = kb::gameplay::DamageType::Fire, .manaCost = 10.0F, .cooldownSeconds = 2.0F };
    Require(kb::gameplay::AbilityAssetIO::Save(root / "Assets/Abilities/Fireball.kbability", fireball), "Fireball could not be saved");
    std::string abilityError;
    const auto reread = kb::gameplay::AbilityAssetIO::Load(root / "Assets/Abilities/Fireball.kbability", &abilityError);
    Require(reread.has_value() && reread->damageType == kb::gameplay::DamageType::Fire && reread->radius == 3.0F,
        ("An ability did not survive its text file: " + abilityError).c_str());
    kb::gameplay::AbilityAsset typo{};
    Require(!kb::gameplay::AbilityAssetIO::Set(typo, "damgeType", "Fire", &abilityError) && abilityError.find("unknown key") != std::string::npos,
        "A misspelt ability key must be refused with the list of keys");

    kb::scene::Scene scene;
    Require(scene.Assets().MountProject(root) && scene.Assets().Discover() == 1U, "Combat test project could not be mounted");
    const std::uint64_t fireballId = scene.Assets().Manager().Registry().FindByPath("/Game/Abilities/Fireball.kbability")->id.value;

    const auto spawn = [&scene](const char* name, kb::scene::Vec3 position, kb::scene::CombatantComponent combatant) {
        const kb::scene::SceneObject object = scene.Entities().CreateObject({ .name = name });
        scene.Transforms().Set(object.Entity(), kb::scene::TransformComponent{ .localPosition = position });
        Require(scene.Components().Combatants().Set(object.Entity(), combatant), "A combatant could not be authored");
        return object.Entity();
    };
    kb::scene::CombatantComponent heroStats{};
    heroStats.team = 1U;
    heroStats.maximumMana = 15.0F;
    heroStats.mana = 15.0F;
    heroStats.abilities[0] = fireballId;
    heroStats.requireLineOfSight = false;
    heroStats.sightAngleDegrees = 90.0F;
    heroStats.hearingRange = 0.0F;
    kb::scene::CombatantComponent goblinStats{};
    goblinStats.team = 2U;
    goblinStats.maximumHealth = 50.0F;
    goblinStats.health = 50.0F;
    goblinStats.resistances[static_cast<std::size_t>(kb::gameplay::DamageType::Fire)] = 50.0F;
    const kb::scene::SceneEntity hero = spawn("Hero", { 0.0F, 0.0F, 0.0F }, heroStats);
    const kb::scene::SceneEntity front = spawn("Front Goblin", { 0.0F, 0.0F, 8.0F }, goblinStats);
    const kb::scene::SceneEntity beside = spawn("Beside Goblin", { 2.0F, 0.0F, 8.0F }, goblinStats);
    const kb::scene::SceneEntity behind = spawn("Behind Goblin", { 0.0F, 0.0F, -8.0F }, goblinStats);
    kb::scene::CombatantComponent barrelStats{};
    barrelStats.maximumHealth = 10.0F;
    barrelStats.health = 10.0F;
    const kb::scene::SceneEntity barrel = spawn("Barrel", { 0.0F, 0.0F, 9.0F }, barrelStats);

    scene.Runtime().SetPlaying(true);
    static_cast<void>(scene.Runtime().Update(0.25F));
    Require(scene.Runtime().DrainSceneSystemErrors().empty(), "Combat produced scene system errors");
    const auto enemy = scene.Gameplay().NearestEnemy(hero);
    Require(enemy.has_value() && enemy->entity == front && enemy->sensed && enemy->sense == kb::scene::PerceptionSense::Sight,
        "The hero must see the goblin in front of it as its nearest enemy");
    bool seesBehind = false;
    for (const kb::scene::PerceivedCombatant& seen : scene.Gameplay().Perceived(hero)) seesBehind = seesBehind || seen.entity == behind;
    Require(!seesBehind && !scene.Gameplay().AreHostile(hero, barrel) && scene.Gameplay().AreHostile(hero, front),
        "Sight must keep to its cone, and team 0 must be nobody's enemy");

    // Fireball: 40 fire damage, halved by 50% fire resistance; the goblin beside is in the splash, the barrel is no enemy.
    const kb::scene::AbilityOutcome cast = scene.Gameplay().UseAbility(hero, 1U);
    Require(cast.used && cast.target == front && cast.hits == 2U, ("A fireball without a target must hit the seen goblin and splash: " + cast.reason).c_str());
    const auto health = [&scene](kb::scene::SceneEntity entity) { return scene.Components().Combatants().TryGet(entity)->health; };
    Require(std::abs(health(front) - 30.0F) < 0.001F && std::abs(health(beside) - 30.0F) < 0.001F && health(barrel) == 10.0F &&
            std::abs(scene.Components().Combatants().TryGet(hero)->mana - 5.0F) < 0.001F &&
            std::abs(scene.Gameplay().AbilityCooldown(hero, 1U) - 2.0F) < 0.001F,
        "A fireball must deal its damage through resistances, cost its mana and start its cooldown");
    const kb::scene::AbilityOutcome again = scene.Gameplay().UseAbility(hero, 1U);
    Require(!again.used && again.reason.find("cooling down") != std::string::npos, "A cooling ability must be refused with the reason");

    // Death happens once.
    static_cast<void>(scene.Gameplay().DrainEvents());
    const kb::scene::DamageOutcome kill = scene.Gameplay().ApplyDamage(front, 100.0F, kb::gameplay::DamageType::Physical, hero);
    const kb::scene::DamageOutcome overkill = scene.Gameplay().ApplyDamage(front, 5.0F);
    std::size_t deaths = 0U;
    for (const kb::scene::GameplayEvent& event : scene.Gameplay().DrainEvents()) {
        if (event.kind == kb::scene::GameplayEventKind::Died && event.target == front && event.source == hero) ++deaths;
    }
    static_cast<void>(scene.Runtime().Update(0.25F));
    for (const kb::scene::GameplayEvent& event : scene.Gameplay().DrainEvents()) {
        if (event.kind == kb::scene::GameplayEventKind::Died) ++deaths;
    }
    Require(kill.killed && std::abs(kill.amount - 30.0F) < 0.001F && !overkill.applied && deaths == 1U && !scene.Gameplay().IsAlive(front),
        "A combatant must die once, lose only the health it had, and take no damage when dead");
    const auto next = scene.Gameplay().NearestEnemy(hero);
    Require(next.has_value() && next->entity == beside, "The dead must be forgotten by the senses");

    // Regeneration fills mana back up.
    scene.Components().Combatants().TryGet(hero)->manaRegeneration = 4.0F;
    static_cast<void>(scene.Runtime().Update(1.0F));
    Require(std::abs(scene.Components().Combatants().TryGet(hero)->mana - 9.0F) < 0.01F, "Mana must regenerate per second");
    const kb::scene::DamageOutcome revived = scene.Gameplay().Revive(front);
    Require(revived.applied && health(front) == 50.0F && scene.Gameplay().IsAlive(front), "Revive must bring a combatant back at full health");
}

void RunAnimationRuntimeTests() {
    {
        const std::filesystem::path stateMachineRoot = std::filesystem::temp_directory_path() / "21kb-animation-state-machine-tests";
        std::filesystem::remove_all(stateMachineRoot);
        RunCharacterStateMachineTest(stateMachineRoot);
        std::filesystem::remove_all(stateMachineRoot);
    }
    {
        const std::filesystem::path socketRoot = std::filesystem::temp_directory_path() / "21kb-animation-socket-tests";
        std::filesystem::remove_all(socketRoot);
        RunSocketAttachmentTest(socketRoot);
        std::filesystem::remove_all(socketRoot);
    }
    {
        const std::filesystem::path retargetRoot = std::filesystem::temp_directory_path() / "21kb-animation-retarget-tests";
        std::filesystem::remove_all(retargetRoot);
        RunAnimationRetargetTest(retargetRoot);
        std::filesystem::remove_all(retargetRoot);
    }
    {
        const std::filesystem::path combatRoot = std::filesystem::temp_directory_path() / "21kb-combat-gameplay-tests";
        std::filesystem::remove_all(combatRoot);
        RunCombatGameplayTest(combatRoot);
        std::filesystem::remove_all(combatRoot);
    }
    constexpr kb::scene::AnimationEventId kFootstepEvent = 0xA11CE001U;
    const kb::scene::Animator authoredAnimator{
        .controllerAssetId = 73U,
        .speed = 1.25F,
        .enabled = false,
        .rootMotionOwner = kb::scene::AnimatorRootMotionOwner::Rigidbody,
    };
    Require(authoredAnimator.controllerAssetId == 73U && NearlyEqual(authoredAnimator.speed, 1.25F) &&
            !authoredAnimator.enabled &&
            authoredAnimator.rootMotionOwner == kb::scene::AnimatorRootMotionOwner::Rigidbody,
        "Animator must remain a compact authored controller, speed, enabled, and root-motion-owner configuration");
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "21kb-animation-runtime-tests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "Assets" / "Animation");

    kb::scene::AnimationClip clip{};
    clip.durationSeconds = 1.0F;
    clip.looping = true;
    clip.tracks = {
        kb::scene::AnimationTransformTrack{
            .targetPath = "",
            .bindingMask = 1U,
            .keyframes = {
                { .timeSeconds = 0.0F, .transform = {} },
                { .timeSeconds = 1.0F, .transform = { .position = { 10.0F, 0.0F, 0.0F } } },
            },
        },
        kb::scene::AnimationTransformTrack{
            .targetPath = "Left Arm",
            .bindingMask = 2U,
            .keyframes = {
                { .timeSeconds = 0.0F, .transform = {} },
                { .timeSeconds = 1.0F, .transform = { .position = { 20.0F, 0.0F, 0.0F } } },
            },
        },
    };
    const auto clipPath = root / "Assets" / "Animation" / "Move.kbanim";
    Require(kb::scene::AnimationAssetIO::SaveClip(clipPath, clip), "AnimationClip production asset could not be saved");
    kb::scene::AnimationClip runClip = clip;
    runClip.tracks[0].keyframes[1].transform.position.x = 30.0F;
    runClip.tracks[1].keyframes[1].transform.position.x = 40.0F;
    runClip.events = { { .timeSeconds = 0.25F, .id = kFootstepEvent } };
    const auto runClipPath = root / "Assets" / "Animation" / "Run Fast.kbanim";
    Require(kb::scene::AnimationAssetIO::SaveClip(runClipPath, runClip), "Second AnimationClip production asset could not be saved");

    kb::scene::AnimationClip skeletalClip{};
    skeletalClip.durationSeconds = 1.0F;
    skeletalClip.looping = true;
    skeletalClip.targetSkeletonAssetId = 0xA55E7U;
    skeletalClip.targetSkeletonCompatibilitySignature = 0x5A17U;
    skeletalClip.skeletalTracks = {
        {
            .boneId = 101U,
            .bindingMask = 4U,
            .keyframes = {
                { .timeSeconds = 0.0F, .transform = {} },
                { .timeSeconds = 1.0F, .transform = { .position = { 2.0F, 0.0F, 0.0F } } },
            },
        },
    };
    skeletalClip.morphTracks = {
        { .morphTarget = "Smile", .keyframes = { { .timeSeconds = 0.0F, .weight = 0.0F }, { .timeSeconds = 1.0F, .weight = 1.0F } } },
    };
    skeletalClip.curves = {
        { .name = "FootPlant", .keyframes = { { .timeSeconds = 0.0F, .value = 0.0F }, { .timeSeconds = 1.0F, .value = 1.0F } } },
    };
    skeletalClip.rootMotionMode = kb::scene::AnimationRootMotionMode::ExtractFromBone;
    skeletalClip.rootMotionBoneId = 101U;
    const auto skeletalClipPath = root / "SkeletalRoundTrip.kbanim";
    Require(kb::scene::AnimationAssetIO::SaveClip(skeletalClipPath, skeletalClip),
        "Skeletal AnimationClip production asset could not be saved");
    const auto loadedSkeletalClip = kb::scene::AnimationAssetIO::LoadClip(skeletalClipPath);
    Require(loadedSkeletalClip.has_value() &&
            loadedSkeletalClip->targetSkeletonAssetId == skeletalClip.targetSkeletonAssetId &&
            loadedSkeletalClip->targetSkeletonCompatibilitySignature == skeletalClip.targetSkeletonCompatibilitySignature &&
            loadedSkeletalClip->skeletalTracks.size() == 1U &&
            loadedSkeletalClip->skeletalTracks.front().boneId == 101U &&
            loadedSkeletalClip->skeletalTracks.front().bindingMask == 4U &&
            loadedSkeletalClip->morphTracks.size() == 1U &&
            loadedSkeletalClip->curves.size() == 1U &&
            loadedSkeletalClip->rootMotionMode == kb::scene::AnimationRootMotionMode::ExtractFromBone &&
            loadedSkeletalClip->rootMotionBoneId == 101U,
        "Skeletal AnimationClip round trip lost canonical bindings");
    std::string legacySkeletalText = ReadTextFile(skeletalClipPath);
    const std::string currentSkeletalHeader = "21kb AnimationClip 2";
    const std::size_t currentSkeletalHeaderOffset =
        legacySkeletalText.find(currentSkeletalHeader);
    const std::string currentSkeletalTrack = "skeletalTrack 101 4";
    const std::size_t currentSkeletalTrackOffset =
        legacySkeletalText.find(currentSkeletalTrack);
    Require(currentSkeletalHeaderOffset != std::string::npos &&
            currentSkeletalTrackOffset != std::string::npos,
        "Skeletal AnimationClip did not serialize its binding mask");
    legacySkeletalText.replace(
        currentSkeletalHeaderOffset, currentSkeletalHeader.size(),
        "21kb AnimationClip 1");
    legacySkeletalText.replace(
        currentSkeletalTrackOffset, currentSkeletalTrack.size(),
        "skeletalTrack 101");
    const auto legacySkeletalClipPath = root / "LegacySkeletal.kbanim";
    WriteTextFile(legacySkeletalClipPath, legacySkeletalText);
    const auto loadedLegacySkeletalClip =
        kb::scene::AnimationAssetIO::LoadClip(legacySkeletalClipPath);
    Require(loadedLegacySkeletalClip.has_value() &&
            loadedLegacySkeletalClip->skeletalTracks.size() == 1U &&
            loadedLegacySkeletalClip->skeletalTracks.front().bindingMask ==
                ~std::uint64_t{ 0U },
        "Legacy skeletal track did not migrate to the full binding mask");
    kb::scene::AnimationClip mixedBindingClip = skeletalClip;
    mixedBindingClip.tracks = clip.tracks;
    Require(!kb::scene::AnimationAssetIO::SaveClip(root / "MixedBinding.kbanim", mixedBindingClip),
        "Skeletal AnimationClip accepted SceneEntity path bindings");
    kb::scene::AnimationClip invalidRootMotionClip = skeletalClip;
    invalidRootMotionClip.rootMotionBoneId = 999U;
    Require(!kb::scene::AnimationAssetIO::SaveClip(root / "InvalidRootMotion.kbanim", invalidRootMotionClip),
        "Skeletal AnimationClip accepted root motion outside its bone bindings");

    kb::scene::AnimatorController controller{};
    controller.parameters = {
        { .name = "Grounded", .type = kb::scene::AnimatorParameterType::Bool, .boolDefault = true },
        { .name = "Lives", .type = kb::scene::AnimatorParameterType::Int, .intDefault = 3 },
        { .name = "Speed", .type = kb::scene::AnimatorParameterType::Float, .floatDefault = 2.5F },
        { .name = "Jump", .type = kb::scene::AnimatorParameterType::Trigger },
    };
    controller.layers = {
        { .name = "Root Layer", .defaultState = "Walk State", .weight = 1.0F, .mask = 1U, .states = {
            { .id = 1U, .name = "Walk State", .clipReference = "/Game/Animation/Move.kbanim" },
            { .id = 2U, .name = "Run State", .clipReference = "/Game/Animation/Run Fast.kbanim" },
            { .id = 3U, .name = "Blend State", .blendParameter = "Speed", .blendChildren = {
                { .threshold = 0.0F, .clipReference = "/Game/Animation/Move.kbanim" },
                { .threshold = 10.0F, .clipReference = "/Game/Animation/Run Fast.kbanim" },
            } },
        }, .transitions = {
            { .id = 4U, .fromState = "Walk State", .toState = "Run State", .durationSeconds = 0.2F,
              .conditions = {
                  { .parameter = "Grounded", .mode = kb::scene::AnimatorConditionMode::BoolEquals, .boolValue = false },
                  { .parameter = "Lives", .mode = kb::scene::AnimatorConditionMode::IntGreater, .intValue = 3 },
                  { .parameter = "Speed", .mode = kb::scene::AnimatorConditionMode::FloatGreater, .floatValue = 6.0F },
                  { .parameter = "Jump", .mode = kb::scene::AnimatorConditionMode::TriggerSet },
              } },
        } },
        { .name = "Upper Body", .defaultState = "Walk State", .weight = 0.5F, .mask = 2U, .states = {
            { .name = "Walk State", .clipReference = "/Game/Animation/Move.kbanim" },
            { .name = "Run State", .clipReference = "/Game/Animation/Run Fast.kbanim" },
        } },
    };
    controller.graphLayout = {
        { .stateId = 1U, .positionX = 0, .positionY = 0 },
        { .stateId = 2U, .positionX = 240, .positionY = 0 },
        { .stateId = 3U, .positionX = 480, .positionY = 0 },
    };
    controller.graphComments = {{ .id = 5U, .text = "Locomotion", .positionX = -20, .positionY = -80, .width = 780, .height = 280 }};
    controller.graphGroups = {{ .id = 6U, .name = "Movement", .stateIds = { 1U, 2U, 3U } }};
    const auto controllerPath = root / "Assets" / "Animation" / "Character.kbanimcontroller";
    Require(kb::scene::AnimationAssetIO::SaveController(controllerPath, controller), "AnimatorController production asset could not be saved");
    const std::optional<kb::scene::AnimatorController> persistedController =
        kb::scene::AnimationAssetIO::LoadController(controllerPath);
    Require(persistedController.has_value() && persistedController->graphLayout.size() == 5U &&
            persistedController->graphComments.size() == 1U &&
            persistedController->graphComments.front().text == "Locomotion" &&
            persistedController->graphGroups.size() == 1U &&
            persistedController->graphGroups.front().stateIds == std::vector<std::uint64_t>{ 1U, 2U, 3U },
        "Animator graph serialization did not preserve layout, comments, and groups");
    const std::filesystem::path roundTripRoot = root / "RoundTrip";
    const std::filesystem::path deterministicClipPath = roundTripRoot / "MoveCopy.kbanim";
    Require(kb::scene::AnimationAssetIO::SaveClip(deterministicClipPath, clip) &&
            ReadTextFile(clipPath) == ReadTextFile(deterministicClipPath),
        "AnimationClip serialization is not deterministic");
    const std::string serializedClip = ReadTextFile(clipPath);
    const std::size_t clipHeaderEnd = serializedClip.find('\n');
    Require(clipHeaderEnd != std::string::npos,
        "AnimationClip serialization has no schema header");
    const std::filesystem::path legacyClipPath = roundTripRoot / "Legacy.kbanim";
    WriteTextFile(legacyClipPath, std::string_view{ serializedClip }.substr(clipHeaderEnd + 1U));
    Require(kb::scene::AnimationAssetIO::LoadClip(legacyClipPath).has_value(),
        "AnimationClip legacy migration failed");
    const std::filesystem::path corruptClipPath = roundTripRoot / "Corrupt.kbanim";
    WriteTextFile(corruptClipPath, "21kb AnimationClip 99\n");
    std::string corruptClipError;
    Require(!kb::scene::AnimationAssetIO::LoadClip(corruptClipPath, &corruptClipError).has_value() &&
            corruptClipError.find("line 1") != std::string::npos,
        "AnimationClip accepted an unsupported schema version without a diagnostic");
    const std::filesystem::path deterministicControllerPath = roundTripRoot / "CharacterCopy.kbanimcontroller";
    Require(kb::scene::AnimationAssetIO::SaveController(deterministicControllerPath, controller) &&
            ReadTextFile(controllerPath) == ReadTextFile(deterministicControllerPath),
        "AnimatorController serialization is not deterministic");
    const std::string serializedController = ReadTextFile(controllerPath);
    const std::size_t controllerHeaderEnd = serializedController.find('\n');
    Require(controllerHeaderEnd != std::string::npos,
        "AnimatorController serialization has no schema header");
    const std::filesystem::path legacyControllerPath = roundTripRoot / "Legacy.kbanimcontroller";
    WriteTextFile(legacyControllerPath,
        std::string_view{ serializedController }.substr(controllerHeaderEnd + 1U));
    Require(kb::scene::AnimationAssetIO::LoadController(legacyControllerPath).has_value(),
        "AnimatorController legacy migration failed");
    const std::filesystem::path versionOneControllerPath =
        roundTripRoot / "VersionOne.kbanimcontroller";
    WriteTextFile(versionOneControllerPath,
        "21kb AnimatorController 1\n"
        "parameter \"Speed\" Float 0\n"
        "layer \"Base\" \"Idle\" 1 1\n"
        "state 0 \"Idle\" \"/Game/Animation/Move.kbanim\"\n");
    Require(kb::scene::AnimationAssetIO::LoadController(
                versionOneControllerPath).has_value(),
        "AnimatorController schema 1 migration failed");
    const std::filesystem::path corruptControllerPath = roundTripRoot / "Corrupt.kbanimcontroller";
    WriteTextFile(corruptControllerPath, "21kb AnimatorController 99\n");
    std::string corruptControllerError;
    Require(!kb::scene::AnimationAssetIO::LoadController(
                corruptControllerPath, &corruptControllerError).has_value() &&
            corruptControllerError.find("line 1") != std::string::npos,
        "AnimatorController accepted an unsupported schema version without a diagnostic");
    kb::scene::AnimationClip turnClip{};
    turnClip.durationSeconds = 1.0F;
    turnClip.looping = true;
    turnClip.tracks = {
        kb::scene::AnimationTransformTrack{
            .targetPath = "",
            .bindingMask = 1U,
            .keyframes = {
                { .timeSeconds = 0.0F, .transform = {} },
                { .timeSeconds = 1.0F, .transform = {
                    .rotation = { 0.0F, 0.70710678F, 0.0F, 0.70710678F },
                } },
            },
        },
    };
    Require(kb::scene::AnimationAssetIO::SaveClip(
                root / "Assets" / "Animation" / "Turn.kbanim", turnClip),
        "Root-rotation AnimationClip production asset could not be saved");
    kb::scene::AnimatorController turnController{};
    turnController.layers = {
        { .name = "Root Layer", .defaultState = "Turn", .weight = 1.0F, .mask = 1U,
          .states = { { .name = "Turn", .clipReference = "/Game/Animation/Turn.kbanim" } } },
    };
    Require(kb::scene::AnimationAssetIO::SaveController(
                root / "Assets" / "Animation" / "Turn.kbanimcontroller", turnController),
        "Root-rotation AnimatorController production asset could not be saved");
    kb::scene::AnimatorController rigController{};
    rigController.layers = {
        { .name = "Rig Layer", .defaultState = "Bind", .states = {
            { .name = "Bind", .clipReference = "/Game/Animation/Move.kbanim" },
        } },
    };
    rigController.rigConstraints = {
        {
            .name = "Arm IK",
            .type = kb::scene::AnimatorRigConstraintType::TwoBoneIK,
            .constrainedPath = "Upper",
            .midPath = "Upper/Lower",
            .tipPath = "Upper/Lower/Hand",
            .target = "HandTarget",
            .poleTarget = "ElbowPole",
        },
        {
            .name = "Look",
            .type = kb::scene::AnimatorRigConstraintType::Aim,
            .constrainedPath = "Look",
            .target = "LookTarget",
        },
        {
            .name = "Follower",
            .type = kb::scene::AnimatorRigConstraintType::CopyTransform,
            .constrainedPath = "Follower",
            .target = "CopyTarget",
        },
    };
    Require(kb::scene::AnimationAssetIO::SaveController(
                root / "Assets" / "Animation" / "Rig.kbanimcontroller",
                rigController),
        "Rig AnimatorController production asset could not be saved");
    kb::scene::AnimatorController invalidBlendController = controller;
    invalidBlendController.layers[0].states[2].blendParameter = "Grounded";
    std::string invalidBlendError;
    const std::string controllerBeforeInvalidSave = ReadTextFile(controllerPath);
    Require(!kb::scene::AnimationAssetIO::SaveController(
                controllerPath,
                invalidBlendController,
                &invalidBlendError) &&
            invalidBlendError.find("Blend State") != std::string::npos &&
            invalidBlendError.find("Grounded") != std::string::npos &&
            ReadTextFile(controllerPath) == controllerBeforeInvalidSave,
        "Invalid controller save replaced the active serialized controller");
    kb::scene::AnimatorController invalidRigController = rigController;
    invalidRigController.rigConstraints[0].weight = 0.0F;
    Require(!kb::scene::AnimationAssetIO::SaveController(
                root / "Assets" / "Animation" / "InvalidRig.kbanimcontroller",
                invalidRigController),
        "Rig asset accepted a zero-weight dead constraint");
    const auto scriptPath = root / "Assets" / "Logic" / "Animate.lua";
    std::filesystem::create_directories(scriptPath.parent_path());
    {
        std::ofstream script{ scriptPath, std::ios::binary | std::ios::trunc };
        script << R"(function Created(self)
    Events.Subscribe("OnAnimationEvent", function(event)
        SetShared("animationSchemaMajor", event.args.schemaMajor)
        SetShared("animationSchemaMinor", event.args.schemaMinor)
        SetShared("animationEventId", event.args.event)
        SetShared("animationEventLayer", event.args.layer)
        SetShared("animationEventState", event.args.state)
        SetShared("animationEventTime", event.args.normalizedTime)
        local applied, error = CallFunction("Animator.SetFloat", { name = "Speed", value = 8.0 })
        if error then SetShared("animationEventError", error) end
        SetShared("animationEventMutatedRuntime", applied)
    end)
end

function Tick(self)
    if GetShared("animatorConfigured") then return end
    local applied, error = CallFunction("Animator.SetSpeed", { speed = 2.0 })
    if error then SetShared("animatorError", error) return end
    CallFunction("Animator.Play", { layer = "Root Layer", state = "Run State", normalizedTime = 0.0 })
    SetShared("animatorConfigured", true)
end
)";
    }
    const auto rigScriptPath = root / "Assets" / "Logic" / "Rig.lua";
    {
        std::ofstream script{ rigScriptPath, std::ios::binary | std::ios::trunc };
        script << R"(function Tick(self)
    if GetShared("rigConfigured") then return end
    local hand, handError = CallFunction("Animator.SetIKTarget", {
        name = "HandTarget", x = 1.0, y = 1.0, z = 0.0,
        rotationWeight = 0.0
    })
    local pole, poleError = CallFunction("Animator.SetIKTarget", {
        name = "ElbowPole", x = 0.0, y = 0.0, z = 1.0
    })
    local look, lookError = CallFunction("Animator.SetIKTarget", {
        name = "LookTarget", x = 0.0, y = 0.0, z = 5.0
    })
    local copy, copyError = CallFunction("Animator.SetIKTarget", {
        name = "CopyTarget", x = 3.0, y = 2.0, z = 1.0,
        rotationY = 0.70710678, rotationW = 0.70710678
    })
    if handError or poleError or lookError or copyError then
        SetShared("rigError", handError or poleError or lookError or copyError)
        return
    end
    SetShared("rigConfigured", hand and pole and look and copy)
end
)";
    }
    const auto scenePath = root / "AnimationRuntime.21kbscene";

    {
        kb::scene::Scene scene;
        Require(scene.Assets().MountProject(root), "Animation runtime project mount failed");
        Require(scene.Assets().Discover() == 8U, "Animation runtime discovery did not register clips, controllers, and script");
        const auto* metadata = scene.Assets().Manager().Registry().FindByPath("/Game/Animation/Character.kbanimcontroller");
        Require(metadata != nullptr && metadata->type == kb::scene::kAnimatorControllerAssetType,
            "AnimatorController was not classified by the production asset registry");
        Require(metadata->dependencies.size() == 2U,
            "AnimatorController did not publish its deduplicated clip dependency to the asset registry");
        const auto loadedController =
            kb::scene::AnimationAssetIO::LoadController(controllerPath);
        Require(loadedController.has_value() &&
                loadedController->layers[0].states[2].blendChildren.size() == 2U &&
                loadedController->layers[0].states[2].blendParameter == "Speed",
            "AnimatorController asset round trip lost its typed 1D blend tree");
        const auto* scriptMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Logic/Animate.lua");
        Require(scriptMetadata != nullptr, "Animator runtime script was not discovered");
        const auto* rigScriptMetadata =
            scene.Assets().Manager().Registry().FindByPath("/Game/Logic/Rig.lua");
        Require(rigScriptMetadata != nullptr,
            "Rig runtime script was not discovered");

        {
            kb::scene::Scene authored;
            const kb::scene::SceneObject authoredOwner = authored.Entities().CreateObject({ .name = "Character" });
            const kb::scene::SceneObject authoredArm = authored.Entities().CreateObject({ .name = "Left Arm" });
            Require(authoredArm.SetParent(authoredOwner), "Authored animation hierarchy could not be created");
            authored.Components().Behaviours().Set(authoredOwner.Entity(), kb::scene::BehaviourComponent{
                .behaviourAssetId = scriptMetadata->id.value,
                .backend = kb::scene::BehaviourBackend::Lua,
                .enabled = true,
            });
            authored.Components().Animators().Set(authoredOwner.Entity(), kb::scene::Animator{
                .controllerAssetId = metadata->id.value,
                .speed = 1.0F,
                .enabled = true,
                .rootMotionOwner = kb::scene::AnimatorRootMotionOwner::Animator,
            });
            Require(kb::scene::SceneDocumentService::Save(authored, scenePath, "AnimationRuntime"),
                "Animation runtime scene with serialized behaviour could not be saved");
        }

        const kb::scene::SceneObject owner = scene.Entities().CreateObject({ .name = "Character" });
        const kb::scene::SceneObject arm = scene.Entities().CreateObject({ .name = "Left Arm" });
        Require(arm.SetParent(owner), "Animation runtime hierarchy could not be created");
        scene.Components().Animators().Set(owner.Entity(), kb::scene::Animator{
            .controllerAssetId = metadata->id.value,
            .speed = 2.0F,
            .enabled = true,
        });
        scene.Runtime().SetPlaying(false);
        static_cast<void>(scene.Runtime().Update(0.0F));
        Require(scene.Animators().Exists(owner.Entity()),
            "Authored Animator component did not attach its controller and clip dependencies");
        Require(NearlyEqual(scene.Animators().Speed(owner.Entity()), 2.0F) &&
                scene.Animators().SetSpeed(owner.Entity(), 1.0F),
            "Animator component did not initialize the runtime speed");
        const auto parameters = scene.Animators().Parameters(owner.Entity());
        Require(parameters.size() == 4U && parameters[0].boolValue && parameters[1].intValue == 3 &&
                NearlyEqual(parameters[2].floatValue, 2.5F) && !parameters[3].boolValue,
            "Animator parameters did not retain their authored types/defaults");

        static_cast<void>(scene.Runtime().Update(0.5F));
        Require(NearlyEqual(scene.Transforms().Get(owner.Entity()).localPosition.x, 0.0F),
            "Animator advanced while the editor/runtime scene was not playing");
        scene.Runtime().SetPlaying(true);
        static_cast<void>(scene.Runtime().Update(0.5F));
        const auto ownerTransform = scene.Transforms().Get(owner.Entity());
        const auto armTransform = scene.Transforms().Get(arm.Entity());
        Require(NearlyEqual(ownerTransform.localPosition.x, 5.0F),
            "AnimatorSceneSystem did not apply the root layer through Scene::Runtime().Update");
        Require(NearlyEqual(armTransform.localPosition.x, 5.0F),
            "Animator layer mask/weight did not apply the child track deterministically");
        Require(NearlyEqual(armTransform.worldPosition.x, 10.0F),
            "Animated local transforms were not published through hierarchy synchronization");
        Require(scene.Animators().SetSpeed(owner.Entity(), 2.0F) && NearlyEqual(scene.Animators().Speed(owner.Entity()), 2.0F),
            "Animator speed could not be set and queried");
        Require(scene.Animators().Play(owner.Entity(), "Root Layer", "Run State", 0.0F),
            "Animator Play could not select a named state");
        Require(!scene.Animators().Play(owner.Entity(), "Root Layer", "Run State", 1.01F),
            "Animator Play silently clamped an invalid normalized time");
        static_cast<void>(scene.Runtime().Update(0.25F));
        Require(NearlyEqual(scene.Transforms().Get(owner.Entity()).localPosition.x, 15.0F),
            "Animator Play/speed did not drive the selected state in production update");
        const auto nativeEvents = scene.Animators().DrainEvents();
        Require(nativeEvents.size() == 1U && nativeEvents[0].target == owner.Entity() &&
                nativeEvents[0].schemaMajor == 1 && nativeEvents[0].schemaMinor == 0 &&
                nativeEvents[0].eventId == kFootstepEvent && nativeEvents[0].layer == "Root Layer" &&
                nativeEvents[0].state == "Run State" && NearlyEqual(nativeEvents[0].normalizedTime, 0.25F),
            "Animation event did not cross the native typed queue at the authored playhead time");
        Require(scene.Animators().SeekNormalized(owner.Entity(), 0.5F) &&
                !scene.Animators().SeekNormalized(owner.Entity(), -0.01F),
            "Animator normalized seek did not validate and apply the requested playhead");
        static_cast<void>(scene.Runtime().Update(0.0F));
        const auto seekedState = scene.Animators().State(owner.Entity(), "Root Layer");
        Require(seekedState.has_value() && !seekedState->transitioning &&
                NearlyEqual(seekedState->normalizedTime, 0.5F) &&
                NearlyEqual(scene.Transforms().Get(owner.Entity()).localPosition.x, 15.0F),
            "Animator normalized seek did not deterministically resample the production pose");
        static_cast<void>(scene.Runtime().Update(0.5F));
        const auto loopEvents = scene.Animators().DrainEvents();
        Require(loopEvents.size() == 1U && loopEvents[0].eventId == kFootstepEvent,
            "Looping animation skipped or duplicated an event while the playhead wrapped");
        Require(scene.Animators().CrossFade(owner.Entity(), "Root Layer", "Walk State", 0.5F, 0.0F),
            "Animator CrossFade could not start a named-state transition");
        static_cast<void>(scene.Runtime().Update(0.25F));
        const auto transition = scene.Animators().State(owner.Entity(), "Root Layer");
        Require(transition.has_value() && transition->transitioning && transition->state == "Walk State" &&
                NearlyEqual(transition->transitionProgress, 0.5F),
            "Animator state query did not report the active transition");
        Require(NearlyEqual(scene.Transforms().Get(owner.Entity()).localPosition.x, 2.5F),
            "Animator CrossFade state progressed but did not blend both live poses into the scene transform");
        Require(scene.Animators().SetBool(owner.Entity(), "Grounded", false) &&
                scene.Animators().SetInt(owner.Entity(), "Lives", 4) &&
                scene.Animators().SetFloat(owner.Entity(), "Speed", 7.0F) &&
                scene.Animators().SetTrigger(owner.Entity(), "Jump"),
            "Animator typed parameter mutation failed");
        const auto changedParameters = scene.Animators().Parameters(owner.Entity());
        Require(!changedParameters[0].boolValue && changedParameters[1].intValue == 4 &&
                NearlyEqual(changedParameters[2].floatValue, 7.0F) && changedParameters[3].boolValue,
            "Animator typed parameter values were incorrect");
        static_cast<void>(scene.Runtime().Update(0.25F));
        const auto completedTransition = scene.Animators().State(owner.Entity(), "Root Layer");
        Require(completedTransition.has_value() && !completedTransition->transitioning &&
                completedTransition->state == "Walk State",
            "Animator CrossFade did not complete deterministically");
        static_cast<void>(scene.Runtime().Update(0.1F));
        const auto automaticTransition = scene.Animators().State(owner.Entity(), "Root Layer");
        Require(automaticTransition.has_value() && automaticTransition->transitioning &&
                automaticTransition->state == "Run State" && !changedParameters[3].boolValue,
            "Controller transition did not consume bool/int/float conditions and reset its trigger on entry");
        scene.Runtime().SetPlaying(false);
        scene.Entities().Destroy(owner);
        static_cast<void>(scene.Runtime().Update(0.0F));
        Require(!scene.Animators().Exists(owner.Entity()),
            "Animator did not release its retained assets after owner destruction while editor playback was paused");

        const kb::scene::SceneObject rootMotionOwner =
            scene.Entities().CreateObject({ .name = "Root Motion Character" });
        const kb::scene::SceneObject rootMotionArm =
            scene.Entities().CreateObject({ .name = "Left Arm" });
        Require(rootMotionArm.SetParent(rootMotionOwner),
            "Root-motion runtime hierarchy could not bind the controller's child track");
        scene.Components().Animators().Set(rootMotionOwner.Entity(), kb::scene::Animator{
            .controllerAssetId = metadata->id.value,
            .speed = 1.0F,
            .enabled = true,
            .rootMotionOwner = kb::scene::AnimatorRootMotionOwner::Animator,
        });
        scene.Runtime().SetPlaying(true);
        static_cast<void>(scene.Runtime().Update(0.25F));
        static_cast<void>(scene.Runtime().Update(0.75F));
        Require(NearlyEqual(scene.Transforms().Get(rootMotionOwner.Entity()).localPosition.x, 10.0F),
            "Animator-owned root motion did not accumulate the root-track delta through a loop in production runtime");
        const auto* turnMetadata =
            scene.Assets().Manager().Registry().FindByPath("/Game/Animation/Turn.kbanimcontroller");
        Require(turnMetadata != nullptr, "Root-rotation AnimatorController metadata was not found");
        const kb::scene::SceneObject turningOwner =
            scene.Entities().CreateObject({ .name = "Turning Root Motion Character" });
        scene.Components().Animators().Set(turningOwner.Entity(), kb::scene::Animator{
            .controllerAssetId = turnMetadata->id.value,
            .speed = 1.0F,
            .enabled = true,
            .rootMotionOwner = kb::scene::AnimatorRootMotionOwner::Animator,
        });
        static_cast<void>(scene.Runtime().Update(0.5F));
        const kb::scene::Quat turningRotation =
            scene.Transforms().Get(turningOwner.Entity()).localRotation;
        Require(std::abs(turningRotation.y - 0.38268343F) <= 0.001F &&
                std::abs(turningRotation.w - 0.92387953F) <= 0.001F,
            "Animator-owned root motion did not extract and accumulate the root-track quaternion delta");

        const kb::scene::SceneObject blendedOwner =
            scene.Entities().CreateObject({ .name = "Blend Tree Character" });
        const kb::scene::SceneObject blendedArm =
            scene.Entities().CreateObject({ .name = "Left Arm" });
        Require(blendedArm.SetParent(blendedOwner),
            "Blend-tree hierarchy could not bind the controller");
        scene.Components().Animators().Set(blendedOwner.Entity(), kb::scene::Animator{
            .controllerAssetId = metadata->id.value,
            .speed = 1.0F,
            .enabled = true,
            .rootMotionOwner = kb::scene::AnimatorRootMotionOwner::Animator,
        });
        static_cast<void>(scene.Runtime().Update(0.0F));
        Require(scene.Animators().SetFloat(blendedOwner.Entity(), "Speed", 5.0F) &&
                scene.Animators().Play(
                    blendedOwner.Entity(), "Root Layer", "Blend State", 0.0F),
            "Blend-tree state could not be driven through the production Animator API");
        static_cast<void>(scene.Runtime().Update(0.2F));
        Require(NearlyEqual(
                    scene.Transforms().Get(blendedOwner.Entity()).localPosition.x,
                    4.0F),
            "1D blend tree did not blend both retained clips through root-motion runtime");
        const float formerArmPosition =
            scene.Transforms().Get(blendedArm.Entity()).localPosition.x;
        blendedArm.SetName("Former Left Arm");
        const kb::scene::SceneObject replacementArm =
            scene.Entities().CreateObject({ .name = "Left Arm" });
        Require(replacementArm.SetParent(blendedOwner),
            "Replacement animation binding could not enter the authored hierarchy");
        static_cast<void>(scene.Runtime().Update(0.0F));
        Require(scene.Animators().SetFloat(
                    blendedOwner.Entity(), "Speed", 5.0F) &&
                scene.Animators().Play(
                    blendedOwner.Entity(), "Root Layer", "Blend State", 0.0F),
            "Animator did not rebind after the canonical scene hierarchy changed");
        static_cast<void>(scene.Runtime().Update(0.1F));
        Require(NearlyEqual(
                    scene.Transforms().Get(blendedArm.Entity()).localPosition.x,
                    formerArmPosition) &&
                NearlyEqual(
                    scene.Transforms().Get(replacementArm.Entity()).localPosition.x,
                    1.0F),
            "Animator kept writing a stale entity binding after hierarchy replacement");

        const kb::scene::SceneObject prefabAuthor =
            scene.Entities().CreateObject({ .name = "Prefab Animator" });
        const kb::scene::SceneObject prefabAuthorArm =
            scene.Entities().CreateObject({ .name = "Left Arm" });
        Require(prefabAuthorArm.SetParent(prefabAuthor),
            "Animator prefab author hierarchy could not be created");
        scene.Components().Animators().Set(
            prefabAuthor.Entity(), kb::scene::Animator{
                .controllerAssetId = metadata->id.value,
                .speed = 1.0F,
                .enabled = true,
            });
        const kb::scene::ScenePrefab animatorPrefab =
            scene.Prefabs().Capture(prefabAuthor);
        Require(animatorPrefab.NodeCount() == 2U,
            "Animator prefab capture lost its hierarchy or component");
        scene.Components().Animators().Remove(prefabAuthor.Entity());
        kb::scene::TransformComponent* authorArmTransform =
            scene.Transforms().TryGet(prefabAuthorArm.Entity());
        Require(authorArmTransform != nullptr,
            "Animator prefab author lost its Transform");
        authorArmTransform->localPosition.x = -123.0F;
        scene.Transforms().MarkModified(prefabAuthorArm.Entity());

        const kb::scene::ScenePrefabInstance animatorInstance =
            scene.Prefabs().Instantiate(animatorPrefab);
        Require(!animatorInstance.Empty() &&
                animatorInstance.ObjectCount() == 2U,
            "Animator prefab did not instantiate through ScenePrefabs");
        const kb::scene::SceneObject prefabRoot =
            animatorInstance.RootObject();
        const kb::scene::SceneObject prefabArm =
            animatorInstance.ObjectAt(1U);
        static_cast<void>(scene.Runtime().Update(0.0F));
        Require(scene.Animators().Exists(prefabRoot.Entity()) &&
                scene.Animators().Play(
                    prefabRoot.Entity(), "Root Layer", "Walk State", 0.0F),
            "Instantiated Animator did not attach its controller runtime");
        static_cast<void>(scene.Runtime().Update(0.2F));
        const float prefabArmBeforeUnload =
            scene.Transforms().Get(prefabArm.Entity()).localPosition.x;
        Require(prefabArmBeforeUnload > 0.0F &&
                NearlyEqual(
                    scene.Transforms().Get(prefabAuthorArm.Entity())
                        .localPosition.x,
                    -123.0F),
            "Prefab Animator reused the author's stale entity binding");

        const auto* runMetadata =
            scene.Assets().Manager().Registry().FindByPath(
                "/Game/Animation/Run Fast.kbanim");
        Require(runMetadata != nullptr,
            "Blend-tree child clip metadata was not discovered");
        const auto* moveMetadata =
            scene.Assets().Manager().Registry().FindByPath(
                "/Game/Animation/Move.kbanim");
        Require(moveMetadata != nullptr &&
                scene.Assets().Manager().Unload(moveMetadata->id) &&
                scene.Assets().Manager().Unload(metadata->id),
            "Live prefab Animator clip/controller could not be unloaded");
        static_cast<void>(scene.Runtime().Update(0.2F));
        Require(scene.Animators().Exists(prefabRoot.Entity()) &&
                scene.Animators().State(
                    prefabRoot.Entity(), "Root Layer").has_value() &&
                scene.Transforms().Get(prefabArm.Entity()).localPosition.x >
                    prefabArmBeforeUnload,
            "Live prefab Animator did not reload clip/controller and preserve its bound runtime");

        kb::scene::AnimationClip editedRunClip = runClip;
        editedRunClip.tracks[0].keyframes[1].transform.position.x = 50.0F;
        editedRunClip.tracks[1].keyframes[1].transform.position.x = 60.0F;
        Require(kb::scene::AnimationAssetIO::SaveClip(
                    runClipPath, editedRunClip) &&
                scene.Assets().Manager().Unload(runMetadata->id),
            "Edited blend-tree child clip could not invalidate its retained runtime asset");
        static_cast<void>(scene.Runtime().Update(0.0F));
        Require(scene.Animators().SetFloat(
                    blendedOwner.Entity(), "Speed", 5.0F) &&
                scene.Animators().Play(
                    blendedOwner.Entity(), "Root Layer", "Blend State", 0.0F),
            "Blend tree could not restart after child-clip reload");
        const float beforeReloadedClip =
            scene.Transforms().Get(blendedOwner.Entity()).localPosition.x;
        static_cast<void>(scene.Runtime().Update(0.2F));
        Require(NearlyEqual(
                    scene.Transforms().Get(blendedOwner.Entity()).localPosition.x -
                        beforeReloadedClip,
                    6.0F),
            "Animator retained the pre-save blend child after canonical clip invalidation");
        Require(kb::scene::AnimationAssetIO::SaveClip(runClipPath, runClip) &&
                scene.Assets().Manager().Unload(runMetadata->id),
            "Blend-tree child fixture could not be restored after reload verification");
        static_cast<void>(scene.Runtime().Update(0.0F));

        const auto* rigMetadata =
            scene.Assets().Manager().Registry().FindByPath(
                "/Game/Animation/Rig.kbanimcontroller");
        Require(rigMetadata != nullptr && rigMetadata->dependencies.size() == 1U,
            "Rig controller asset/dependency was not discovered");
        const kb::scene::SceneObject rigOwner =
            scene.Entities().CreateObject({ .name = "Rig Character" });
        const kb::scene::SceneObject rigArm =
            scene.Entities().CreateObject({ .name = "Left Arm" });
        Require(rigArm.SetParent(rigOwner),
            "Rig controller clip hierarchy could not bind");
        const kb::scene::SceneObject upper =
            scene.Entities().CreateObject({ .name = "Upper" });
        const kb::scene::SceneObject lower =
            scene.Entities().CreateObject({
                .name = "Lower",
                .transform = kb::scene::TransformComponent{
                    .localPosition = { 1.0F, 0.0F, 0.0F },
                },
            });
        const kb::scene::SceneObject hand =
            scene.Entities().CreateObject({
                .name = "Hand",
                .transform = kb::scene::TransformComponent{
                    .localPosition = { 1.0F, 0.0F, 0.0F },
                },
            });
        const kb::scene::SceneObject look =
            scene.Entities().CreateObject({ .name = "Look" });
        const kb::scene::SceneObject follower =
            scene.Entities().CreateObject({ .name = "Follower" });
        Require(upper.SetParent(rigOwner) && lower.SetParent(upper) &&
                hand.SetParent(lower) && look.SetParent(rigOwner) &&
                follower.SetParent(rigOwner),
            "Rig constraint hierarchy could not be authored");
        scene.Components().Animators().Set(rigOwner.Entity(), kb::scene::Animator{
            .controllerAssetId = rigMetadata->id.value,
            .speed = 1.0F,
            .enabled = true,
        });
        scene.Components().Behaviours().Set(
            rigOwner.Entity(), kb::scene::BehaviourComponent{
                .behaviourAssetId = rigScriptMetadata->id.value,
                .backend = kb::scene::BehaviourBackend::Lua,
                .enabled = true,
            });
        kb::script::ScriptRuntimeHost rigScriptHost{ scene };
        Require(rigScriptHost.Succeeded() &&
                rigScriptHost.Functions().FindSignature("Animator.SetIKTarget") != nullptr &&
                rigScriptHost.Functions().FindSignature("Animator.ClearIKTarget") != nullptr &&
                rigScriptHost.InstallSceneSystem(),
            "Typed IK target functions were not registered in the production script host");
        static_cast<void>(scene.Runtime().Update(0.0F));
        static_cast<void>(scene.Runtime().Update(0.0F));
        Require(rigScriptHost.SharedState().Get("rigConfigured").has_value() &&
                rigScriptHost.SharedState().Get("rigConfigured")->AsBool() &&
                !rigScriptHost.SharedState().Get("rigError").has_value(),
            "Project Lua script did not configure all typed IK targets");
        const kb::scene::TransformComponent handTransform =
            scene.Transforms().Get(hand.Entity());
        Require(kb::math::Length(
                    handTransform.worldPosition -
                    kb::scene::Vec3{ 1.0F, 1.0F, 0.0F }) <= 0.01F,
            "TwoBoneIK did not drive the bound hierarchy to its runtime target");
        const kb::scene::TransformComponent lookTransform =
            scene.Transforms().Get(look.Entity());
        const kb::scene::Vec3 lookForward = kb::math::Rotate(
            lookTransform.worldRotation, kb::scene::Vec3{ 0.0F, 0.0F, 1.0F });
        Require(lookForward.z >= 0.999F,
            "Aim rig constraint did not orient its bound Transform");
        const kb::scene::TransformComponent followerTransform =
            scene.Transforms().Get(follower.Entity());
        Require(kb::math::Length(
                    followerTransform.worldPosition -
                    kb::scene::Vec3{ 3.0F, 2.0F, 1.0F }) <= 0.001F &&
                std::abs(followerTransform.worldRotation.y - 0.70710678F) <= 0.001F,
            "CopyTransform rig constraint did not consume the typed world target");
        {
            // The same rig ten thousand kilometres out, given double-precision targets, solves as precisely as
            // near the origin (a float position there is only good to a metre).
            const kb::math::DVec3 far{ 1.0e7 + 0.25, 0.0, 1.0e7 - 0.125 };
            // The clip animates the owner's own position, so the owner stands under a far anchor.
            const kb::scene::SceneObject farAnchor = scene.Entities().CreateObject({ .name = "Far Anchor" });
            const kb::scene::SceneObject farOwner = scene.Entities().CreateObject({ .name = "Far Rig Character" });
            const kb::scene::SceneObject farArm = scene.Entities().CreateObject({ .name = "Left Arm" });
            const kb::scene::SceneObject farUpper = scene.Entities().CreateObject({ .name = "Upper" });
            const kb::scene::SceneObject farLower = scene.Entities().CreateObject({
                .name = "Lower", .transform = kb::scene::TransformComponent{ .localPosition = { 1.0F, 0.0F, 0.0F } } });
            const kb::scene::SceneObject farHand = scene.Entities().CreateObject({
                .name = "Hand", .transform = kb::scene::TransformComponent{ .localPosition = { 1.0F, 0.0F, 0.0F } } });
            const kb::scene::SceneObject farLook = scene.Entities().CreateObject({ .name = "Look" });
            const kb::scene::SceneObject farFollower = scene.Entities().CreateObject({ .name = "Follower" });
            Require(farOwner.SetParent(farAnchor) && farArm.SetParent(farOwner) && farUpper.SetParent(farOwner) && farLower.SetParent(farUpper) &&
                    farHand.SetParent(farLower) && farLook.SetParent(farOwner) && farFollower.SetParent(farOwner),
                "Far rig hierarchy could not be authored");
            scene.Transforms().SetLocalTranslation(farAnchor.Entity(), far);
            scene.Components().Animators().Set(farOwner.Entity(), kb::scene::Animator{
                .controllerAssetId = rigMetadata->id.value,
                .speed = 1.0F,
                .enabled = true,
            });
            // A Lua script gives the far rig its targets through Animator.SetPreciseIKTarget; Lua numbers are
            // doubles and CallFunction keeps them whole for Double inputs.
            const kb::assets::AssetId farRigScript{ 0x7A2F1E00U };
            const kb::script::PucLuaLoadResult farRigLoaded = rigScriptHost.LuaRuntime().LoadScript(farRigScript, R"(function Tick(self)
    if GetShared("farRigConfigured") then return end
    local hand, handError = CallFunction("Animator.SetPreciseIKTarget", {
        name = "HandTarget", x = 10000001.25, y = 1.0, z = 9999999.875, rotationWeight = 0.0
    })
    local pole, poleError = CallFunction("Animator.SetPreciseIKTarget", {
        name = "ElbowPole", x = 10000000.25, y = 0.0, z = 10000000.875
    })
    local look, lookError = CallFunction("Animator.SetPreciseIKTarget", {
        name = "LookTarget", x = 10000000.25, y = 0.0, z = 10000004.875
    })
    local copy, copyError = CallFunction("Animator.SetPreciseIKTarget", {
        name = "CopyTarget", x = 10000003.25, y = 2.0, z = 10000000.875,
        rotationY = 0.70710678, rotationW = 0.70710678
    })
    if handError or poleError or lookError or copyError then
        SetShared("farRigError", handError or poleError or lookError or copyError)
        return
    end
    SetShared("farRigConfigured", hand and pole and look and copy)
end
)", "FarRig.lua");
            Require(farRigLoaded.succeeded && rigScriptHost.Functions().FindSignature("Animator.SetPreciseIKTarget") != nullptr,
                "The far rig script did not load");
            scene.Components().Behaviours().Set(farOwner.Entity(), kb::scene::BehaviourComponent{
                .behaviourAssetId = farRigScript.value,
                .backend = kb::scene::BehaviourBackend::Lua,
                .enabled = true,
            });
            static_cast<void>(scene.Runtime().Update(0.0F));
            static_cast<void>(scene.Runtime().Update(0.0F));
            Require(rigScriptHost.SharedState().Get("farRigConfigured").has_value() &&
                    rigScriptHost.SharedState().Get("farRigConfigured")->AsBool() &&
                    !rigScriptHost.SharedState().Get("farRigError").has_value(),
                "Double-precision IK targets set from Lua were not accepted");
            static_cast<void>(scene.Runtime().Update(0.0F));
            scene.Runtime().SynchronizeTransforms();
            Require(kb::math::Length(scene.Transforms().WorldTranslation(farHand.Entity()) - (far + kb::math::DVec3{ 1.0, 1.0, 0.0 })) <= 0.01,
                "TwoBoneIK far from the origin did not reach its double-precision target");
            const kb::scene::Vec3 farLookForward = kb::math::Rotate(
                scene.Transforms().Get(farLook.Entity()).worldRotation, kb::scene::Vec3{ 0.0F, 0.0F, 1.0F });
            Require(farLookForward.z >= 0.999F, "Aim far from the origin did not orient its Transform");
            Require(kb::math::Length(scene.Transforms().WorldTranslation(farFollower.Entity()) - (far + kb::math::DVec3{ 3.0, 2.0, 1.0 })) <= 0.001 &&
                    std::abs(scene.Transforms().Get(farFollower.Entity()).worldRotation.y - 0.70710678F) <= 0.001F,
                "CopyTransform far from the origin did not reach its double-precision target");
        }
        Require(scene.Animators().SetIkTarget(
                    rigOwner.Entity(), "HandTarget",
                    kb::scene::AnimatorIkTarget{
                        .worldPosition = {},
                        .positionWeight = 0.0F,
                        .rotationWeight = 0.0F,
                    }),
            "A disabled TwoBoneIK target could not be updated");
        static_cast<void>(scene.Runtime().Update(0.0F));
        Require(scene.Runtime().DrainSceneSystemErrors().empty(),
            "Zero-weight TwoBoneIK evaluated a degenerate target instead of remaining inactive");
        Require(!scene.Animators().SetIkTarget(
                    rigOwner.Entity(), "HandTarget",
                    kb::scene::AnimatorIkTarget{
                        .worldPosition = { 1.0F, 1.0F, 0.0F },
                        .worldRotation = { 0.0F, 0.0F, 0.0F, 0.0F },
                    }),
            "IK target accepted a zero quaternion and silently fabricated a rotation");
        Require(scene.Animators().ClearIkTarget(rigOwner.Entity(), "CopyTarget") &&
                !scene.Animators().ClearIkTarget(rigOwner.Entity(), "CopyTarget") &&
                !scene.Animators().SetIkTarget(
                    rigOwner.Entity(), "Undeclared",
                    kb::scene::AnimatorIkTarget{}),
            "IK target lifecycle accepted an undeclared or already-cleared target");
        rigController.rigConstraints[2].target = "ReloadedCopyTarget";
        Require(kb::scene::AnimationAssetIO::SaveController(
                    root / "Assets" / "Animation" / "Rig.kbanimcontroller",
                    rigController) &&
                scene.Assets().Manager().Unload(rigMetadata->id),
            "Edited rig controller could not invalidate its retained runtime asset");
        static_cast<void>(scene.Runtime().Update(0.0F));
        Require(!scene.Animators().SetIkTarget(
                    rigOwner.Entity(), "CopyTarget",
                    kb::scene::AnimatorIkTarget{}) &&
                scene.Animators().SetIkTarget(
                    rigOwner.Entity(), "ReloadedCopyTarget",
                    kb::scene::AnimatorIkTarget{
                        .worldPosition = { -2.0F, 4.0F, 1.0F },
                        .rotationWeight = 0.0F,
                    }),
            "Animator retained the pre-save rig definition after its canonical asset was invalidated");
        static_cast<void>(scene.Runtime().Update(0.0F));
        Require(kb::math::Length(
                    scene.Transforms().Get(follower.Entity()).worldPosition -
                    kb::scene::Vec3{ -2.0F, 4.0F, 1.0F }) <= 0.001F,
            "Reloaded rig definition did not drive the live runtime hierarchy");

        const kb::scene::SceneObject transitionOwner =
            scene.Entities().CreateObject({ .name = "Transition Root Motion Character" });
        const kb::scene::SceneObject transitionArm =
            scene.Entities().CreateObject({ .name = "Left Arm" });
        Require(transitionArm.SetParent(transitionOwner),
            "Transition root-motion hierarchy could not bind the controller");
        scene.Components().Animators().Set(transitionOwner.Entity(), kb::scene::Animator{
            .controllerAssetId = metadata->id.value,
            .speed = 1.0F,
            .enabled = true,
            .rootMotionOwner = kb::scene::AnimatorRootMotionOwner::Animator,
        });
        static_cast<void>(scene.Runtime().Update(0.0F));
        Require(scene.Animators().CrossFade(
                    transitionOwner.Entity(), "Root Layer", "Run State", 0.2F, 0.0F),
            "Root-motion transition could not be started through the production API");
        static_cast<void>(scene.Runtime().Update(0.2F));
        Require(NearlyEqual(
                    scene.Transforms().Get(transitionOwner.Entity()).localPosition.x, 4.0F),
            "Cross-faded root motion depended on render-frame slicing instead of integrating transition ownership");
        Require(scene.Animators().Play(
                    transitionOwner.Entity(), "Root Layer", "Walk State", 0.0F),
            "Root-motion transition could not reset its production playhead");
        kb::scene::TransformComponent* resetTransitionTransform =
            scene.Transforms().TryGet(transitionOwner.Entity());
        Require(resetTransitionTransform != nullptr,
            "Transition root-motion owner lost its Transform");
        resetTransitionTransform->localPosition = {};
        resetTransitionTransform->localRotation = {};
        scene.Transforms().MarkModified(transitionOwner.Entity());
        Require(scene.Animators().CrossFade(
                    transitionOwner.Entity(), "Root Layer", "Run State", 0.2F, 0.0F),
            "Frame-sliced root-motion transition could not be started");
        static_cast<void>(scene.Runtime().Update(0.1F));
        static_cast<void>(scene.Runtime().Update(0.1F));
        Require(NearlyEqual(
                    scene.Transforms().Get(transitionOwner.Entity()).localPosition.x, 4.0F),
            "Cross-faded root motion produced a different result across equivalent render-frame slicing");

        const float transitionPosition =
            scene.Transforms().Get(transitionOwner.Entity()).localPosition.x;
        scene.Components().Animators().Remove(transitionOwner.Entity());
        static_cast<void>(scene.Runtime().Update(0.1F));
        Require(!scene.Animators().Exists(transitionOwner.Entity()) &&
                NearlyEqual(
                    scene.Transforms().Get(transitionOwner.Entity()).localPosition.x,
                    transitionPosition),
            "Removing the authored Animator left an orphan runtime writer active");
    }

    {
        kb::scene::Scene scene;
        Require(scene.Assets().MountProject(root), "Serialized animation scene project mount failed");
        Require(scene.Assets().Discover() == 8U, "Serialized animation scene project discovery failed");
        Require(kb::scene::SceneDocumentService::LoadFileIntoScene(scene, scenePath),
            "Serialized animation scene could not be loaded into runtime");
        const auto loadedRoots = scene.Hierarchy().RootEntities();
        Require(loadedRoots.size() == 1U, "Serialized animation scene did not restore its authored root");
        const kb::scene::Animator* loadedAnimator = scene.Components().Animators().TryGet(loadedRoots.front());
        Require(loadedAnimator != nullptr && loadedAnimator->controllerAssetId != 0U &&
                NearlyEqual(loadedAnimator->speed, 1.0F) && loadedAnimator->enabled &&
                loadedAnimator->rootMotionOwner == kb::scene::AnimatorRootMotionOwner::Animator,
            "Serialized Animator component did not survive the production scene document round trip");
        kb::script::ScriptRuntimeHost host{ scene };
        Require(host.Succeeded() && host.InstallSceneSystem(), "Animator script runtime system could not be installed");
        static_cast<void>(scene.Runtime().Update(0.25F));
        static_cast<void>(scene.Runtime().Update(0.25F));
        const auto roots = scene.Hierarchy().RootEntities();
        Require(roots.size() == 1U && scene.Animators().Exists(roots.front()),
            "Serialized scene behaviour did not attach Animator through the production script path");
        Require(NearlyEqual(scene.Transforms().Get(roots.front()).localPosition.x, 17.5F),
            "Project -> scene -> Lua -> Animator -> runtime update did not apply the selected clip");
        Require(host.SharedState().Get("animationEventId").has_value(),
            "Animator event bus did not invoke the Lua subscription");
        Require(host.SharedState().Get("animationSchemaMajor").has_value() &&
                host.SharedState().Get("animationSchemaMinor").has_value() &&
                host.SharedState().Get("animationEventLayer").has_value() &&
                host.SharedState().Get("animationEventState").has_value() &&
                host.SharedState().Get("animationEventTime").has_value(),
            "Animator event bus delivered an incomplete typed payload");
        Require(host.SharedState().Get("animationEventId")->AsUInt64() == kFootstepEvent,
            "Animator event id was not preserved through the typed Lua payload");
        Require(host.SharedState().Get("animationSchemaMajor")->AsInt() == 1 &&
                host.SharedState().Get("animationSchemaMinor")->AsInt() == 0,
            "Animator event payload schema version was not preserved through Lua");
        Require(host.SharedState().Get("animationEventLayer")->AsString() == "Root Layer" &&
                host.SharedState().Get("animationEventState")->AsString() == "Run State",
            "Animator event payload state identity was not preserved through Lua");
        Require(NearlyEqual(host.SharedState().Get("animationEventTime")->AsFloat(), 0.25F),
            "Animator event payload normalized time was not preserved through Lua");
        Require(host.SharedState().Get("animationEventMutatedRuntime").has_value() &&
                host.SharedState().Get("animationEventMutatedRuntime")->AsBool() &&
                !host.SharedState().Get("animationEventError").has_value(),
            "Animation event callback could not safely call the production Animator API");
        Require(!host.SharedState().Get("animatorError").has_value(),
            "Animator Lua integration surfaced an attachment error");
    }

    if (!std::filesystem::path{ KB_PHYSICS_JOLT_PLUGIN_PATH }.empty()) {
        kb::project::ProjectDescriptor descriptor;
        descriptor.disableEnginePluginsByDefault = true;
        descriptor.plugins.push_back(kb::project::ProjectPluginReference{
            .name = "Physics.Jolt",
            .binaryPath = KB_PHYSICS_JOLT_PLUGIN_PATH,
            .enabled = true,
        });
        kb::scene::Scene scene{ std::move(descriptor) };
        kb::scene::PhysicsBackend::SetStepPipelining(scene, false); // the test reads the pose in the update that stepped it
        Require(scene.Assets().MountProject(root), "Root-motion Jolt project mount failed");
        Require(scene.Assets().Discover() == 8U, "Root-motion Jolt assets were not discovered");
        const auto* metadata =
            scene.Assets().Manager().Registry().FindByPath("/Game/Animation/Character.kbanimcontroller");
        Require(metadata != nullptr, "Root-motion Jolt controller metadata was not found");

        const kb::scene::SceneObject character =
            scene.Entities().CreateObject({ .name = "Character Root Motion" });
        const kb::scene::SceneObject characterArm =
            scene.Entities().CreateObject({ .name = "Left Arm" });
        Require(characterArm.SetParent(character),
            "Character root-motion hierarchy could not bind the controller");
        scene.Components().CharacterControllers().Set(
            character.Entity(), kb::scene::CharacterControllerComponent{});
        scene.Components().Animators().Set(character.Entity(), kb::scene::Animator{
            .controllerAssetId = metadata->id.value,
            .speed = 1.0F,
            .enabled = true,
            .rootMotionOwner = kb::scene::AnimatorRootMotionOwner::CharacterController,
        });

        const kb::scene::SceneObject body = scene.Entities().CreateObject({
            .name = "Rigidbody Root Motion",
            .transform = kb::scene::TransformComponent{ .localPosition = { 0.0F, 0.0F, 5.0F } },
        });
        const kb::scene::SceneObject bodyArm =
            scene.Entities().CreateObject({ .name = "Left Arm" });
        Require(bodyArm.SetParent(body),
            "Rigidbody root-motion hierarchy could not bind the controller");
        scene.Components().Rigidbodies().Set(body.Entity(), kb::scene::RigidbodyComponent{
            .bodyType = kb::scene::RigidbodyBodyType::Kinematic,
            .useGravity = false,
        });
        scene.Components().Colliders().Set(body.Entity(), kb::scene::ColliderComponent{});
        scene.Components().Animators().Set(body.Entity(), kb::scene::Animator{
            .controllerAssetId = metadata->id.value,
            .speed = 1.0F,
            .enabled = true,
            .rootMotionOwner = kb::scene::AnimatorRootMotionOwner::Rigidbody,
        });
        const auto* turnMetadata =
            scene.Assets().Manager().Registry().FindByPath("/Game/Animation/Turn.kbanimcontroller");
        Require(turnMetadata != nullptr, "Root-motion Jolt rotation controller metadata was not found");
        const kb::scene::SceneObject turningBody =
            scene.Entities().CreateObject({
                .name = "Turning Rigidbody Root Motion",
                .transform = kb::scene::TransformComponent{ .localPosition = { 0.0F, 0.0F, 10.0F } },
            });
        scene.Components().Rigidbodies().Set(turningBody.Entity(), kb::scene::RigidbodyComponent{
            .bodyType = kb::scene::RigidbodyBodyType::Kinematic,
            .useGravity = false,
        });
        scene.Components().Colliders().Set(turningBody.Entity(), kb::scene::ColliderComponent{});
        scene.Components().Animators().Set(turningBody.Entity(), kb::scene::Animator{
            .controllerAssetId = turnMetadata->id.value,
            .speed = 1.0F,
            .enabled = true,
            .rootMotionOwner = kb::scene::AnimatorRootMotionOwner::Rigidbody,
        });

        scene.Runtime().SetPlaying(true);
        for (int step = 0; step < 12; ++step) {
            static_cast<void>(scene.Runtime().Update(1.0F / 60.0F));
        }
        Require(std::abs(scene.Transforms().Get(character.Entity()).localPosition.x - 2.0F) <= 0.08F,
            "CharacterController-owned root motion did not cross Animator -> real Jolt character -> Transform");
        Require(std::abs(scene.Transforms().Get(body.Entity()).localPosition.x - 2.0F) <= 0.08F,
            "Rigidbody-owned root motion did not cross Animator -> real Jolt body -> Transform");
        Require(std::abs(scene.Transforms().Get(turningBody.Entity()).localRotation.y - 0.15643447F) <= 0.01F,
            "Rigidbody-owned root rotation did not cross Animator -> real Jolt kinematic body -> Transform");
        static_cast<void>(scene.Runtime().Update(0.05F));
        const float characterAfterSubsteps =
            scene.Transforms().Get(character.Entity()).localPosition.x;
        const float bodyAfterSubsteps =
            scene.Transforms().Get(body.Entity()).localPosition.x;
        const float expectedAfterSubsteps = 2.0F +
            static_cast<float>(scene.Runtime().LastFixedStepCount()) * (10.0F / 60.0F);
        const std::string substepFailure =
            "Physics-owned root motion repeated or dropped its frame delta across multiple fixed substeps: character=" +
            std::to_string(characterAfterSubsteps) + ", body=" + std::to_string(bodyAfterSubsteps);
        Require(std::abs(characterAfterSubsteps - expectedAfterSubsteps) <= 0.03F &&
                std::abs(bodyAfterSubsteps - expectedAfterSubsteps) <= 0.03F,
            substepFailure.c_str());

        const kb::scene::SceneObject slicedBody = scene.Entities().CreateObject({
            .name = "Fixed-sliced Rigidbody Root Motion",
            .transform = kb::scene::TransformComponent{ .localPosition = { 0.0F, 0.0F, 15.0F } },
        });
        const kb::scene::SceneObject slicedBodyArm =
            scene.Entities().CreateObject({ .name = "Left Arm" });
        Require(slicedBodyArm.SetParent(slicedBody),
            "Fixed-sliced root-motion hierarchy could not bind the controller");
        scene.Components().Rigidbodies().Set(slicedBody.Entity(), kb::scene::RigidbodyComponent{
            .bodyType = kb::scene::RigidbodyBodyType::Kinematic,
            .useGravity = false,
        });
        scene.Components().Colliders().Set(slicedBody.Entity(), kb::scene::ColliderComponent{});
        scene.Components().Animators().Set(slicedBody.Entity(), kb::scene::Animator{
            .controllerAssetId = metadata->id.value,
            .speed = 1.0F,
            .enabled = true,
            .rootMotionOwner = kb::scene::AnimatorRootMotionOwner::Rigidbody,
        });
        scene.Runtime().SetFixedStepSettings(kb::scene::SceneRuntimeFixedStepSettings{
            .fixedDeltaSeconds = 1.0F / 60.0F,
            .maxFrameDeltaSeconds = 0.25F,
            .maxFixedStepsPerFrame = 1U,
        });
        static_cast<void>(scene.Runtime().Update(0.05F));
        Require(std::abs(
                    scene.Transforms().Get(slicedBody.Entity()).localPosition.x -
                    (1.0F / 6.0F)) <= 0.02F,
            "Rigidbody root motion burst the full variable-frame delta into one fixed substep");
        const auto* moveMetadata =
            scene.Assets().Manager().Registry().FindByPath(
                "/Game/Animation/Move.kbanim");
        Require(moveMetadata != nullptr,
            "Physics-owned root-motion clip metadata was not found");
        kb::scene::AnimationClip editedMoveClip = clip;
        editedMoveClip.tracks[0].keyframes[1].transform.position.x = 50.0F;
        Require(kb::scene::AnimationAssetIO::SaveClip(
                    clipPath, editedMoveClip) &&
                scene.Assets().Manager().Unload(moveMetadata->id),
            "Physics-owned root-motion clip could not be invalidated");
        const float slicedBeforeClipReload =
            scene.Transforms().Get(slicedBody.Entity()).localPosition.x;
        static_cast<void>(scene.Runtime().Update(1.0F / 60.0F));
        Require(std::abs(
                    scene.Transforms().Get(slicedBody.Entity()).localPosition.x -
                    slicedBeforeClipReload - (50.0F / 60.0F)) <= 0.03F,
            "Jolt consumed queued root motion from the pre-save clip source");
        Require(kb::scene::AnimationAssetIO::SaveClip(clipPath, clip) &&
                scene.Assets().Manager().Unload(moveMetadata->id),
            "Physics-owned root-motion fixture could not be restored");
        const float slicedBeforeOwnerChange =
            scene.Transforms().Get(slicedBody.Entity()).localPosition.x;
        scene.Entities().SetActive(slicedBody.Entity(), false);
        static_cast<void>(scene.Runtime().Update(1.0F / 60.0F));
        Require(std::abs(
                    scene.Transforms().Get(slicedBody.Entity()).localPosition.x -
                    slicedBeforeOwnerChange) <= 0.01F,
            "Jolt consumed queued root motion after its authoritative entity became inactive");
        scene.Entities().SetActive(slicedBody.Entity(), true);
        kb::scene::Animator* slicedAnimator =
            scene.Components().Animators().TryGet(slicedBody.Entity());
        Require(slicedAnimator != nullptr,
            "Fixed-sliced root-motion Animator disappeared before ownership invalidation");
        slicedAnimator->rootMotionOwner =
            kb::scene::AnimatorRootMotionOwner::Animator;
        scene.Components().Animators().MarkModified(slicedBody.Entity());
        static_cast<void>(scene.Runtime().Update(1.0F / 60.0F));
        Require(std::abs(
                    scene.Transforms().Get(slicedBody.Entity()).localPosition.x -
                    slicedBeforeOwnerChange) <= 0.01F,
            "Jolt consumed stale queued root motion after the authoritative owner changed");
        slicedAnimator = scene.Components().Animators().TryGet(slicedBody.Entity());
        Require(slicedAnimator != nullptr,
            "Fixed-sliced root-motion Animator disappeared after ownership change");
        slicedAnimator->enabled = false;
        scene.Components().Animators().MarkModified(slicedBody.Entity());
        static_cast<void>(scene.Runtime().DrainSceneSystemErrors());

        const kb::scene::SceneObject tiltedCharacter = scene.Entities().CreateObject({
            .name = "Tilted Character Root Motion",
            .transform = kb::scene::TransformComponent{
                .localRotation = { 0.0F, 0.0F, 0.70710678F, 0.70710678F },
            },
        });
        const kb::scene::SceneObject tiltedArm =
            scene.Entities().CreateObject({ .name = "Left Arm" });
        Require(tiltedArm.SetParent(tiltedCharacter),
            "Tilted character root-motion hierarchy could not bind the controller");
        scene.Components().CharacterControllers().Set(
            tiltedCharacter.Entity(), kb::scene::CharacterControllerComponent{});
        scene.Components().Animators().Set(tiltedCharacter.Entity(), kb::scene::Animator{
            .controllerAssetId = metadata->id.value,
            .speed = 1.0F,
            .enabled = true,
            .rootMotionOwner = kb::scene::AnimatorRootMotionOwner::CharacterController,
        });
        static_cast<void>(scene.Runtime().Update(1.0F / 60.0F));
        const std::vector<std::string> rootMotionErrors =
            scene.Runtime().DrainSceneSystemErrors();
        Require(std::any_of(
                    rootMotionErrors.begin(), rootMotionErrors.end(),
                    [](const std::string& error) {
                        return error.find("world-planar") != std::string::npos;
                    }),
            "CharacterController root motion accepted local-planar motion that was vertical in world space");
        kb::scene::Animator* tiltedAnimator =
            scene.Components().Animators().TryGet(tiltedCharacter.Entity());
        Require(tiltedAnimator != nullptr,
            "Tilted CharacterController Animator disappeared after validation");
        tiltedAnimator->enabled = false;
        scene.Components().Animators().MarkModified(tiltedCharacter.Entity());

        const kb::scene::SceneObject conflictingBody = scene.Entities().CreateObject({
            .name = "Conflicting Rigidbody Root Motion",
            .transform = kb::scene::TransformComponent{ .localPosition = { 0.0F, 0.0F, 20.0F } },
        });
        const kb::scene::SceneObject conflictingArm =
            scene.Entities().CreateObject({ .name = "Left Arm" });
        Require(conflictingArm.SetParent(conflictingBody),
            "Conflicting root-motion hierarchy could not bind the controller");
        scene.Components().CharacterControllers().Set(
            conflictingBody.Entity(), kb::scene::CharacterControllerComponent{});
        scene.Components().Rigidbodies().Set(conflictingBody.Entity(), kb::scene::RigidbodyComponent{
            .bodyType = kb::scene::RigidbodyBodyType::Kinematic,
            .useGravity = false,
        });
        scene.Components().Colliders().Set(conflictingBody.Entity(), kb::scene::ColliderComponent{});
        scene.Components().Animators().Set(conflictingBody.Entity(), kb::scene::Animator{
            .controllerAssetId = metadata->id.value,
            .speed = 1.0F,
            .enabled = true,
            .rootMotionOwner = kb::scene::AnimatorRootMotionOwner::Rigidbody,
        });
        static_cast<void>(scene.Runtime().Update(1.0F / 60.0F));
        const std::vector<std::string> ownershipErrors =
            scene.Runtime().DrainSceneSystemErrors();
        Require(std::any_of(
                    ownershipErrors.begin(), ownershipErrors.end(),
                    [](const std::string& error) {
                        return error.find("exclusive kinematic Rigidbody") != std::string::npos;
                    }),
            "Runtime accepted CharacterController and Rigidbody as competing Transform authorities");
    }
    {
        kb::scene::Scene scene;
        Require(scene.Assets().MountProject(root) && scene.Assets().Discover() == 8U,
            "Animation rediscovery test could not mount and discover the project");
        const auto* controllerMetadata = scene.Assets().Manager().Registry().FindByPath(
            "/Game/Animation/Character.kbanimcontroller");
        const auto* runMetadata = scene.Assets().Manager().Registry().FindByPath(
            "/Game/Animation/Run Fast.kbanim");
        Require(controllerMetadata != nullptr && runMetadata != nullptr,
            "Animation rediscovery test did not find its controller and clip");
        const kb::assets::AssetId controllerId = controllerMetadata->id;
        const kb::assets::AssetId runId = runMetadata->id;
        const kb::scene::SceneObject owner = scene.Entities().CreateObject({ .name = "Rediscovery Character" });
        const kb::scene::SceneObject arm = scene.Entities().CreateObject({ .name = "Left Arm" });
        Require(arm.SetParent(owner),
            "Animation rediscovery hierarchy could not be authored");
        scene.Components().Animators().Set(owner.Entity(), kb::scene::Animator{
            .controllerAssetId = controllerId.value,
            .speed = 1.0F,
            .enabled = true,
        });
        Require(scene.Components().Animators().TryGet(owner.Entity()) != nullptr,
            "Animation rediscovery Animator could not be authored");
        scene.Runtime().SetPlaying(true);
        static_cast<void>(scene.Runtime().Update(0.0F));
        Require(scene.Animators().Play(owner.Entity(), "Root Layer", "Run State", 0.2F),
            "Animation rediscovery test could not select its live state");
        const auto stateBefore = scene.Animators().State(owner.Entity(), "Root Layer");
        const std::uint64_t runGeneration = scene.Assets().Manager().LoadGeneration(runId);
        const std::uint64_t controllerGeneration = scene.Assets().Manager().LoadGeneration(controllerId);

        kb::scene::AnimationClip reimportedRunClip = runClip;
        reimportedRunClip.tracks[0].keyframes[1].transform.position.x = 50.0F;
        reimportedRunClip.tracks[1].keyframes[1].transform.position.x = 60.0F;
        Require(kb::scene::AnimationAssetIO::SaveClip(runClipPath, reimportedRunClip) &&
                scene.Assets().Discover() == 8U,
            "Animation clip reimport was not discovered through the production asset pipeline");
        const auto* reimportedController = scene.Assets().Manager().Registry().FindByPath(
            "/Game/Animation/Character.kbanimcontroller");
        const auto* reimportedRun = scene.Assets().Manager().Registry().FindByPath(
            "/Game/Animation/Run Fast.kbanim");
        Require(reimportedController != nullptr && reimportedController->id == controllerId &&
                reimportedRun != nullptr && reimportedRun->id == runId,
            "Animation reimport changed a stable controller or clip reference");
        Require(scene.Assets().Manager().LoadGeneration(runId) > runGeneration &&
                scene.Assets().Manager().LoadGeneration(controllerId) > controllerGeneration,
            "Animation reimport did not invalidate the clip's dependent controller closure");
        static_cast<void>(scene.Runtime().Update(0.0F));
        const auto stateAfter = scene.Animators().State(owner.Entity(), "Root Layer");
        Require(stateBefore.has_value() && stateAfter.has_value() &&
                stateAfter->state == stateBefore->state &&
                NearlyEqual(stateAfter->normalizedTime, stateBefore->normalizedTime),
            "Animation hot reload lost its compatible live state and playhead");
        static_cast<void>(scene.Runtime().Update(0.1F));
        Require(scene.Transforms().Get(owner.Entity()).localPosition.x > 10.0F,
            "Animation hot reload retained the stale clip payload after rediscovery");
        Require(kb::scene::AnimationAssetIO::SaveClip(runClipPath, runClip),
            "Animation rediscovery fixture could not restore its canonical clip");
    }
    {
        const std::filesystem::path skeletalRoot =
            std::filesystem::temp_directory_path() /
            "21kb-animator-instance-tests";
        std::filesystem::remove_all(skeletalRoot);

        kb::scene::SkeletonAsset skeleton{};
        skeleton.bones = {
            {
                .id = 700U,
                .parentIndex = -1,
                .name = "Root",
                .referencePose = {
                    .position = { 3.0F, 0.0F, 0.0F },
                    .scale = { 2.0F, 2.0F, 2.0F },
                },
                .inverseBind = {},
            },
            {
                .id = 101U,
                .parentIndex = 0,
                .name = "Spine",
                .referencePose = {
                    .position = { 0.0F, 1.0F, 0.0F },
                },
                .inverseBind = {},
            },
            {
                .id = 900U,
                .parentIndex = 1,
                .name = "Hand",
                .referencePose = {
                    .position = { 1.0F, 0.0F, 0.0F },
                },
                .inverseBind = { .columns = {
                    { 1.0F, 0.0F, 0.0F, 0.0F },
                    { 0.0F, 1.0F, 0.0F, 0.0F },
                    { 0.0F, 0.0F, 1.0F, 0.0F },
                    { -1.0F, 0.0F, 0.0F, 1.0F },
                } },
            },
        };
        skeleton.sockets = {
            {
                .name = "Weapon",
                .boneId = 900U,
                .localTransform = { .position = { 0.0F, 0.5F, 0.0F } },
            },
        };
        const std::uint64_t skeletonSignature =
            kb::scene::SkeletonCompatibilitySignature(skeleton);
        const std::filesystem::path skeletonPath = skeletalRoot / "Assets" /
            "Skeletal" / "RuntimeRig.kbskeleton";
        Require(skeletonSignature != 0U &&
                kb::scene::SkeletonAssetIO::Save(skeletonPath, skeleton),
            "AnimatorInstance fixture could not save its canonical Skeleton");

        kb::scene::Scene scene;
        Require(scene.Assets().MountProject(skeletalRoot) &&
                scene.Assets().Discover() == 1U,
            "AnimatorInstance fixture could not discover its Skeleton");
        const kb::assets::AssetMetadata* skeletonMetadata =
            scene.Assets().Manager().Registry().FindByPath(
                "/Game/Skeletal/RuntimeRig.kbskeleton");
        Require(skeletonMetadata != nullptr,
            "AnimatorInstance fixture did not retain its Skeleton metadata");
        const kb::assets::AssetId skeletonId = skeletonMetadata->id;

        kb::scene::AnimationClip instanceClip{};
        instanceClip.durationSeconds = 1.0F;
        instanceClip.looping = true;
        instanceClip.targetSkeletonAssetId = skeletonId.value;
        instanceClip.targetSkeletonCompatibilitySignature = skeletonSignature;
        instanceClip.skeletalTracks = {
            {
                .boneId = 900U,
                .bindingMask = 1U,
                .keyframes = {
                    { .timeSeconds = 0.0F, .transform = {} },
                    { .timeSeconds = 1.0F, .transform = {
                        .position = { 2.0F, 0.0F, 0.0F },
                    } },
                },
            },
            {
                .boneId = 700U,
                .bindingMask = 2U,
                .keyframes = {
                    { .timeSeconds = 0.0F, .transform = {} },
                    { .timeSeconds = 1.0F, .transform = {
                        .position = { 0.0F, 0.0F, 1.0F },
                    } },
                },
            },
        };
        instanceClip.morphTracks = {
            { .morphTarget = "Smile", .keyframes = {
                { .timeSeconds = 0.0F, .weight = 0.0F },
                { .timeSeconds = 1.0F, .weight = 1.0F },
            } },
        };
        instanceClip.rootMotionMode =
            kb::scene::AnimationRootMotionMode::ExtractFromBone;
        instanceClip.rootMotionBoneId = 700U;
        const std::filesystem::path instanceClipPath = skeletalRoot /
            "Assets" / "Animation" / "Skeletal.kbanim";
        kb::scene::AnimationClip instanceClipB = instanceClip;
        instanceClipB.skeletalTracks[0].keyframes[1].transform.position.x =
            6.0F;
        const std::filesystem::path instanceClipBPath = skeletalRoot /
            "Assets" / "Animation" / "SkeletalB.kbanim";
        Require(kb::scene::AnimationAssetIO::SaveClip(
                    instanceClipPath, instanceClip) &&
                kb::scene::AnimationAssetIO::SaveClip(
                    instanceClipBPath, instanceClipB),
            "AnimatorInstance fixture could not save its skeletal clips");

        kb::scene::AnimatorController instanceController{};
        instanceController.parameters = {
            {
                .name = "Blend",
                .type = kb::scene::AnimatorParameterType::Float,
                .floatDefault = 0.5F,
            },
        };
        instanceController.layers = {
            {
                .name = "Base",
                .defaultState = "Blend",
                .weight = 1.0F,
                .mask = 3U,
                .states = {
                    {
                        .name = "Blend",
                        .blendParameter = "Blend",
                        .blendChildren = {
                            {
                                .threshold = 0.0F,
                                .clipReference =
                                    "/Game/Animation/Skeletal.kbanim",
                            },
                            {
                                .threshold = 1.0F,
                                .clipReference =
                                    "/Game/Animation/SkeletalB.kbanim",
                            },
                        },
                    },
                    {
                        .name = "DirectB",
                        .clipReference =
                            "/Game/Animation/SkeletalB.kbanim",
                    },
                },
            },
            {
                .name = "Root",
                .defaultState = "Root",
                .weight = 0.5F,
                .mask = 2U,
                .states = {
                    {
                        .name = "Root",
                        .clipReference =
                            "/Game/Animation/Skeletal.kbanim",
                    },
                },
            },
        };
        const std::filesystem::path instanceControllerPath = skeletalRoot /
            "Assets" / "Animation" / "Skeletal.kbanimcontroller";
        Require(kb::scene::AnimationAssetIO::SaveController(
                    instanceControllerPath, instanceController) &&
                scene.Assets().Discover() == 4U,
            "AnimatorInstance fixture could not discover its clip and controller");
        const std::string instanceControllerBeforeDebugSnapshot =
            ReadTextFile(instanceControllerPath);
        const kb::assets::AssetMetadata* instanceControllerMetadata =
            scene.Assets().Manager().Registry().FindByPath(
                "/Game/Animation/Skeletal.kbanimcontroller");
        Require(instanceControllerMetadata != nullptr,
            "AnimatorInstance fixture did not retain controller metadata");

        const kb::scene::SceneObject referenceOwner =
            scene.Entities().CreateObject({ .name = "Reference Pose Character" });
        Require(scene.Components().SkeletonBindings().Set(
                    referenceOwner.Entity(), kb::scene::SkeletonBindingComponent{
                        .skeletonAssetId = skeletonId.value,
                        .skeletonCompatibilitySignature = skeletonSignature,
                        .enabled = true,
                    }),
            "SkeletonBinding reference-pose fixture could not author its binding");
        scene.Runtime().SetPlaying(false);
        static_cast<void>(scene.Runtime().Update(0.0F));
        const auto referenceSkeleton =
            scene.Animators().InstanceSkeleton(referenceOwner.Entity());
        Require(scene.Runtime().DrainSceneSystemErrors().empty() &&
                !scene.Animators().Exists(referenceOwner.Entity()) &&
                scene.Animators().RuntimeBindingGeneration(referenceOwner.Entity()) != 0U &&
                referenceSkeleton.has_value() &&
                referenceSkeleton->skeletonAssetId == skeletonId.value &&
                referenceSkeleton->compatibilitySignature == skeletonSignature &&
                referenceSkeleton->boneIds.size() == 3U &&
                referenceSkeleton->currentComponentPose.positions.size() == 3U &&
                referenceSkeleton->currentSkinMatrices.size() == 3U &&
                referenceSkeleton->previousSkinMatrices.size() == 3U,
            "SkeletonBinding must publish a complete reference pose without an Animator");

        const kb::scene::SceneObject owner =
            scene.Entities().CreateObject({ .name = "Skeletal Character" });
        Require(scene.Components().SkeletonBindings().Set(
                    owner.Entity(), kb::scene::SkeletonBindingComponent{
                        .skeletonAssetId = skeletonId.value,
                        .skeletonCompatibilitySignature = skeletonSignature,
                        .enabled = true,
                    }),
            "AnimatorInstance fixture could not author SkeletonBinding");
        scene.Components().Animators().Set(owner.Entity(), kb::scene::Animator{
            .controllerAssetId = instanceControllerMetadata->id.value,
            .speed = 1.0F,
            .enabled = true,
        });
        Require(scene.Components().DeformedGeometries().Set(owner.Entity(),
                    kb::scene::DrawD3DeformedGeometryComponent{
                        .skeletalMeshAssetId = 0xD1A60001U,
                        .enabled = true,
                    }),
            "Animator debug fixture could not author an incompatible deformed geometry asset");
        scene.Runtime().SetPlaying(false);
        static_cast<void>(scene.Runtime().Update(0.0F));
        const auto instanceSkeleton =
            scene.Animators().InstanceSkeleton(owner.Entity());
        const auto completePose = [](const kb::scene::AnimatorPoseSoaView& pose) {
            return pose.positions.size() == 3U &&
                pose.rotations.size() == 3U && pose.scales.size() == 3U;
        };
        Require(scene.Runtime().DrainSceneSystemErrors().empty() &&
                scene.Animators().Exists(owner.Entity()) &&
                scene.Animators().RuntimeBindingGeneration(owner.Entity()) !=
                    0U &&
                instanceSkeleton.has_value() &&
                instanceSkeleton->skeletonAssetId == skeletonId.value &&
                instanceSkeleton->compatibilitySignature ==
                    skeletonSignature &&
                instanceSkeleton->boneIds.size() == 3U &&
                instanceSkeleton->boneIds[0] == 700U &&
                instanceSkeleton->boneIds[1] == 101U &&
                instanceSkeleton->boneIds[2] == 900U &&
                completePose(instanceSkeleton->currentLocalPose) &&
                completePose(instanceSkeleton->previousLocalPose) &&
                completePose(instanceSkeleton->currentComponentPose) &&
                completePose(instanceSkeleton->previousComponentPose) &&
                instanceSkeleton->currentSkinMatrices.size() == 3U &&
                instanceSkeleton->previousSkinMatrices.size() == 3U &&
                instanceSkeleton->currentSkinMatrices.data() !=
                instanceSkeleton->previousSkinMatrices.data() &&
                instanceSkeleton->morphWeights.targetNames.size() == 1U &&
                instanceSkeleton->morphWeights.targetNames[0] == "Smile" &&
                instanceSkeleton->morphWeights.currentWeights.size() == 1U &&
                instanceSkeleton->morphWeights.previousWeights.size() == 1U &&
                instanceSkeleton->evaluationCount == 0U &&
                instanceSkeleton->hierarchySolveCount == 0U &&
                NearlyEqual(
                    instanceSkeleton->currentLocalPose.positions[2].x,
                    1.0F) &&
                NearlyEqual(
                    instanceSkeleton->currentComponentPose.positions[1].x,
                    3.0F) &&
                NearlyEqual(
                    instanceSkeleton->currentComponentPose.positions[1].y,
                    2.0F) &&
                NearlyEqual(
                    instanceSkeleton->currentComponentPose.positions[2].x,
                    5.0F) &&
                NearlyEqual(
                    instanceSkeleton->currentComponentPose.positions[2].y,
                    2.0F) &&
                instanceSkeleton->currentLocalPose.positions.data() !=
                    instanceSkeleton->previousLocalPose.positions.data() &&
                instanceSkeleton->currentComponentPose.positions.data() !=
                    instanceSkeleton->previousComponentPose.positions.data() &&
                scene.Entities().Count() == 2U,
            "AnimatorInstance did not derive contiguous double-buffered local/component SoA poses without bone entities");

        scene.Runtime().SetPlaying(true);
        static_cast<void>(scene.Runtime().Update(0.25F));
        const auto sampledSkeleton =
            scene.Animators().InstanceSkeleton(owner.Entity());
        const auto attachment = scene.Animators().AttachmentTransform(
            owner.Entity(), 900U,
            kb::scene::LocalTransform{ .position = { 0.0F, 0.5F, 0.0F } });
        const auto socket = scene.Animators().SocketTransform(
            owner.Entity(), "Weapon");
        const kb::scene::TransformComponent& ownerTransform =
            scene.Transforms().Get(owner.Entity());
        const kb::math::Vec4 sampledSkinOrigin = sampledSkeleton.has_value() &&
                sampledSkeleton->currentSkinMatrices.size() == 3U
            ? sampledSkeleton->currentSkinMatrices[2] *
                kb::math::Vec4{ 0.0F, 0.0F, 0.0F, 1.0F }
            : kb::math::Vec4{};
        Require(scene.Runtime().DrainSceneSystemErrors().empty() &&
                sampledSkeleton.has_value() &&
                sampledSkeleton->evaluationCount == 1U &&
                sampledSkeleton->hierarchySolveCount == 1U &&
                sampledSkeleton->morphWeights.targetNames.size() == 1U &&
                sampledSkeleton->morphWeights.targetNames[0] == "Smile" &&
                NearlyEqual(sampledSkeleton->morphWeights.currentWeights[0], 0.25F) &&
                NearlyEqual(sampledSkeleton->morphWeights.previousWeights[0], 0.0F) &&
                attachment.has_value() && socket.has_value() &&
                NearlyEqual(attachment->componentSpace.position.x, 1.0F) &&
                NearlyEqual(attachment->componentSpace.position.y, 1.5F) &&
                NearlyEqual(attachment->componentSpace.position.z, 0.25F) &&
                NearlyEqual(socket->componentSpace.position.x, attachment->componentSpace.position.x) &&
                NearlyEqual(socket->componentSpace.position.y, attachment->componentSpace.position.y) &&
                NearlyEqual(socket->componentSpace.position.z, attachment->componentSpace.position.z) &&
                NearlyEqual(socket->worldSpace.position.x, attachment->componentSpace.position.x) &&
                !scene.Animators().SocketTransform(owner.Entity(), "Missing").has_value() &&
                NearlyEqual(
                    sampledSkeleton->previousLocalPose.positions[0].x,
                    3.0F) &&
                NearlyEqual(
                    sampledSkeleton->currentLocalPose.positions[0].x,
                    0.0F) &&
                NearlyEqual(
                    sampledSkeleton->currentLocalPose.positions[0].z,
                    0.25F) &&
                NearlyEqual(
                    sampledSkeleton->currentLocalPose.positions[2].x,
                    1.0F) &&
                NearlyEqual(
                    sampledSkeleton->currentComponentPose.positions[2].x,
                    1.0F) &&
                NearlyEqual(
                    sampledSkeleton->currentComponentPose.positions[2].y,
                    1.0F) &&
                NearlyEqual(
                    sampledSkeleton->currentComponentPose.positions[2].z,
                    0.25F) &&
                NearlyEqual(sampledSkinOrigin.x, 0.0F) &&
                NearlyEqual(sampledSkinOrigin.y, 1.0F) &&
                NearlyEqual(sampledSkinOrigin.z, 0.25F) &&
                NearlyEqual(sampledSkinOrigin.w, 1.0F) &&
                NearlyEqual(ownerTransform.localPosition.x, 0.0F) &&
                NearlyEqual(ownerTransform.localPosition.y, 0.0F) &&
                NearlyEqual(ownerTransform.localPosition.z, 0.0F) &&
                scene.Entities().Count() == 2U,
            "Indexed skeletal sampling did not write the compact pose independently of SceneEntity Transform storage");

        Require(scene.Animators().CrossFade(
                    owner.Entity(), "Base", "DirectB", 0.5F, 0.0F),
            "Compact skeletal controller could not start a named transition");
        static_cast<void>(scene.Runtime().Update(0.25F));
        const auto transitionedSkeleton =
            scene.Animators().InstanceSkeleton(owner.Entity());
        const auto transitionedState =
            scene.Animators().State(owner.Entity(), "Base");
        const kb::math::Vec4 transitionedSkinOrigin =
            transitionedSkeleton.has_value() &&
                transitionedSkeleton->currentSkinMatrices.size() == 3U
            ? transitionedSkeleton->currentSkinMatrices[2] *
                kb::math::Vec4{ 0.0F, 0.0F, 0.0F, 1.0F }
            : kb::math::Vec4{};
        Require(scene.Runtime().DrainSceneSystemErrors().empty() &&
                transitionedSkeleton.has_value() &&
                transitionedState.has_value() &&
                transitionedSkeleton->evaluationCount == 2U &&
                transitionedSkeleton->hierarchySolveCount == 2U &&
                transitionedState->state == "DirectB" &&
                transitionedState->previousState == "Blend" &&
                transitionedState->transitioning &&
                NearlyEqual(transitionedState->transitionProgress, 0.5F) &&
                NearlyEqual(
                    transitionedSkeleton->previousLocalPose.positions[2].x,
                    1.0F) &&
                NearlyEqual(
                    transitionedSkeleton->currentLocalPose.positions[2].x,
                    1.75F) &&
                NearlyEqual(
                    transitionedSkeleton->currentLocalPose.positions[0].x,
                    0.0F) &&
                NearlyEqual(
                    transitionedSkeleton->currentLocalPose.positions[0].z,
                    0.4375F) &&
                NearlyEqual(
                    transitionedSkeleton->currentComponentPose.positions[2].x,
                    1.75F) &&
                NearlyEqual(
                    transitionedSkeleton->currentComponentPose.positions[2].y,
                    1.0F) &&
                NearlyEqual(
                    transitionedSkeleton->currentComponentPose.positions[2].z,
                    0.4375F) &&
                NearlyEqual(transitionedSkinOrigin.x, 0.75F) &&
                NearlyEqual(transitionedSkinOrigin.y, 1.0F) &&
                NearlyEqual(transitionedSkinOrigin.z, 0.4375F) &&
                NearlyEqual(transitionedSkinOrigin.w, 1.0F),
            "Compact skeletal state/layer/mask/blend/transition evaluation diverged from controller semantics");

        const std::shared_ptr<const kb::scene::AnimatorDebugSnapshot> transitionedDebug =
            scene.Animators().DebugSnapshot();
        const kb::scene::AnimatorDebugInstanceSnapshot* transitionedDebugInstance =
            transitionedDebug == nullptr ? nullptr : transitionedDebug->Find(owner.Entity());
        Require(transitionedDebug != nullptr && transitionedDebug->revision != 0U &&
                transitionedDebugInstance != nullptr &&
                transitionedDebugInstance->controllerAssetId == scene.Animators().Controller(owner.Entity()) &&
                transitionedDebugInstance->parameters.size() == 1U &&
                transitionedDebugInstance->parameters.front().name == "Blend" &&
                transitionedDebugInstance->parameters.front().type == kb::scene::AnimatorParameterType::Float &&
                NearlyEqual(transitionedDebugInstance->parameters.front().floatValue, 0.5F) &&
                transitionedDebugInstance->layers.size() == 2U &&
                transitionedDebugInstance->layers.front().name == "Base" &&
                transitionedDebugInstance->layers.front().activeState == "DirectB" &&
                transitionedDebugInstance->layers.front().previousState == "Blend" &&
                transitionedDebugInstance->layers.front().transitioning &&
                NearlyEqual(transitionedDebugInstance->layers.front().weight, 1.0F) &&
                NearlyEqual(static_cast<float>(transitionedDebugInstance->layers.front().elapsedSeconds), 0.25F) &&
                NearlyEqual(transitionedDebugInstance->layers.front().transitionProgress, 0.5F) &&
                transitionedDebugInstance->bones.size() == 3U &&
                transitionedDebugInstance->paletteMatrixCount == 3U &&
                !transitionedDebugInstance->compatibilityDiagnostics.empty() &&
                transitionedDebugInstance->poseEvaluationCount == 2U,
            "Animator debug snapshot did not retain the evaluated live skeletal state");
        Require(ReadTextFile(instanceControllerPath) ==
                instanceControllerBeforeDebugSnapshot,
            "Animator debug snapshot serialized live debug state into the controller asset");

        scene.Components().Animators().Set(
            owner.Entity(), kb::scene::Animator{
                .controllerAssetId =
                    scene.Animators().Controller(owner.Entity()),
                .speed = 1.0F,
                .enabled = true,
                .rootMotionOwner =
                    kb::scene::AnimatorRootMotionOwner::Animator,
            });
        static_cast<void>(scene.Runtime().Update(0.25F));
        const auto extractedSkeleton =
            scene.Animators().InstanceSkeleton(owner.Entity());
        const kb::scene::TransformComponent& extractedOwnerTransform =
            scene.Transforms().Get(owner.Entity());
        const kb::math::Vec4 extractedSkinOrigin =
            extractedSkeleton.has_value() &&
                extractedSkeleton->currentSkinMatrices.size() == 3U
            ? extractedSkeleton->currentSkinMatrices[2] *
                kb::math::Vec4{ 0.0F, 0.0F, 0.0F, 1.0F }
            : kb::math::Vec4{};
        Require(scene.Runtime().DrainSceneSystemErrors().empty() &&
                extractedSkeleton.has_value() &&
                extractedSkeleton->evaluationCount == 3U &&
                extractedSkeleton->hierarchySolveCount == 3U &&
                NearlyEqual(
                    extractedSkeleton->previousLocalPose.positions[0].z,
                    0.4375F) &&
                NearlyEqual(
                    extractedSkeleton->currentLocalPose.positions[0].x,
                    3.0F) &&
                NearlyEqual(
                    extractedSkeleton->currentLocalPose.positions[0].z,
                    0.0F) &&
                NearlyEqual(
                    extractedSkeleton->currentLocalPose.positions[2].x,
                    3.0F) &&
                NearlyEqual(
                    extractedSkeleton->currentComponentPose.positions[2].x,
                    6.0F) &&
                NearlyEqual(
                    extractedSkeleton->currentComponentPose.positions[2].y,
                    1.0F) &&
                NearlyEqual(extractedSkinOrigin.x, 5.0F) &&
                NearlyEqual(extractedSkinOrigin.y, 1.0F) &&
                NearlyEqual(extractedSkinOrigin.z, 0.0F) &&
                NearlyEqual(extractedOwnerTransform.localPosition.x, 0.0F) &&
                NearlyEqual(extractedOwnerTransform.localPosition.y, 0.0F) &&
                NearlyEqual(extractedOwnerTransform.localPosition.z, 0.25F),
            "Skeletal root motion was not extracted before pose solve and routed exclusively to the Animator owner");

        kb::scene::AnimationClip invalidBoneClip = instanceClip;
        invalidBoneClip.rootMotionMode =
            kb::scene::AnimationRootMotionMode::None;
        invalidBoneClip.rootMotionBoneId = 0U;
        invalidBoneClip.skeletalTracks[0].boneId = 404U;
        const std::filesystem::path invalidBoneClipPath = skeletalRoot /
            "Assets" / "Animation" / "InvalidBone.kbanim";
        kb::scene::AnimatorController invalidBoneController =
            instanceController;
        invalidBoneController.layers[0].states[0]
            .blendChildren[0].clipReference =
            "/Game/Animation/InvalidBone.kbanim";
        const std::filesystem::path invalidBoneControllerPath = skeletalRoot /
            "Assets" / "Animation" / "InvalidBone.kbanimcontroller";
        Require(kb::scene::AnimationAssetIO::SaveClip(
                    invalidBoneClipPath, invalidBoneClip) &&
                kb::scene::AnimationAssetIO::SaveController(
                    invalidBoneControllerPath, invalidBoneController) &&
                scene.Assets().Discover() == 6U,
            "AnimatorInstance invalid-bone fixture could not be discovered");
        const kb::assets::AssetMetadata* invalidControllerMetadata =
            scene.Assets().Manager().Registry().FindByPath(
                "/Game/Animation/InvalidBone.kbanimcontroller");
        Require(invalidControllerMetadata != nullptr,
            "AnimatorInstance invalid-bone controller metadata was missing");
        const kb::scene::SceneObject invalidOwner =
            scene.Entities().CreateObject({ .name = "Invalid Skeletal Character" });
        Require(scene.Components().SkeletonBindings().Set(
                    invalidOwner.Entity(), kb::scene::SkeletonBindingComponent{
                        .skeletonAssetId = skeletonId.value,
                        .skeletonCompatibilitySignature = skeletonSignature,
                        .enabled = true,
                    }),
            "AnimatorInstance invalid-bone binding could not be authored");
        scene.Components().Animators().Set(
            invalidOwner.Entity(), kb::scene::Animator{
                .controllerAssetId = invalidControllerMetadata->id.value,
                .speed = 1.0F,
                .enabled = true,
            });
        static_cast<void>(scene.Runtime().Update(0.0F));
        const std::vector<std::string> invalidBoneErrors =
            scene.Runtime().DrainSceneSystemErrors();
        Require(!scene.Animators().Exists(invalidOwner.Entity()) &&
                std::any_of(
                    invalidBoneErrors.begin(), invalidBoneErrors.end(),
                    [](const std::string& error) {
                        return error.find("bind the authored hierarchy") !=
                            std::string::npos;
                    }),
            "AnimatorInstance accepted a skeletal track with no canonical bone index");

        scene.Components().Animators().Remove(invalidOwner.Entity());
        scene.Runtime().SetPlaying(false);
        static_cast<void>(scene.Runtime().Update(0.0F));
        static_cast<void>(scene.Runtime().DrainSceneSystemErrors());

        kb::scene::AnimationClip constraintClip{};
        constraintClip.durationSeconds = 1.0F;
        constraintClip.looping = true;
        constraintClip.targetSkeletonAssetId = skeletonId.value;
        constraintClip.targetSkeletonCompatibilitySignature =
            skeletonSignature;
        constraintClip.skeletalTracks = {
            {
                .boneId = 700U,
                .keyframes = {
                    { .timeSeconds = 0.0F,
                      .transform = skeleton.bones[0].referencePose },
                    { .timeSeconds = 1.0F,
                      .transform = skeleton.bones[0].referencePose },
                },
            },
            {
                .boneId = 101U,
                .keyframes = {
                    { .timeSeconds = 0.0F,
                      .transform = skeleton.bones[1].referencePose },
                    { .timeSeconds = 1.0F,
                      .transform = skeleton.bones[1].referencePose },
                },
            },
            {
                .boneId = 900U,
                .keyframes = {
                    { .timeSeconds = 0.0F,
                      .transform = skeleton.bones[2].referencePose },
                    { .timeSeconds = 1.0F,
                      .transform = skeleton.bones[2].referencePose },
                },
            },
        };
        const std::filesystem::path constraintClipPath = skeletalRoot /
            "Assets" / "Animation" / "ConstraintPose.kbanim";
        Require(kb::scene::AnimationAssetIO::SaveClip(
                    constraintClipPath, constraintClip),
            "Compact-pose constraint fixture could not save its clip");

        const auto constraintController = [](
            kb::scene::AnimatorRigConstraint constraint) {
            kb::scene::AnimatorController value{};
            value.layers = {
                {
                    .name = "Base",
                    .defaultState = "Pose",
                    .states = {
                        {
                            .name = "Pose",
                            .clipReference =
                                "/Game/Animation/ConstraintPose.kbanim",
                        },
                    },
                },
            };
            value.rigConstraints.push_back(std::move(constraint));
            return value;
        };
        const kb::scene::AnimatorController skeletalIkController =
            constraintController({
                .name = "Arm IK",
                .type = kb::scene::AnimatorRigConstraintType::TwoBoneIK,
                .target = "HandTarget",
                .poleTarget = "ElbowPole",
                .constrainedBoneId = 700U,
                .midBoneId = 101U,
                .tipBoneId = 900U,
            });
        const kb::scene::AnimatorController skeletalAimController =
            constraintController({
                .name = "Hand Aim",
                .type = kb::scene::AnimatorRigConstraintType::Aim,
                .target = "LookTarget",
                .constrainedBoneId = 900U,
            });
        const kb::scene::AnimatorController skeletalCopyController =
            constraintController({
                .name = "Hand Copy",
                .type = kb::scene::AnimatorRigConstraintType::CopyTransform,
                .target = "CopyTarget",
                .constrainedBoneId = 900U,
            });
        const std::filesystem::path skeletalIkControllerPath = skeletalRoot /
            "Assets" / "Animation" / "SkeletalIk.kbanimcontroller";
        const std::filesystem::path skeletalAimControllerPath = skeletalRoot /
            "Assets" / "Animation" / "SkeletalAim.kbanimcontroller";
        const std::filesystem::path skeletalCopyControllerPath = skeletalRoot /
            "Assets" / "Animation" / "SkeletalCopy.kbanimcontroller";
        Require(kb::scene::AnimationAssetIO::SaveController(
                    skeletalIkControllerPath, skeletalIkController) &&
                kb::scene::AnimationAssetIO::SaveController(
                    skeletalAimControllerPath, skeletalAimController) &&
                kb::scene::AnimationAssetIO::SaveController(
                    skeletalCopyControllerPath, skeletalCopyController) &&
                scene.Assets().Discover() == 10U,
            "Compact-pose constraint assets were not discovered");
        const auto loadedSkeletalIkController =
            kb::scene::AnimationAssetIO::LoadController(
                skeletalIkControllerPath);
        Require(loadedSkeletalIkController.has_value() &&
                loadedSkeletalIkController->rigConstraints.size() == 1U &&
                loadedSkeletalIkController->rigConstraints[0]
                        .constrainedBoneId == 700U &&
                loadedSkeletalIkController->rigConstraints[0].midBoneId ==
                    101U &&
                loadedSkeletalIkController->rigConstraints[0].tipBoneId ==
                    900U,
            "Skeletal rig constraint serialization lost stable bone IDs");

        const auto controllerIdAt = [&](std::string_view path) {
            const kb::assets::AssetMetadata* metadata =
                scene.Assets().Manager().Registry().FindByPath(
                    std::filesystem::path{ path });
            Require(metadata != nullptr,
                "Compact-pose constraint controller metadata was missing");
            return metadata->id.value;
        };
        const auto authorConstraintOwner = [&scene, skeletonId,
                                             skeletonSignature](
            std::string_view name, std::uint64_t controllerAssetId,
            kb::scene::TransformComponent transform = {}) {
            const kb::scene::SceneObject entity =
                scene.Entities().CreateObject({
                    .name = std::string{ name },
                    .transform = transform,
                });
            Require(scene.Components().SkeletonBindings().Set(
                        entity.Entity(),
                        kb::scene::SkeletonBindingComponent{
                            .skeletonAssetId = skeletonId.value,
                            .skeletonCompatibilitySignature =
                                skeletonSignature,
                            .enabled = true,
                        }),
                "Compact-pose constraint owner could not bind its Skeleton");
            scene.Components().Animators().Set(
                entity.Entity(), kb::scene::Animator{
                    .controllerAssetId = controllerAssetId,
                    .speed = 1.0F,
                    .enabled = true,
                });
            return entity;
        };
        const kb::scene::SceneObject ikOwner = authorConstraintOwner(
            "Skeletal IK", controllerIdAt(
                "/Game/Animation/SkeletalIk.kbanimcontroller"));
        const kb::scene::SceneObject aimOwner = authorConstraintOwner(
            "Skeletal Aim", controllerIdAt(
                "/Game/Animation/SkeletalAim.kbanimcontroller"));
        const kb::scene::SceneObject copyOwner = authorConstraintOwner(
            "Skeletal Copy", controllerIdAt(
                "/Game/Animation/SkeletalCopy.kbanimcontroller"),
            kb::scene::TransformComponent{
                .localPosition = { 10.0F, 0.0F, 0.0F },
                .localScale = { 2.0F, 2.0F, 2.0F },
            });
        static_cast<void>(scene.Runtime().Update(0.0F));
        const std::size_t entityCountBeforeConstraintEvaluation =
            scene.Entities().Count();
        Require(scene.Runtime().DrainSceneSystemErrors().empty() &&
                scene.Animators().SetIkTarget(
                    ikOwner.Entity(), "HandTarget",
                    kb::scene::AnimatorIkTarget{
                        .worldPosition = { 4.0F, 2.0F, 1.0F },
                    }) &&
                scene.Animators().SetIkTarget(
                    ikOwner.Entity(), "ElbowPole",
                    kb::scene::AnimatorIkTarget{
                        .worldPosition = { 3.0F, 0.0F, 2.0F },
                    }) &&
                scene.Animators().SetIkTarget(
                    aimOwner.Entity(), "LookTarget",
                    kb::scene::AnimatorIkTarget{
                        .worldPosition = { 5.0F, 2.0F, 5.0F },
                    }) &&
                scene.Animators().SetIkTarget(
                    copyOwner.Entity(), "CopyTarget",
                    kb::scene::AnimatorIkTarget{
                        .worldPosition = { 24.0F, 8.0F, 2.0F },
                        .worldRotation = {
                            0.0F, 0.70710678F, 0.0F, 0.70710678F,
                        },
                    }),
            "Compact-pose constraint targets could not bind to live instances");
        scene.Runtime().SetPlaying(true);
        static_cast<void>(scene.Runtime().Update(0.0F));
        const auto ikPose = scene.Animators().InstanceSkeleton(
            ikOwner.Entity());
        const auto aimPose = scene.Animators().InstanceSkeleton(
            aimOwner.Entity());
        const auto copyPose = scene.Animators().InstanceSkeleton(
            copyOwner.Entity());
        const kb::math::Vec3 aimedForward = aimPose.has_value()
            ? kb::math::Rotate(
                  aimPose->currentComponentPose.rotations[2],
                  kb::math::Vec3{ 0.0F, 0.0F, 1.0F })
            : kb::math::Vec3{};
        Require(scene.Runtime().DrainSceneSystemErrors().empty() &&
                ikPose.has_value() && aimPose.has_value() &&
                copyPose.has_value() &&
                ikPose->evaluationCount == 1U &&
                ikPose->hierarchySolveCount == 1U &&
                aimPose->hierarchySolveCount == 1U &&
                copyPose->hierarchySolveCount == 1U &&
                kb::math::Length(
                    ikPose->currentComponentPose.positions[2] -
                    kb::math::Vec3{ 4.0F, 2.0F, 1.0F }) <= 1.0e-3F &&
                kb::math::Dot(
                    aimedForward,
                    kb::math::Vec3{ 0.0F, 0.0F, 1.0F }) >= 0.999F &&
                kb::math::Length(
                    copyPose->currentComponentPose.positions[2] -
                    kb::math::Vec3{ 7.0F, 4.0F, 1.0F }) <= 1.0e-4F &&
                NearlyEqual(
                    copyPose->currentComponentPose.rotations[2].y,
                    0.70710678F) &&
                scene.Entities().Count() ==
                    entityCountBeforeConstraintEvaluation,
            "Skeletal TwoBoneIK/Aim/CopyTransform did not evaluate on compact poses without bone entities or extra hierarchy solves");
        {
            // The same IK 10 000 km out: a double-precision target reaches animator space exactly.
            const kb::math::DVec3 far{ 1.0e7 + 0.25, 0.0, 1.0e7 - 0.125 };
            const kb::scene::SceneObject farIkOwner = authorConstraintOwner(
                "Far Skeletal IK", controllerIdAt("/Game/Animation/SkeletalIk.kbanimcontroller"));
            scene.Transforms().SetLocalTranslation(farIkOwner.Entity(), far);
            static_cast<void>(scene.Runtime().Update(0.0F));
            Require(scene.Animators().SetIkTarget(farIkOwner.Entity(), "HandTarget", kb::scene::AnimatorIkTarget{
                        .preciseWorldPosition = far + kb::math::DVec3{ 4.0, 2.0, 1.0 } }) &&
                    scene.Animators().SetIkTarget(farIkOwner.Entity(), "ElbowPole", kb::scene::AnimatorIkTarget{
                        .preciseWorldPosition = far + kb::math::DVec3{ 3.0, 0.0, 2.0 } }),
                "Double-precision skeletal IK targets were not accepted");
            static_cast<void>(scene.Runtime().Update(0.0F));
            const auto farIkPose = scene.Animators().InstanceSkeleton(farIkOwner.Entity());
            Require(farIkPose.has_value() &&
                    kb::math::Length(farIkPose->currentComponentPose.positions[2] - kb::math::Vec3{ 4.0F, 2.0F, 1.0F }) <= 1.0e-3F,
                "Skeletal TwoBoneIK far from the origin did not reach its double-precision target");
        }

        const std::uint64_t instanceControllerId =
            scene.Animators().Controller(owner.Entity());
        const auto sampleDirectSkeletalState = [&scene, &owner,
                                                 instanceControllerId]() {
            scene.Components().Animators().Remove(owner.Entity());
            static_cast<void>(scene.Runtime().Update(0.0F));
            scene.Components().Animators().Set(
                owner.Entity(), kb::scene::Animator{
                    .controllerAssetId = instanceControllerId,
                    .speed = 1.0F,
                    .enabled = true,
                    .rootMotionOwner =
                        kb::scene::AnimatorRootMotionOwner::Animator,
                });
            kb::scene::TransformComponent* transform =
                scene.Transforms().TryGet(owner.Entity());
            Require(transform != nullptr,
                "Deterministic skeletal sampling lost its root transform");
            transform->localPosition = {};
            transform->localRotation = {};
            transform->localScale = { 1.0F, 1.0F, 1.0F };
            scene.Transforms().MarkModified(owner.Entity());
            static_cast<void>(scene.Runtime().Update(0.0F));
            Require(scene.Animators().CrossFade(
                        owner.Entity(), "Base", "DirectB", 0.0F, 0.0F),
                "Deterministic skeletal sampling could not select DirectB");
        };
        sampleDirectSkeletalState();
        static_cast<void>(scene.Runtime().Update(0.5F));
        const auto wholeStepPose = scene.Animators().InstanceSkeleton(
            owner.Entity());
        const bool hasWholeStepPose = wholeStepPose.has_value();
        const float wholeStepLocalHandX = hasWholeStepPose
            ? wholeStepPose->currentLocalPose.positions[2].x
            : 0.0F;
        const float wholeStepComponentHandX = hasWholeStepPose
            ? wholeStepPose->currentComponentPose.positions[2].x
            : 0.0F;
        const kb::scene::TransformComponent wholeStepTransform =
            scene.Transforms().Get(owner.Entity());

        sampleDirectSkeletalState();
        static_cast<void>(scene.Runtime().Update(0.25F));
        static_cast<void>(scene.Runtime().Update(0.25F));
        const auto splitStepPose = scene.Animators().InstanceSkeleton(
            owner.Entity());
        const kb::scene::TransformComponent splitStepTransform =
            scene.Transforms().Get(owner.Entity());
        const std::string deterministicSkeletalFailure =
            "Skeletal sampling, known pose, or root motion changed across equivalent frame slicing: whole hand=" +
            std::to_string(wholeStepLocalHandX) +
            " split hand=" +
            std::to_string(splitStepPose.has_value()
                               ? splitStepPose->currentLocalPose.positions[2].x
                               : -999.0F) +
            " whole root motion=" +
            std::to_string(wholeStepTransform.localPosition.z) +
            " split root motion=" +
            std::to_string(splitStepTransform.localPosition.z);
        Require(scene.Runtime().DrainSceneSystemErrors().empty() &&
                hasWholeStepPose && splitStepPose.has_value() &&
                NearlyEqual(wholeStepLocalHandX, 3.0F) &&
                NearlyEqual(splitStepPose->currentLocalPose.positions[2].x,
                            3.0F) &&
                NearlyEqual(wholeStepLocalHandX,
                            splitStepPose->currentLocalPose.positions[2].x) &&
                NearlyEqual(wholeStepComponentHandX,
                            splitStepPose->currentComponentPose.positions[2].x) &&
                NearlyEqual(wholeStepTransform.localPosition.z,
                            splitStepTransform.localPosition.z),
            deterministicSkeletalFailure.c_str());

        const auto directBStateBeforeReload = scene.Animators().State(
            owner.Entity(), "Base");
        const kb::assets::AssetMetadata* instanceClipBMetadata =
            scene.Assets().Manager().Registry().FindByPath(
                "/Game/Animation/SkeletalB.kbanim");
        Require(directBStateBeforeReload.has_value() &&
                instanceClipBMetadata != nullptr,
            "Skeletal hot-reload fixture lost its active state or clip metadata");
        const std::uint64_t instanceClipBGeneration =
            scene.Assets().Manager().LoadGeneration(instanceClipBMetadata->id);
        kb::scene::AnimationClip reloadedInstanceClipB = instanceClipB;
        reloadedInstanceClipB.skeletalTracks[0].keyframes[1].transform.position.x =
            10.0F;
        Require(kb::scene::AnimationAssetIO::SaveClip(
                    instanceClipBPath, reloadedInstanceClipB) &&
                scene.Assets().Discover() > 0U &&
                scene.Assets().Manager().LoadGeneration(instanceClipBMetadata->id) >
                    instanceClipBGeneration,
            "Skeletal clip hot reload was not published through the production asset pipeline");
        static_cast<void>(scene.Runtime().Update(0.25F));
        const auto directBStateAfterReload = scene.Animators().State(
            owner.Entity(), "Base");
        const auto reloadedSkeletalPose = scene.Animators().InstanceSkeleton(
            owner.Entity());
        const std::shared_ptr<const kb::scene::AnimatorDebugSnapshot> reloadedDebug =
            scene.Animators().DebugSnapshot();
        const kb::scene::AnimatorDebugInstanceSnapshot* reloadedDebugInstance =
            reloadedDebug == nullptr ? nullptr : reloadedDebug->Find(owner.Entity());
        Require(scene.Runtime().DrainSceneSystemErrors().empty() &&
                directBStateAfterReload.has_value() &&
                reloadedSkeletalPose.has_value() &&
                reloadedDebug != nullptr && transitionedDebug != nullptr &&
                reloadedDebug->revision > transitionedDebug->revision &&
                reloadedDebugInstance != nullptr &&
                reloadedDebugInstance->layers.front().activeState == "DirectB" &&
                reloadedDebugInstance->bones.size() == 3U &&
                NearlyEqual(reloadedDebugInstance->bones[2].localPose.position.x, 7.5F) &&
                directBStateAfterReload->state == "DirectB" &&
                !directBStateAfterReload->transitioning &&
                NearlyEqual(directBStateAfterReload->normalizedTime, 0.75F) &&
                NearlyEqual(
                    reloadedSkeletalPose->currentLocalPose.positions[2].x,
                    7.5F),
            "Skeletal hot reload did not preserve the live state/playhead or use the new clip payload");
        Require(kb::scene::AnimationAssetIO::SaveClip(
                    instanceClipBPath, instanceClipB),
            "Skeletal hot-reload fixture could not restore its canonical clip");

        const std::shared_ptr<const kb::scene::AnimatorDebugSnapshot> retainedDebug =
            scene.Animators().DebugSnapshot();
        scene.Components().Animators().Remove(owner.Entity());
        static_cast<void>(scene.Runtime().Update(0.0F));
        const std::shared_ptr<const kb::scene::AnimatorDebugSnapshot> detachedDebug =
            scene.Animators().DebugSnapshot();
        Require(retainedDebug != nullptr && retainedDebug->Find(owner.Entity()) != nullptr &&
                detachedDebug != nullptr && detachedDebug->Find(owner.Entity()) == nullptr,
            "Animator debug snapshots must remain valid after the live instance detaches");

        std::filesystem::remove_all(skeletalRoot);
    }

    // SK-11.85 — thread-safe asynchronous debug snapshots. With enough
    // skeletal instances the bone copy leaves the animation hot path and runs
    // as one worker-pool job; readers keep receiving complete immutable
    // revisions while evaluation continues on the main thread and workers.
    {
        const std::filesystem::path asyncRoot =
            std::filesystem::temp_directory_path() /
            "21kb-animator-debug-snapshot-async-tests";
        std::filesystem::remove_all(asyncRoot);

        kb::scene::SkeletonAsset asyncSkeleton{};
        asyncSkeleton.bones = {
            {
                .id = 700U,
                .parentIndex = -1,
                .name = "Root",
                .referencePose = { .position = { 3.0F, 0.0F, 0.0F } },
                .inverseBind = {},
            },
            {
                .id = 101U,
                .parentIndex = 0,
                .name = "Spine",
                .referencePose = { .position = { 0.0F, 1.0F, 0.0F } },
                .inverseBind = {},
            },
            {
                .id = 900U,
                .parentIndex = 1,
                .name = "Hand",
                .referencePose = { .position = { 1.0F, 0.0F, 0.0F } },
                .inverseBind = {},
            },
        };
        const std::uint64_t asyncSkeletonSignature =
            kb::scene::SkeletonCompatibilitySignature(asyncSkeleton);
        Require(kb::scene::SkeletonAssetIO::Save(
                    asyncRoot / "Assets" / "Skeletal" / "AsyncRig.kbskeleton",
                    asyncSkeleton),
            "Async debug snapshot fixture could not save its Skeleton");

        kb::scene::Scene bootstrap;
        Require(bootstrap.Assets().MountProject(asyncRoot) &&
                bootstrap.Assets().Discover() == 1U,
            "Async debug snapshot fixture could not discover its Skeleton");
        const kb::assets::AssetMetadata* asyncSkeletonMetadata =
            bootstrap.Assets().Manager().Registry().FindByPath(
                "/Game/Skeletal/AsyncRig.kbskeleton");
        Require(asyncSkeletonMetadata != nullptr,
            "Async debug snapshot fixture did not retain its Skeleton metadata");
        const std::uint64_t asyncSkeletonId = asyncSkeletonMetadata->id.value;

        kb::scene::AnimationClip asyncClip{};
        asyncClip.durationSeconds = 1.0F;
        asyncClip.looping = true;
        asyncClip.targetSkeletonAssetId = asyncSkeletonId;
        asyncClip.targetSkeletonCompatibilitySignature = asyncSkeletonSignature;
        asyncClip.skeletalTracks = {
            {
                .boneId = 900U,
                .bindingMask = 1U,
                .keyframes = {
                    { .timeSeconds = 0.0F, .transform = {} },
                    { .timeSeconds = 1.0F, .transform = {
                        .position = { 2.0F, 0.0F, 0.0F } } },
                },
            },
        };
        kb::scene::AnimatorController asyncController{};
        asyncController.layers = {
            {
                .name = "Base",
                .defaultState = "Move",
                .weight = 1.0F,
                .mask = 7U,
                .states = {
                    {
                        .name = "Move",
                        .clipReference = "/Game/Animation/Async.kbanim",
                    },
                },
            },
        };
        Require(kb::scene::AnimationAssetIO::SaveClip(
                    asyncRoot / "Assets" / "Animation" / "Async.kbanim",
                    asyncClip) &&
                kb::scene::AnimationAssetIO::SaveController(
                    asyncRoot / "Assets" / "Animation" / "Async.kbanimcontroller",
                    asyncController),
            "Async debug snapshot fixture could not save its clip and controller");

        // Above the async offload threshold (32 skeletal instances).
        constexpr std::size_t kAsyncRigCount = 40U;
        const auto buildScene = [&]() {
            auto scene = std::make_unique<kb::scene::Scene>();
            Require(scene->Assets().MountProject(asyncRoot) &&
                    scene->Assets().Discover() == 3U,
                "Async debug snapshot fixture could not discover its assets");
            const kb::assets::AssetMetadata* skeletonMetadata =
                scene->Assets().Manager().Registry().FindByPath(
                    "/Game/Skeletal/AsyncRig.kbskeleton");
            const kb::assets::AssetMetadata* controllerMetadata =
                scene->Assets().Manager().Registry().FindByPath(
                    "/Game/Animation/Async.kbanimcontroller");
            Require(skeletonMetadata != nullptr && controllerMetadata != nullptr &&
                    skeletonMetadata->id.value == asyncSkeletonId,
                "Async debug snapshot fixture did not resolve stable asset ids");
            for (std::size_t index = 0U; index < kAsyncRigCount; ++index) {
                const kb::scene::SceneObject object =
                    scene->Entities().CreateObject({ .name = "Async Rig" });
                Require(scene->Components().SkeletonBindings().Set(
                            object.Entity(), kb::scene::SkeletonBindingComponent{
                                .skeletonAssetId = asyncSkeletonId,
                                .skeletonCompatibilitySignature = asyncSkeletonSignature,
                                .enabled = true,
                            }),
                    "Async debug snapshot fixture could not author SkeletonBinding");
                scene->Components().Animators().Set(object.Entity(), kb::scene::Animator{
                    .controllerAssetId = controllerMetadata->id.value,
                    .speed = 1.0F,
                    .enabled = true,
                });
            }
            return scene;
        };
        const auto bonesMatch = [](const kb::scene::AnimatorDebugInstanceSnapshot& lhs,
                                   const kb::scene::AnimatorDebugInstanceSnapshot& rhs) {
            if (lhs.entity != rhs.entity || lhs.bones.size() != rhs.bones.size()) {
                return false;
            }
            for (std::size_t index = 0U; index < lhs.bones.size(); ++index) {
                const kb::scene::AnimatorDebugBoneSnapshot& lhsBone = lhs.bones[index];
                const kb::scene::AnimatorDebugBoneSnapshot& rhsBone = rhs.bones[index];
                if (lhsBone.id != rhsBone.id ||
                    !NearlyEqual(lhsBone.localPose.position.x, rhsBone.localPose.position.x) ||
                    !NearlyEqual(lhsBone.localPose.position.y, rhsBone.localPose.position.y) ||
                    !NearlyEqual(lhsBone.localPose.position.z, rhsBone.localPose.position.z) ||
                    !NearlyEqual(lhsBone.componentPose.position.x, rhsBone.componentPose.position.x) ||
                    !NearlyEqual(lhsBone.componentPose.position.y, rhsBone.componentPose.position.y) ||
                    !NearlyEqual(lhsBone.componentPose.position.z, rhsBone.componentPose.position.z)) {
                    return false;
                }
            }
            return true;
        };

        const std::unique_ptr<kb::scene::Scene> sceneA = buildScene();
        sceneA->Runtime().SetPlaying(false);
        static_cast<void>(sceneA->Runtime().Update(0.0F));
        sceneA->Runtime().SetPlaying(true);
        static_cast<void>(sceneA->Runtime().Update(0.25F));
        const kb::scene::SceneRuntimeHotPathReport asyncHotPath =
            sceneA->Runtime().HotPathReport();
        sceneA->Animators().WaitForDebugSnapshot();
        const std::shared_ptr<const kb::scene::AnimatorDebugSnapshot> snapshotA =
            sceneA->Animators().DebugSnapshot();
        Require(asyncHotPath.animatorDebugSnapshotAsyncSubmissionCount >= 1U,
            "Async debug snapshot builds must be offloaded to the worker pool above the threshold");
        Require(snapshotA != nullptr && snapshotA->revision >= 1U &&
                snapshotA->instances.size() == kAsyncRigCount,
            "Async debug snapshot must publish every skeletal instance");
        bool asyncSnapshotComplete = true;
        for (const kb::scene::AnimatorDebugInstanceSnapshot& instance : snapshotA->instances) {
            asyncSnapshotComplete = asyncSnapshotComplete && instance.bones.size() == 3U;
        }
        Require(asyncSnapshotComplete,
            "Async debug snapshot must publish a complete bone set for every skeletal instance");
        const kb::scene::SceneEntity firstAsyncEntity = snapshotA->instances.front().entity;
        const auto firstAsyncView = sceneA->Animators().InstanceSkeleton(firstAsyncEntity);
        Require(firstAsyncView.has_value() && firstAsyncView->currentLocalPose.positions.size() == 3U &&
                NearlyEqual(snapshotA->instances.front().bones[2].localPose.position.x,
                    firstAsyncView->currentLocalPose.positions[2].x) &&
                NearlyEqual(snapshotA->instances.front().bones[2].componentPose.position.x,
                    firstAsyncView->currentComponentPose.positions[2].x),
            "Async debug snapshot bones must match the live frozen pose buffers");

        const std::uint64_t firstAsyncRevision = snapshotA->revision;
        const float firstAsyncBoneX = snapshotA->instances.front().bones[2].localPose.position.x;
        static_cast<void>(sceneA->Runtime().Update(0.25F));
        sceneA->Animators().WaitForDebugSnapshot();
        const std::shared_ptr<const kb::scene::AnimatorDebugSnapshot> snapshotB =
            sceneA->Animators().DebugSnapshot();
        Require(snapshotB != nullptr && snapshotB->revision > firstAsyncRevision &&
                snapshotB->instances.front().bones[2].localPose.position.x > firstAsyncBoneX,
            "Async debug snapshots must track the animated pose across frames");
        Require(snapshotA->revision == firstAsyncRevision &&
                NearlyEqual(snapshotA->instances.front().bones[2].localPose.position.x, firstAsyncBoneX),
            "A retained async debug snapshot must stay immutable while newer revisions publish");

        const std::unique_ptr<kb::scene::Scene> sceneB = buildScene();
        sceneB->Runtime().SetPlaying(false);
        static_cast<void>(sceneB->Runtime().Update(0.0F));
        sceneB->Runtime().SetPlaying(true);
        static_cast<void>(sceneB->Runtime().Update(0.25F));
        static_cast<void>(sceneB->Runtime().Update(0.25F));
        sceneB->Animators().WaitForDebugSnapshot();
        const std::shared_ptr<const kb::scene::AnimatorDebugSnapshot> snapshotB2 =
            sceneB->Animators().DebugSnapshot();
        bool deterministicSnapshot = snapshotB2 != nullptr &&
            snapshotB2->instances.size() == snapshotB->instances.size();
        for (std::size_t index = 0U;
             deterministicSnapshot && index < snapshotB->instances.size(); ++index) {
            deterministicSnapshot = bonesMatch(
                snapshotB->instances[index], snapshotB2->instances[index]);
        }
        Require(deterministicSnapshot,
            "Async debug snapshots must stay deterministic across identical scenes and worker schedules");

        std::atomic<bool> stopSnapshotReader{ false };
        std::atomic<bool> snapshotRevisionRegressed{ false };
        std::thread snapshotReader{ [&] {
            std::uint64_t lastRevision = 0U;
            while (!stopSnapshotReader.load(std::memory_order_acquire)) {
                const std::shared_ptr<const kb::scene::AnimatorDebugSnapshot> observed =
                    sceneA->Animators().DebugSnapshot();
                if (observed == nullptr) continue;
                if (observed->revision < lastRevision) {
                    snapshotRevisionRegressed.store(true, std::memory_order_release);
                    return;
                }
                lastRevision = observed->revision;
            }
        } };
        for (std::uint32_t frame = 0U; frame < 30U; ++frame) {
            static_cast<void>(sceneA->Runtime().Update(1.0F / 60.0F));
            sceneA->Animators().WaitForDebugSnapshot();
        }
        stopSnapshotReader.store(true, std::memory_order_release);
        snapshotReader.join();
        Require(!snapshotRevisionRegressed.load(std::memory_order_acquire),
            "Concurrent readers must never observe an async debug snapshot revision regression");

        const auto beforeDependencyChange = sceneA->Animators().DebugSnapshot();
        Require(beforeDependencyChange && !beforeDependencyChange->instances.empty(),
            "Compatibility cache test requires a live animator snapshot");
        const auto checkedEntity = beforeDependencyChange->instances.front().entity;
        const auto diagnosticCount = beforeDependencyChange->instances.front().compatibilityDiagnostics.size();
        auto& registry = sceneA->Assets().Manager().Registry();
        const auto skeletonFile = registry.Find(kb::assets::AssetId{
            beforeDependencyChange->instances.front().skeletonAssetId })->physicalPath;
        auto offlineSkeletonFile = skeletonFile;
        offlineSkeletonFile += ".offline";
        std::filesystem::rename(skeletonFile, offlineSkeletonFile);
        static_cast<void>(sceneA->Runtime().Update(0.0F));
        sceneA->Animators().WaitForDebugSnapshot();
        const auto cachedDiagnostics = sceneA->Animators().DebugSnapshot();
        std::filesystem::rename(offlineSkeletonFile, skeletonFile);
        Require(cachedDiagnostics && cachedDiagnostics->Find(checkedEntity) &&
            cachedDiagnostics->Find(checkedEntity)->compatibilityDiagnostics.size() == diagnosticCount,
            "Unchanged animator diagnostics must use resident results until an asset refresh");
        const auto originalMetadata = *registry.Find(kb::assets::AssetId{
            beforeDependencyChange->instances.front().controllerAssetId });
        auto changedMetadata = originalMetadata;
        changedMetadata.dependencies.push_back(kb::assets::AssetId{ 0xCAFE1234U });
        Require(registry.Upsert(std::move(changedMetadata)), "Could not change animator dependency metadata");
        static_cast<void>(sceneA->Runtime().Update(0.0F));
        sceneA->Animators().WaitForDebugSnapshot();
        const auto changedDiagnostics = sceneA->Animators().DebugSnapshot();
        Require(changedDiagnostics && changedDiagnostics->Find(checkedEntity) &&
            changedDiagnostics->Find(checkedEntity)->compatibilityDiagnostics.size() == diagnosticCount + 1U,
            "Animator compatibility diagnostics must invalidate after dependency metadata changes");
        Require(registry.Upsert(originalMetadata), "Could not restore animator dependency metadata");
        static_cast<void>(sceneA->Runtime().Update(0.0F));
        sceneA->Animators().WaitForDebugSnapshot();
        const auto restoredDiagnostics = sceneA->Animators().DebugSnapshot();
        Require(restoredDiagnostics && restoredDiagnostics->Find(checkedEntity) &&
            restoredDiagnostics->Find(checkedEntity)->compatibilityDiagnostics.size() == diagnosticCount,
            "Animator compatibility diagnostics must clear a repaired dependency failure");

        const auto removedAnimator = restoredDiagnostics->instances.back().entity;
        Require(removedAnimator != checkedEntity, "Animator removal test requires two separate rigs");
        sceneA->Entities().Destroy(checkedEntity);
        Require(!sceneA->Animators().Exists(checkedEntity) &&
            sceneA->Animators().Controller(checkedEntity) == 0U &&
            sceneA->Animators().Parameters(checkedEntity).empty() &&
            sceneA->Animators().RuntimeBindingGeneration(checkedEntity) == 0U &&
            !sceneA->Animators().InstanceSkeleton(checkedEntity).has_value(),
            "Destroyed rigs must stop exposing runtime state before the next animation update");
        sceneA->Components().Animators().Remove(removedAnimator);
        static_cast<void>(sceneA->Runtime().Update(0.0F));
        sceneA->Animators().WaitForDebugSnapshot();
        Require(!sceneA->Animators().Exists(checkedEntity) && !sceneA->Animators().Exists(removedAnimator) &&
            sceneA->Entities().IsAlive(removedAnimator),
            "Animator synchronization must discard destroyed entities and removed components while other rigs remain");
        const auto afterRemoval = sceneA->Animators().DebugSnapshot();
        Require(afterRemoval && afterRemoval->Find(checkedEntity) == nullptr &&
            restoredDiagnostics->Find(checkedEntity) != nullptr,
            "Removing a rig must publish its absence without mutating retained snapshots");

        std::uint64_t teardownAsyncSubmissions = 0U;
        for (std::uint32_t iteration = 0U; iteration < 8U; ++iteration) {
            const std::unique_ptr<kb::scene::Scene> ephemeral = buildScene();
            ephemeral->Runtime().SetPlaying(true);
            static_cast<void>(ephemeral->Runtime().Update(1.0F / 60.0F));
            teardownAsyncSubmissions +=
                ephemeral->Runtime().HotPathReport().animatorDebugSnapshotAsyncSubmissionCount;
            // The scene is destroyed here with its asynchronous snapshot
            // build potentially still in flight; ~SceneState must join it
            // before releasing animator storage.
        }
        Require(teardownAsyncSubmissions == 8U,
            "Async debug snapshot builds must survive scene teardown while in flight");

        std::filesystem::remove_all(asyncRoot);
    }
    std::filesystem::remove_all(root);
}

} // namespace kb::tests
