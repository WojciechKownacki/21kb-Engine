#include "TestSupport.hpp"

#include "engine/scene/AnimationAssetIO.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/project/ProjectDescriptor.hpp"
#include "engine/scene/CharacterControllerComponent.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/RigidbodyComponent.hpp"
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
#include "engine/script/ScriptRuntimeHost.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

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

void RunAnimationRuntimeTests() {
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
            loadedSkeletalClip->morphTracks.size() == 1U &&
            loadedSkeletalClip->curves.size() == 1U &&
            loadedSkeletalClip->rootMotionMode == kb::scene::AnimationRootMotionMode::ExtractFromBone &&
            loadedSkeletalClip->rootMotionBoneId == 101U,
        "Skeletal AnimationClip round trip lost canonical bindings");
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
            { .name = "Walk State", .clipReference = "/Game/Animation/Move.kbanim" },
            { .name = "Run State", .clipReference = "/Game/Animation/Run Fast.kbanim" },
            { .name = "Blend State", .blendParameter = "Speed", .blendChildren = {
                { .threshold = 0.0F, .clipReference = "/Game/Animation/Move.kbanim" },
                { .threshold = 10.0F, .clipReference = "/Game/Animation/Run Fast.kbanim" },
            } },
        }, .transitions = {
            { .fromState = "Walk State", .toState = "Run State", .durationSeconds = 0.2F,
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
    const auto controllerPath = root / "Assets" / "Animation" / "Character.kbanimcontroller";
    Require(kb::scene::AnimationAssetIO::SaveController(controllerPath, controller), "AnimatorController production asset could not be saved");
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
    Require(!kb::scene::AnimationAssetIO::SaveController(
                root / "Assets" / "Animation" / "InvalidBlend.kbanimcontroller",
                invalidBlendController),
        "Blend tree accepted a non-Float parameter");
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
                .inverseBind = {},
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
                .keyframes = {
                    { .timeSeconds = 0.0F, .transform = {} },
                    { .timeSeconds = 1.0F, .transform = {
                        .position = { 2.0F, 0.0F, 0.0F },
                    } },
                },
            },
            {
                .boneId = 700U,
                .keyframes = {
                    { .timeSeconds = 0.0F, .transform = {} },
                    { .timeSeconds = 1.0F, .transform = {
                        .position = { 0.0F, 0.0F, 1.0F },
                    } },
                },
            },
        };
        instanceClip.rootMotionMode =
            kb::scene::AnimationRootMotionMode::ExtractFromBone;
        instanceClip.rootMotionBoneId = 700U;
        const std::filesystem::path instanceClipPath = skeletalRoot /
            "Assets" / "Animation" / "Skeletal.kbanim";
        Require(kb::scene::AnimationAssetIO::SaveClip(
                    instanceClipPath, instanceClip),
            "AnimatorInstance fixture could not save its skeletal clip");

        kb::scene::AnimatorController instanceController{};
        instanceController.layers = {
            {
                .name = "Base",
                .defaultState = "Idle",
                .states = {
                    {
                        .name = "Idle",
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
                scene.Assets().Discover() == 3U,
            "AnimatorInstance fixture could not discover its clip and controller");
        const kb::assets::AssetMetadata* instanceControllerMetadata =
            scene.Assets().Manager().Registry().FindByPath(
                "/Game/Animation/Skeletal.kbanimcontroller");
        Require(instanceControllerMetadata != nullptr,
            "AnimatorInstance fixture did not retain controller metadata");

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
                scene.Entities().Count() == 1U,
            "AnimatorInstance did not derive contiguous double-buffered local/component SoA poses without bone entities");

        kb::scene::AnimationClip invalidBoneClip = instanceClip;
        invalidBoneClip.rootMotionMode =
            kb::scene::AnimationRootMotionMode::None;
        invalidBoneClip.rootMotionBoneId = 0U;
        invalidBoneClip.skeletalTracks[0].boneId = 404U;
        const std::filesystem::path invalidBoneClipPath = skeletalRoot /
            "Assets" / "Animation" / "InvalidBone.kbanim";
        kb::scene::AnimatorController invalidBoneController =
            instanceController;
        invalidBoneController.layers[0].states[0].clipReference =
            "/Game/Animation/InvalidBone.kbanim";
        const std::filesystem::path invalidBoneControllerPath = skeletalRoot /
            "Assets" / "Animation" / "InvalidBone.kbanimcontroller";
        Require(kb::scene::AnimationAssetIO::SaveClip(
                    invalidBoneClipPath, invalidBoneClip) &&
                kb::scene::AnimationAssetIO::SaveController(
                    invalidBoneControllerPath, invalidBoneController) &&
                scene.Assets().Discover() == 5U,
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

        std::filesystem::remove_all(skeletalRoot);
    }
    std::filesystem::remove_all(root);
}

} // namespace kb::tests
