#include "TestSupport.hpp"

#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneBehaviourComponents.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTimelines.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/scene/SceneUIComponents.hpp"
#include "engine/ecs/ComponentReflectionMacros.hpp"
#include "engine/ecs/World.hpp"
#include "engine/scene/TimelineAssetIO.hpp"
#include "engine/scene/TimelineSequencePlan.hpp"
#include "engine/scene/AnimationAssetIO.hpp"
#include "engine/scene/SceneAnimators.hpp"
#include "engine/scene/SceneAnimatorComponents.hpp"
#include "engine/scene/SkeletonAssetIO.hpp"
#include "engine/scene/SkeletonBindingComponent.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace kb::tests {
void RunTimelineAudioTests(const std::filesystem::path& root);

namespace {
void TestTimelineSkeletalAnimation(const std::filesystem::path& root) {
    using namespace kb::scene;
    const auto project = root / "SkeletalTimeline";
    std::filesystem::create_directories(project / "Assets");
    const kb::math::Mat4 identity{.columns = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
    SkeletonAsset skeleton;
    skeleton.bones.push_back({.id = 1, .parentIndex = -1, .name = "Root", .inverseBind = identity});
    Require(SkeletonAssetIO::Save(project / "Assets" / "Rig.kbskeleton", skeleton), "Timeline skeleton save failed");
    Scene scene;
    Require(scene.Assets().MountProject(project) && scene.Assets().Discover() == 1, "Timeline skeleton discovery failed");
    const auto* metadata = scene.Assets().Manager().Registry().FindByPath("/Game/Rig.kbskeleton");
    Require(metadata != nullptr, "Timeline skeleton metadata missing");
    AnimationClip clip;
    clip.looping = false;
    clip.targetSkeletonAssetId = metadata->id.value;
    clip.targetSkeletonCompatibilitySignature = SkeletonCompatibilitySignature(skeleton);
    clip.skeletalTracks.push_back({.boneId = 1, .keyframes = {{0.0F, {}}, {1.0F, {.position = {2, 0, 0}}}}});
    clip.morphTracks.push_back({"Expression", {{0.0F, 0.0F}, {1.0F, 1.0F}}});
    clip.curves.push_back({"Weight", {{0.0F, 0.0F}, {1.0F, 2.0F}}});
    TimelineAsset timeline;
    timeline.bindings.push_back({"Actor", "."});
    timeline.animationTracks.push_back({"Actor", "/Game/Pose.kbanim", 0.0F, 1.0F, 0.0F, 1.0F});
    Require(AnimationAssetIO::SaveClip(project / "Assets" / "Pose.kbanim", clip) &&
        TimelineAssetIO::Save(project / "Assets" / "Pose.kbtimeline", timeline) && scene.Assets().Discover() == 3,
        "Skeletal Timeline assets failed to save or discover");
    const auto* sequence = scene.Assets().Manager().Registry().FindByPath("/Game/Pose.kbtimeline");
    Require(sequence != nullptr, "Skeletal Timeline metadata missing");
    const auto owner = scene.Entities().CreateObject({.name = "Actor"});
    Require(scene.Components().SkeletonBindings().Set(owner.Entity(), {
        .skeletonAssetId = clip.targetSkeletonAssetId,
        .skeletonCompatibilitySignature = clip.targetSkeletonCompatibilitySignature, .enabled = true}),
        "Timeline actor could not bind its skeleton");
    const auto instance = scene.Timelines().Create(sequence->id.value, owner.Entity());
    Require(instance && scene.Timelines().Seek(instance, 0.5F), "Skeletal Timeline scrub failed");
    auto pose = scene.Animators().InstanceSkeleton(owner.Entity());
    const auto curve = static_cast<const kb::scene::Scene&>(scene).Animators().Curve(owner.Entity(), "Weight");
    Require(pose && pose->currentLocalPose.positions.size() == 1 &&
        NearlyEqual(pose->currentLocalPose.positions[0].x, 1.0F) && pose->morphWeights.currentWeights.size() == 1 &&
        NearlyEqual(pose->morphWeights.currentWeights[0], 0.5F) && curve && NearlyEqual(*curve, 1.0F),
        "Timeline did not sample skeletal, morph and curve channels together");
    bool conflict = false;
    try { static_cast<void>(scene.Timelines().Create(sequence->id.value, owner.Entity())); }
    catch (const std::runtime_error& error) { conflict = std::string{error.what()}.find("already controlled") != std::string::npos; }
    Require(conflict && scene.Timelines().Exists(instance), "Competing Timeline animation ownership was accepted");
    Require(scene.Timelines().SetPlayback(instance, -1.0F, false) && scene.Timelines().Play(instance),
        "Skeletal Timeline reverse setup failed");
    static_cast<void>(scene.Runtime().Update(0.25F));
    pose = scene.Animators().InstanceSkeleton(owner.Entity());
    Require(pose && NearlyEqual(pose->currentLocalPose.positions[0].x, 0.5F) &&
        NearlyEqual(pose->morphWeights.currentWeights[0], 0.25F), "Skeletal Timeline reverse sample drifted");
    Require(scene.Timelines().Release(instance) && !scene.Animators().Exists(owner.Entity()),
        "Timeline release retained its temporary animator");
}

void TestTimelineAnimationRuntime(const std::filesystem::path& root) {
    using namespace kb::scene;
    const auto project = root / "AnimationRuntime";
    std::filesystem::create_directories(project / "Assets");
    AnimationClip clip;
    clip.looping = false;
    clip.tracks.push_back({.targetPath = ".", .keyframes = {{0.0F, {.position = {0.0F, 0.0F, 0.0F}}},
        {1.0F, {.position = {10.0F, 0.0F, 0.0F}}}}});
    clip.events = {{0.25F, 71}, {0.75F, 72}};
    AnimatorController controller;
    AnimatorControllerLayer layer;
    layer.name = "Base";
    layer.defaultState = "Idle";
    layer.states.push_back({.id = 1, .name = "Idle", .clipReference = "/Game/Motion.kbanim"});
    controller.layers.push_back(layer);
    TimelineAsset timeline;
    timeline.bindings.push_back({"Actor", "."});
    timeline.animationTracks.push_back({"Actor", "/Game/Motion.kbanim", 0.25F, 0.75F, 0.0F, 2.0F});
    timeline.markers.push_back({0.375F, 70});
    {
        std::ofstream script(project / "Assets" / "Events.lua");
        script << R"lua(function Created(self)
  Events.Subscribe("OnAnimationEvent", function(event)
    SetShared("clipEvent", event.args.event)
    SetShared("clipPhase", event.args.normalizedTime)
    SetShared("clipLayer", event.args.layer)
    SetShared("clipState", event.args.state)
    SetShared("clipSchema", event.args.schemaMajor)
  end)
end
)lua";
    }
    Require(AnimationAssetIO::SaveClip(project / "Assets" / "Motion.kbanim", clip) &&
        AnimationAssetIO::SaveController(project / "Assets" / "Controller.kbanimcontroller", controller) &&
        TimelineAssetIO::Save(project / "Assets" / "Animation.kbtimeline", timeline), "Timeline animation assets failed to save");
    timeline.markers.clear();
    Require(TimelineAssetIO::Save(project / "Assets" / "ClipOnly.kbtimeline", timeline), "Clip-only Timeline save failed");
    Scene scene;
    Require(scene.Assets().MountProject(project) && scene.Assets().Discover() == 5, "Timeline animation discovery failed");
    const auto* metadata = scene.Assets().Manager().Registry().FindByPath("/Game/Animation.kbtimeline");
    const auto* controllerMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Controller.kbanimcontroller");
    Require(metadata && controllerMetadata, "Timeline animation metadata missing");
    const auto owner = scene.Entities().CreateObject({.name = "Actor"});
    scene.Components().Animators().Set(owner.Entity(), {.controllerAssetId = controllerMetadata->id.value, .speed = 0.5F});
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(scene.Animators().Exists(owner.Entity()), "Original animator did not attach");
    const auto instance = scene.Timelines().Create(metadata->id.value, owner.Entity());
    Require(instance && scene.Timelines().Seek(instance, 0.5F) &&
        NearlyEqual(scene.Transforms().Get(owner.Entity()).localPosition.x, 5.0F), "Animation scrub did not immediately sample its clip");
    Require(scene.Timelines().SetPlayback(instance, -1.0F, false) && scene.Timelines().Play(instance),
        "Timeline animation reverse setup failed");
    static_cast<void>(scene.Runtime().Update(0.125F));
    Require(NearlyEqual(scene.Transforms().Get(owner.Entity()).localPosition.x, 2.5F), "Timeline animation reverse sample drifted");
    auto crossed = scene.Timelines().DrainMarkerEvents();
    Require(crossed.size() == 2 && crossed[0].animationEvent && crossed[0].markerId == 71 &&
        NearlyEqual(crossed[0].normalizedTime, 0.25F) && crossed[1].markerId == 70 && !crossed[1].animationEvent,
        "Clip events did not merge with Timeline markers in reverse order");
    Require(scene.Timelines().Pause(instance), "Timeline animation pause failed");
    static_cast<void>(scene.Runtime().Update(0.125F));
    Require(NearlyEqual(scene.Transforms().Get(owner.Entity()).localPosition.x, 2.5F), "Autonomous animator advanced a Timeline-owned pose");
    Require(scene.Timelines().Seek(instance, 0.875F) &&
        scene.Animators().Controller(owner.Entity()) == controllerMetadata->id.value,
        "Leaving an animation interval retained Timeline ownership");
    Require(scene.Timelines().Seek(instance, 0.5F) &&
        NearlyEqual(scene.Transforms().Get(owner.Entity()).localPosition.x, 5.0F),
        "Re-entering an animation interval failed to reacquire its pose");
    const auto controlledGeneration = scene.Animators().RuntimeBindingGeneration(owner.Entity());
    Require(scene.Timelines().Release(instance) && scene.Animators().Controller(owner.Entity()) == controllerMetadata->id.value &&
        NearlyEqual(scene.Animators().Speed(owner.Entity()), 0.5F) &&
        scene.Animators().RuntimeBindingGeneration(owner.Entity()) > controlledGeneration,
        "Timeline release did not restore the previous animator with a fresh binding generation");
    const auto* clipOnlyMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/ClipOnly.kbtimeline");
    Require(clipOnlyMetadata != nullptr, "Clip-only Timeline metadata missing");
    const auto clipOnly = scene.Timelines().Create(clipOnlyMetadata->id.value, owner.Entity());
    Require(clipOnly && scene.Timelines().Seek(clipOnly, 0.875F) &&
        scene.Timelines().SetPlayback(clipOnly, 8.0F, true) && scene.Timelines().Play(clipOnly), "Clip-only loop setup failed");
    static_cast<void>(scene.Runtime().Update(0.25F));
    crossed = scene.Timelines().DrainMarkerEvents();
    Require(crossed.size() == 4 && crossed[0].markerId == 71 && crossed[1].markerId == 72 &&
        crossed[2].markerId == 71 && crossed[3].markerId == 72, "Multiple clip-only loops lost events");
    Require(scene.Timelines().SetPlayback(clipOnly, 100000.0F, true), "Clip event overflow setup failed");
    static_cast<void>(scene.Runtime().Update(0.25F));
    const auto errors = scene.Runtime().DrainSceneSystemErrors();
    Require(errors.size() == 1 && errors[0].find("Timeline marker queue exceeded") != std::string::npos &&
        NearlyEqual(scene.Timelines().Time(clipOnly), 0.875F) && scene.Timelines().DrainMarkerEvents().empty(),
        "Clip-only event overflow did not preserve cursor and queue");
    const auto* clipMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Motion.kbanim");
    Require(clipMetadata != nullptr, "Reloadable Timeline clip metadata missing");
    const auto clipId = clipMetadata->id;
    clip.events[0].id = 81;
    Require(AnimationAssetIO::SaveClip(project / "Assets" / "Motion.kbanim", clip) &&
        scene.Assets().Manager().RefreshAsset(clipId), "Timeline clip reload failed");
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(scene.Timelines().Seek(clipOnly, 0.0F) &&
        scene.Timelines().Skip(clipOnly, 1.0F, TimelineSkipMarkerPolicy::EmitCrossed), "Reloaded clip skip failed");
    crossed = scene.Timelines().DrainMarkerEvents();
    Require(crossed.size() == 2 && crossed[0].markerId == 81 && crossed[1].markerId == 72,
        "Timeline retained stale clip event keys after reload");
    clip.events[0].id = 71;
    Require(AnimationAssetIO::SaveClip(project / "Assets" / "Motion.kbanim", clip) &&
        scene.Assets().Manager().RefreshAsset(clipId), "Timeline clip restore failed");
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(scene.Timelines().Release(clipOnly), "Clip-only Timeline release failed");
    const auto* script = scene.Assets().Manager().Registry().FindByPath("/Game/Events.lua");
    Require(script != nullptr, "Clip event receiver missing");
    scene.Components().Behaviours().Set(owner.Entity(), {
        .behaviourAssetId = script->id.value, .backend = BehaviourBackend::Lua, .enabled = true});
    kb::script::ScriptRuntimeHost host{scene};
    Require(host.Succeeded() && host.InstallSceneSystem(), "Clip event script host failed");
    static_cast<void>(scene.Runtime().Update(0.0F));
    const auto scripted = scene.Timelines().Create(metadata->id.value, owner.Entity());
    Require(scripted && scene.Timelines().Seek(scripted, 0.25F) && scene.Timelines().Play(scripted),
        "Scripted clip event setup failed");
    static_cast<void>(scene.Runtime().Update(0.125F));
    const auto event = host.SharedState().Get("clipEvent");
    const auto phase = host.SharedState().Get("clipPhase");
    const auto eventLayer = host.SharedState().Get("clipLayer");
    const auto eventState = host.SharedState().Get("clipState");
    const auto schema = host.SharedState().Get("clipSchema");
    Require(event && phase && eventLayer && eventState && schema && event->AsInt() == 71 &&
        NearlyEqual(phase->AsFloat(), 0.25F) && eventLayer->AsString() == "Timeline" &&
        eventState->AsString() == "Clip" && schema->AsInt() == 1,
        ("Timeline clip event schema: event=" + std::to_string(event ? event->AsInt() : 0) +
            " phase=" + std::to_string(phase ? phase->AsFloat() : -1.0F) +
            " layer=" + (eventLayer ? eventLayer->AsString() : "missing") +
            " state=" + (eventState ? eventState->AsString() : "missing") +
            " schema=" + std::to_string(schema ? schema->AsInt() : -1)).c_str());
}

void TestTimelineSequencePlan(const std::filesystem::path& root) {
    using namespace kb::scene;
    using Handle = kb::assets::AssetHandle<TimelineAsset>;
    auto parent = std::make_shared<TimelineAsset>();
    parent->durationSeconds = 4.0F;
    parent->bindings.push_back({"Target", "."});
    parent->sequenceTracks.push_back({"Child", 1.0F, 3.0F, 0.5F, 1.5F, {{"Actor", "Target"}}});
    auto child = std::make_shared<TimelineAsset>();
    child->durationSeconds = 4.0F;
    child->bindings.push_back({"Actor", "."});
    child->sequenceTracks.push_back({"Leaf", 0.0F, 2.0F, 0.0F, 0.5F, {{"Object", "Actor"}}});
    auto leaf = std::make_shared<TimelineAsset>();
    leaf->bindings.push_back({"Object", "."});
    const Handle parentHandle{kb::assets::AssetId{1}, parent};
    const auto resolve = [&](const std::string& reference) -> Handle {
        if (reference == "Child") return {kb::assets::AssetId{2}, child};
        if (reference == "Leaf") return {kb::assets::AssetId{3}, leaf};
        if (reference == "Root") return parentHandle;
        return {};
    };
    std::string error;
    auto plan = BuildTimelineSequencePlan(parentHandle, resolve, error);
    Require(plan && plan->size() == 3 && error.empty(), "Nested sequence expansion failed");
    Require(NearlyEqual(static_cast<float>((*plan)[2].startSeconds), 1.0F) &&
        NearlyEqual(static_cast<float>((*plan)[2].endSeconds), 2.0F) &&
        NearlyEqual(static_cast<float>((*plan)[2].sourceOffsetSeconds), 0.25F) &&
        NearlyEqual(static_cast<float>((*plan)[2].rate), 0.75F) &&
        (*plan)[2].rootBindings.at("Object") == "Target",
        "Nested sequence clipping, time scaling or remapping changed");
    const auto path = root / "Nested.kbtimeline";
    Require(TimelineAssetIO::Save(path, *parent), "Nested sequence serialization failed");
    const auto loaded = TimelineAssetIO::Load(path);
    Require(loaded && loaded->sequenceTracks.size() == 1 &&
        loaded->sequenceTracks[0].bindings[0].child == "Actor" &&
        loaded->sequenceTracks[0].bindings[0].parent == "Target" &&
        loaded->sequenceTracks[0].rate == 1.5F, "Nested sequence serialization lost data");
    leaf->sequenceTracks.push_back({"Root", 0.0F, 1.0F, 0.0F, 1.0F, {{"Target", "Object"}}});
    Require(!BuildTimelineSequencePlan(parentHandle, resolve, error) && error.find("cycle") != std::string::npos,
        "Nested sequence dependency cycle was accepted");
    leaf->sequenceTracks.clear();
    parent->sequenceTracks[0].bindings.clear();
    Require(!BuildTimelineSequencePlan(parentHandle, resolve, error) && error.find("remap") != std::string::npos,
        "Nested sequence missing remap was accepted");
    parent->sequenceTracks[0].bindings.push_back({"Actor", "Target"});
    parent->sequenceTracks[0].rate = 3.0F;
    Require(!BuildTimelineSequencePlan(parentHandle, resolve, error), "Nested sequence source overrun was accepted");
}

void TestTimelineEventRuntime(const std::filesystem::path& root) {
    using namespace kb::scene;
    const auto project = root / "EventRuntime";
    std::filesystem::create_directories(project / "Assets");
    TimelineAsset asset;
    asset.bindings.push_back({"Receiver", "."});
    asset.eventTracks.push_back({"Receiver", {{0.25F, 11, "Cue", "quoted \"value\"\nsecond line"},
        {0.75F, 12, "Finish", "payload"}}});
    Require(TimelineAssetIO::Save(project / "Assets" / "Events.kbtimeline", asset), "Event asset save failed");
    asset.markers = {{1.0F, 20}};
    asset.eventTracks[0].keyframes = {{1.0F, 21, "First", ""}, {1.0F, 22, "Second", ""}};
    Require(TimelineAssetIO::Save(project / "Assets" / "Mixed.kbtimeline", asset), "Mixed event asset save failed");
    {
        std::ofstream script(project / "Assets" / "Receiver.lua");
        script << R"lua(function Created(self)
  Events.Subscribe("OnTimelineEvent", function(event)
    SetShared("eventName", event.args.name)
    SetShared("eventPayload", event.args.payload)
    SetShared("eventSchema", event.args.schemaMajor)
    SetShared("eventKey", event.args.marker)
  end)
end
)lua";
    }
    Scene scene;
    Require(scene.Assets().MountProject(project) && scene.Assets().Discover() == 3, "Event discovery failed");
    const auto* metadata = scene.Assets().Manager().Registry().FindByPath("/Game/Events.kbtimeline");
    Require(metadata != nullptr, "Event metadata missing");
    const auto owner = scene.Entities().CreateObject({.name = "Owner"});
    const auto receiver = scene.Entities().CreateObject({.name = "Receiver"});
    const auto instance = scene.Timelines().Create(metadata->id.value, owner.Entity());
    Require(instance && scene.Timelines().Bind(instance, "Receiver", receiver.Entity()), "Event binding failed");
    Require(scene.Timelines().Skip(instance, 1.0F, TimelineSkipMarkerPolicy::EmitCrossed), "Event skip failed");
    auto events = scene.Timelines().DrainMarkerEvents();
    Require(events.size() == 2 && events[0].target == receiver.Entity() && events[0].markerId == 11 &&
        events[0].eventName == "Cue" && events[0].payload == "quoted \"value\"\nsecond line" && events[1].markerId == 12,
        ("Event binding, payload or forward order changed during serialization: count=" + std::to_string(events.size()) +
            (events.empty() ? "" : " name=" + events[0].eventName + " payload=" + events[0].payload +
                " key=" + std::to_string(events[0].markerId) + " target=" + std::to_string(events[0].target.Id()))).c_str());
    Require(scene.Timelines().Skip(instance, 0.0F, TimelineSkipMarkerPolicy::EmitCrossed), "Reverse event skip failed");
    events = scene.Timelines().DrainMarkerEvents();
    Require(events.size() == 2 && events[0].markerId == 12 && events[1].markerId == 11, "Reverse event order failed");
    Require(scene.Timelines().SetPlayback(instance, 8.0F, true) && scene.Timelines().Play(instance), "Event loop failed");
    static_cast<void>(scene.Runtime().Update(0.25F));
    events = scene.Timelines().DrainMarkerEvents();
    Require(events.size() == 4 && events[0].markerId == 11 && events[2].markerId == 11,
        "Event-only loops lost keys");
    Require(scene.Timelines().Seek(instance, 0.5F) && scene.Timelines().DrainMarkerEvents().empty(), "Event seek was not silent");
    Require(scene.Timelines().SetPlayback(instance, 100000.0F, true), "Event overflow setup failed");
    static_cast<void>(scene.Runtime().Update(0.25F));
    const auto errors = scene.Runtime().DrainSceneSystemErrors();
    const bool overflow = errors.size() == 1 && errors[0].find("Timeline marker queue exceeded") != std::string::npos;
    Require(overflow && NearlyEqual(scene.Timelines().Time(instance), 0.5F) && scene.Timelines().DrainMarkerEvents().empty(),
        "Event overflow changed the cursor or queue");
    const auto* mixedMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Mixed.kbtimeline");
    Require(mixedMetadata != nullptr, "Mixed event metadata missing");
    const auto mixed = scene.Timelines().Create(mixedMetadata->id.value, owner.Entity());
    Require(mixed && scene.Timelines().Skip(mixed, 1.0F, TimelineSkipMarkerPolicy::EmitCrossed), "Mixed event skip failed");
    events = scene.Timelines().DrainMarkerEvents();
    Require(events.size() == 3 && events[0].markerId == 20 && events[1].markerId == 21 && events[2].markerId == 22,
        "Equal-time mixed keys lost authored order");
    Require(scene.Timelines().Seek(mixed, 0.0F) && scene.Timelines().SetPlayback(mixed, -1.0F, true) &&
        scene.Timelines().Play(mixed), "Reverse mixed boundary setup failed");
    static_cast<void>(scene.Runtime().Update(0.25F));
    events = scene.Timelines().DrainMarkerEvents();
    Require(events.size() == 3 && events[0].markerId == 22 && events[1].markerId == 21 && events[2].markerId == 20,
        "Reverse wrap did not reverse equal-time boundary events");
    Require(scene.Timelines().Pause(mixed), "Mixed event pause failed");
    const auto* script = scene.Assets().Manager().Registry().FindByPath("/Game/Receiver.lua");
    Require(script != nullptr, "Event receiver script missing");
    scene.Components().Behaviours().Set(receiver.Entity(), {
        .behaviourAssetId = script->id.value, .backend = BehaviourBackend::Lua, .enabled = true});
    kb::script::ScriptRuntimeHost host{scene};
    Require(host.Succeeded() && host.InstallSceneSystem(), "Event script host failed");
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(scene.Timelines().Seek(instance, 0.0F) && scene.Timelines().SetPlayback(instance, 1.0F, false) &&
        scene.Timelines().Play(instance), "Script event playback setup failed");
    static_cast<void>(scene.Runtime().Update(0.25F));
    const auto name = host.SharedState().Get("eventName");
    const auto payload = host.SharedState().Get("eventPayload");
    const auto schema = host.SharedState().Get("eventSchema");
    const auto key = host.SharedState().Get("eventKey");
    Require(name && payload && schema && key && name->AsString() == "Cue" &&
        payload->AsString() == "quoted \"value\"\nsecond line" && schema->AsInt() == 1 && key->AsInt() == 11,
        "Bound Timeline event did not reach Lua with the declared schema and payload");
}

void TestNestedTimelineRuntime(const std::filesystem::path& root) {
    using namespace kb::scene;
    const auto project = root / "NestedRuntime";
    std::filesystem::create_directories(project / "Assets");
    TimelineAsset child;
    child.bindings.push_back({"Actor", "."});
    child.transformTracks.push_back({"Actor", {{0.0F, {.position = {0.0F, 0.0F, 0.0F}}},
        {1.0F, {.position = {10.0F, 0.0F, 0.0F}}}}});
    child.eventTracks.push_back({"Actor", {{0.25F, 31, "First", ""}, {0.75F, 32, "Second", ""}}});
    TimelineAsset parent;
    parent.bindings.push_back({"Target", "."});
    parent.sequenceTracks.push_back({"/Game/Child.kbtimeline", 0.25F, 0.75F, 0.0F, 2.0F, {{"Actor", "Target"}}});
    Require(TimelineAssetIO::Save(project / "Assets" / "Child.kbtimeline", child) &&
        TimelineAssetIO::Save(project / "Assets" / "Parent.kbtimeline", parent), "Nested runtime assets could not be saved");
    Scene scene;
    Require(scene.Assets().MountProject(project) && scene.Assets().Discover() == 2, "Nested runtime discovery failed");
    const auto* metadata = scene.Assets().Manager().Registry().FindByPath("/Game/Parent.kbtimeline");
    const auto* childMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Child.kbtimeline");
    Require(metadata && childMetadata, "Nested runtime metadata missing");
    const auto owner = scene.Entities().CreateObject({.name = "Director"});
    const auto target = scene.Entities().CreateObject({.name = "Target"});
    const auto instance = scene.Timelines().Create(metadata->id.value, owner.Entity());
    Require(instance && scene.Timelines().Bind(instance, "Target", target.Entity()) &&
        scene.Timelines().Seek(instance, 0.5F), "Nested runtime could not bind or seek");
    Require(NearlyEqual(scene.Transforms().Get(target.Entity()).localPosition.x, 5.0F) &&
        NearlyEqual(scene.Transforms().Get(owner.Entity()).localPosition.x, 0.0F) &&
        scene.Timelines().DrainMarkerEvents().empty(), "Nested scrub changed the wrong object or emitted events");
    Require(scene.Timelines().Seek(instance, 0.75F) &&
        scene.Timelines().Skip(instance, 0.25F, TimelineSkipMarkerPolicy::EmitCrossed), "Nested reverse skip failed");
    auto events = scene.Timelines().DrainMarkerEvents();
    Require(events.size() == 2 && events[0].markerId == 32 && events[1].markerId == 31 &&
        events[0].target == target.Entity() && events[0].assetId == childMetadata->id.value &&
        NearlyEqual(events[0].timeSeconds, 0.625F), "Nested event traversal or source identity changed");
    Require(scene.Timelines().Seek(instance, 0.875F) && scene.Timelines().SetPlayback(instance, 2.0F, true) &&
        scene.Timelines().Play(instance), "Nested loop setup failed");
    static_cast<void>(scene.Runtime().Update(0.25F));
    events = scene.Timelines().DrainMarkerEvents();
    Require(events.size() == 1 && events[0].markerId == 31 &&
        NearlyEqual(scene.Transforms().Get(target.Entity()).localPosition.x, 2.5F), "Nested loop lost its pose or boundary event");
    Require(scene.Timelines().SetPlayback(instance, -1.0F, false), "Nested reverse playback setup failed");
    static_cast<void>(scene.Runtime().Update(0.0625F));
    Require(NearlyEqual(scene.Transforms().Get(target.Entity()).localPosition.x, 1.25F), "Nested reverse pose was not sampled");
    const auto rebound = scene.Entities().CreateObject({.name = "Rebound"});
    Require(scene.Timelines().Bind(instance, "Target", rebound.Entity()) &&
        NearlyEqual(scene.Transforms().Get(rebound.Entity()).localPosition.x, 1.25F), "Nested binding override was not propagated");
    Require(scene.Timelines().Pause(instance), "Nested reload pause failed");
    child.transformTracks[0].keyframes.back().transform.position.x = 20.0F;
    const auto childId = childMetadata->id;
    Require(TimelineAssetIO::Save(project / "Assets" / "Child.kbtimeline", child) &&
        scene.Assets().Manager().RefreshAsset(childId), "Nested source refresh failed");
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(NearlyEqual(scene.Transforms().Get(rebound.Entity()).localPosition.x, 2.5F) &&
        scene.Timelines().DrainMarkerEvents().empty(), "Nested reload lost its override, pose or silent cursor");
}

void TestTimelinePropertyRuntime(const std::filesystem::path& root) {
    using namespace kb::scene;
    using Type = kb::ecs::ComponentFieldType;
    kb::ecs::World schemaWorld;
    const auto* reflection = schemaWorld.RegisterComponentReflection<UICanvasGroup>(UICanvasGroup::StableId, {
        KB_ECS_FIELD(UICanvasGroup, opacity, Type::Float32),
        KB_ECS_FIELD(UICanvasGroup, interactable, Type::Bool),
        KB_ECS_FIELD(UICanvasGroup, blocksRaycasts, Type::Bool),
        KB_ECS_FIELD(UICanvasGroup, ignoreParentGroups, Type::Bool)});
    Require(reflection != nullptr, "Property runtime reflection fixture failed");
    const auto project = root / "PropertyRuntime";
    std::filesystem::create_directories(project / "Assets");
    TimelineAsset asset;
    asset.bindings.push_back({"Target", "."});
    asset.propertyTracks.push_back({
        .binding = "Target",
        .property = {1, std::string{UICanvasGroup::StableId}, "opacity", Type::Float32, TimelineComponentSchema(*reflection)},
        .keyframes = {{0.0F, 0.0F}, {1.0F, 1.0F}}});
    Require(TimelineAssetIO::Save(project / "Assets" / "Fade.kbtimeline", asset),
        "Runtime property asset could not be saved");
    asset.markers = {{0.25F, 1}, {0.75F, 2}, {1.0F, 3}};
    Require(TimelineAssetIO::Save(project / "Assets" / "Marked.kbtimeline", asset),
        "Marked loop asset could not be saved");
    Scene scene;
    Require(scene.Assets().MountProject(project) && scene.Assets().Discover() == 2,
        "Runtime property asset discovery failed");
    const auto* metadata = scene.Assets().Manager().Registry().FindByPath("/Game/Fade.kbtimeline");
    Require(metadata != nullptr, "Runtime property asset metadata missing");
    const auto owner = scene.Entities().CreateObject({.name = "PropertyTarget"});
    UICanvasGroup group;
    group.blocksRaycasts = false;
    scene.Components().UI().Set(owner.Entity(), group);
    const auto instance = scene.Timelines().Create(metadata->id.value, owner.Entity());
    Require(instance != 0 && scene.Timelines().Seek(instance, 0.5F), "Property timeline could not seek");
    const auto* sampled = scene.Components().UI().TryGet<UICanvasGroup>(owner.Entity());
    Require(sampled && NearlyEqual(sampled->opacity, 0.5F) && !sampled->blocksRaycasts,
        "Property playback failed to update its field or changed unrelated component data");
    Require(scene.Timelines().Play(instance), "Property timeline could not play");
    static_cast<void>(scene.Runtime().Update(0.25F));
    Require(NearlyEqual(scene.Components().UI().TryGet<UICanvasGroup>(owner.Entity())->opacity, 0.75F),
        "Property timeline did not advance with the scene");
    Require(!scene.Timelines().SetPlayback(instance, 0.0F, false) &&
        !scene.Timelines().SetPlayback(instance, std::numeric_limits<float>::infinity(), false),
        "Invalid timeline playback rate was accepted");
    Require(scene.Timelines().SetPlayback(instance, -1.0F, false), "Reverse playback configuration failed");
    static_cast<void>(scene.Runtime().Update(0.25F));
    Require(NearlyEqual(scene.Timelines().Time(instance), 0.5F) &&
        NearlyEqual(scene.Components().UI().TryGet<UICanvasGroup>(owner.Entity())->opacity, 0.5F),
        "Reverse playback did not sample property values");
    Require(scene.Timelines().Seek(instance, 0.125F), "Reverse boundary seek failed");
    static_cast<void>(scene.Runtime().Update(0.25F));
    Require(scene.Timelines().Time(instance) == 0.0F && !scene.Timelines().IsPlaying(instance),
        "Reverse playback did not stop at zero");
    Require(scene.Timelines().SetPlayback(instance, -1.0F, true) && scene.Timelines().Play(instance),
        "Reverse looping could not start at zero");
    static_cast<void>(scene.Runtime().Update(0.25F));
    Require(NearlyEqual(scene.Timelines().Time(instance), 0.75F) && scene.Timelines().IsPlaying(instance),
        "Reverse loop did not wrap at zero");
    Require(scene.Timelines().SetPlayback(instance, 2.0F, true), "Forward looping configuration failed");
    static_cast<void>(scene.Runtime().Update(0.25F));
    Require(NearlyEqual(scene.Timelines().Time(instance), 0.25F), "Forward loop ignored playback rate or wrap");
    Require(scene.Timelines().Pause(instance), "Property timeline could not pause");
    const auto* markedMetadata = scene.Assets().Manager().Registry().FindByPath("/Game/Marked.kbtimeline");
    Require(markedMetadata != nullptr, "Marked timeline metadata missing");
    const auto marked = scene.Timelines().Create(markedMetadata->id.value, owner.Entity());
    Require(marked != 0 && scene.Timelines().Seek(marked, 0.875F) &&
        scene.Timelines().SetPlayback(marked, 2.0F, true) && scene.Timelines().Play(marked),
        "Marked forward loop could not start");
    static_cast<void>(scene.Runtime().Update(0.25F));
    auto events = scene.Timelines().DrainMarkerEvents();
    Require(NearlyEqual(scene.Timelines().Time(marked), 0.375F) && events.size() == 2 &&
        events[0].markerId == 3 && events[1].markerId == 1,
        "Forward wrap lost or reordered boundary markers");
    Require(scene.Timelines().SetPlayback(marked, -2.0F, true), "Marked reverse loop configuration failed");
    static_cast<void>(scene.Runtime().Update(0.25F));
    events = scene.Timelines().DrainMarkerEvents();
    Require(NearlyEqual(scene.Timelines().Time(marked), 0.875F) && events.size() == 2 &&
        events[0].markerId == 1 && events[1].markerId == 3,
        "Reverse wrap lost or reordered boundary markers");
    Require(scene.Timelines().Seek(marked, 0.875F) && scene.Timelines().DrainMarkerEvents().empty(),
        "Scrubbing emitted timeline markers");
    Require(scene.Timelines().Skip(marked, 0.125F, TimelineSkipMarkerPolicy::EmitCrossed),
        "Reverse skip was rejected");
    events = scene.Timelines().DrainMarkerEvents();
    Require(events.size() == 2 && events[0].markerId == 2 && events[1].markerId == 1,
        "Reverse skip did not emit markers in traversal order");
    Require(scene.Timelines().SetPlayback(marked, 8.0F, true), "Multiple loop configuration failed");
    static_cast<void>(scene.Runtime().Update(0.25F));
    events = scene.Timelines().DrainMarkerEvents();
    Require(NearlyEqual(scene.Timelines().Time(marked), 0.125F) && events.size() == 6 &&
        events[0].markerId == 1 && events[2].markerId == 3 && events[3].markerId == 1 && events[5].markerId == 3,
        "Multiple loops in one frame lost crossed markers");
    Require(scene.Timelines().Release(marked), "Marked timeline could not release");
    scene.Components().UI().Remove<UICanvasGroup>(owner.Entity());
    bool removedDetected = false;
    try { static_cast<void>(scene.Timelines().Seek(instance, 0.0F)); }
    catch (const std::runtime_error& error) {
        removedDetected = std::string{error.what()}.find("removed") != std::string::npos;
    }
    Require(removedDetected, "Property playback did not diagnose a removed component");
}

void TestTimelinePropertyContracts(const std::filesystem::path& root) {
    using namespace kb::scene;
    using Type = kb::ecs::ComponentFieldType;
    using Value = kb::ecs::ComponentFieldValue;
    const kb::ecs::ComponentReflection reflection{1, "AnimatedValue", sizeof(double),
        {{"value", Type::Float64, 0, sizeof(double)}}};
    TimelinePropertyTrack track{
        .binding = "Target",
        .property = {1, "AnimatedValue", "value", Type::Float64, TimelineComponentSchema(reflection)},
        .keyframes = {{0.0F, 0.125}, {1.0F, 0.875}}};
    std::string error;
    Value value;
    Require(ValidateTimelinePropertyBinding(track.property, reflection, error) &&
        ValidateTimelinePropertyTrack(track, 1.0F, error), "Valid property binding was rejected");
    Require(SampleTimelineProperty(track, 0.5F, value, error) && std::get<double>(value) == 0.5,
        "Double property interpolation lost precision");
    const kb::ecs::ComponentReflection relocated{42, "AnimatedValue", 32,
        {{"value", Type::Float64, 16, sizeof(double)}}};
    Require(TimelineComponentSchema(reflection) == TimelineComponentSchema(relocated),
        "Property schema depends on native component IDs or memory layout");
    const kb::ecs::ComponentReflection renamed{1, "RenamedValue", sizeof(double),
        {{"value", Type::Float64, 0, sizeof(double)}}};
    Require(!ValidateTimelinePropertyBinding(track.property, renamed, error),
        "Renamed component retained a stale property binding");
    auto changed = track.property;
    changed.field = "removed";
    Require(!ValidateTimelinePropertyBinding(changed, reflection, error), "Missing property was accepted");
    changed = track.property;
    changed.type = Type::Float32;
    Require(!ValidateTimelinePropertyBinding(changed, reflection, error), "Changed property type was accepted");
    changed = track.property;
    ++changed.version;
    Require(!ValidateTimelinePropertyBinding(changed, reflection, error), "Unknown binding version was accepted");
    changed = track.property;
    ++changed.componentSchema;
    Require(!ValidateTimelinePropertyBinding(changed, reflection, error), "Stale schema was accepted");

    TimelineAsset asset;
    asset.bindings.push_back({"Target", "."});
    asset.propertyTracks.push_back(track);
    track.property.type = Type::Vec4Float32;
    track.property.field = "color";
    track.keyframes = {{0.0F, std::array<float, 4>{0, 0.25F, 0.5F, 1}},
        {1.0F, std::array<float, 4>{1, 0.75F, 0.5F, 0}}};
    Require(ValidateTimelinePropertyTrack(track, 1.0F, error) &&
        SampleTimelineProperty(track, 0.5F, value, error) &&
        std::get<std::array<float, 4>>(value) == std::array<float, 4>{0.5F, 0.5F, 0.5F, 0.5F},
        "Color properties did not interpolate all channels");
    asset.propertyTracks.push_back(track);
    track.property.type = Type::Bool;
    track.property.field = "enabled";
    track.interpolation = TimelinePropertyInterpolation::Step;
    track.keyframes = {{0.0F, false}, {0.5F, true}};
    Require(ValidateTimelinePropertyTrack(track, 1.0F, error) &&
        SampleTimelineProperty(track, 0.499F, value, error) && !std::get<bool>(value) &&
        SampleTimelineProperty(track, 0.5F, value, error) && std::get<bool>(value),
        "Discrete property changed outside its key boundary");
    asset.propertyTracks.push_back(track);
    const auto path = root / "Properties.kbtimeline";
    Require(TimelineAssetIO::Save(path, asset), "Property asset could not be saved");
    const auto loaded = TimelineAssetIO::Load(path);
    Require(loaded && loaded->propertyTracks.size() == 3 &&
        loaded->propertyTracks[0].keyframes[1].value == asset.propertyTracks[0].keyframes[1].value &&
        loaded->propertyTracks[1].keyframes[0].value == asset.propertyTracks[1].keyframes[0].value &&
        loaded->propertyTracks[2].keyframes[1].value == asset.propertyTracks[2].keyframes[1].value &&
        loaded->propertyTracks[0].property.componentSchema == asset.propertyTracks[0].property.componentSchema,
        "Property asset roundtrip changed values or binding schema");
    track.interpolation = TimelinePropertyInterpolation::Linear;
    Require(!ValidateTimelinePropertyTrack(track, 1.0F, error), "Boolean interpolation accepted linear mode");
    track = asset.propertyTracks.front();
    track.keyframes[1].timeSeconds = 0.0F;
    Require(!ValidateTimelinePropertyTrack(track, 1.0F, error), "Duplicate property key times were accepted");
    track.keyframes[1] = {1.0F, std::numeric_limits<double>::infinity()};
    Require(!ValidateTimelinePropertyTrack(track, 1.0F, error), "Nonfinite property values were accepted");
    track.keyframes[1].value = true;
    Require(!ValidateTimelinePropertyTrack(track, 1.0F, error), "Mismatched property key type was accepted");
    std::istringstream legacy{"kbtimeline 1\nduration 1\nbinding \"Target\" \".\"\nmarker 0.5 123\n"};
    const auto old = TimelineAssetIO::Load(legacy);
    Require(old && old->propertyTracks.empty() && old->markers.size() == 1,
        "Legacy timeline no longer loads");
    for (const auto* malformed : {
        "kbtimeline 3\nduration 1\n",
        "kbtimeline 2\nduration 1\nproperty-key 0 0 1\n",
        "kbtimeline 2\nduration 1\nbinding \"Target\" \".\"\nproperty \"Target\" 1 \"C\" \"f\" 99 1 0\n"}) {
        std::istringstream input{malformed};
        Require(!TimelineAssetIO::Load(input), "Malformed property timeline was accepted");
    }
}
} // namespace

void RunTimelineRuntimeTests() {
    constexpr kb::scene::TimelineMarkerId kFirstMarker = 0x710001U;
    constexpr kb::scene::TimelineMarkerId kSecondMarker = 0x710002U;
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        "21kb-timeline-runtime-tests";
    std::filesystem::remove_all(root);
    const std::filesystem::path assetPath =
        root / "Assets" / "Cinematics" / "Intro.kbtimeline";
    const std::filesystem::path scriptPath =
        root / "Assets" / "Logic" / "Timeline.lua";
    std::filesystem::create_directories(assetPath.parent_path());
    std::filesystem::create_directories(scriptPath.parent_path());
    TestTimelinePropertyContracts(root);
    TestTimelinePropertyRuntime(root);
    RunTimelineAudioTests(root);
    TestTimelineEventRuntime(root);
    TestTimelineSequencePlan(root);
    TestNestedTimelineRuntime(root);
    TestTimelineAnimationRuntime(root);
    try { TestTimelineSkeletalAnimation(root); }
    catch (const std::exception& error) { Require(false, error.what()); }

    kb::scene::TimelineAsset asset{};
    asset.durationSeconds = 1.0F;
    asset.bindings.push_back({ .name = "Hero", .defaultPath = "." });
    asset.transformTracks.push_back({
        .binding = "Hero",
        .keyframes = {
            { .timeSeconds = 0.0F,
              .transform = { .position = { 0.0F, 0.0F, 0.0F } } },
            { .timeSeconds = 1.0F,
              .transform = { .position = { 10.0F, 0.0F, 0.0F } } },
        },
    });
    asset.markers = {
        { .timeSeconds = 0.5F, .id = kFirstMarker },
        { .timeSeconds = 0.75F, .id = kSecondMarker },
    };
    Require(kb::scene::TimelineAssetIO::Save(assetPath, asset),
        "Timeline production asset could not be saved");

    kb::scene::Scene scene;
    Require(scene.Assets().MountProject(root),
        "Timeline runtime project could not be mounted");
    const kb::scene::SceneObject owner =
        scene.Entities().CreateObject({ .name = "Director" });
    const kb::scene::SceneObject target =
        scene.Entities().CreateObject({ .name = "Bound Hero" });
    {
        std::ofstream script(scriptPath, std::ios::trunc);
        script << "function Created(self)\n"
            "  Events.Subscribe(\"OnTimelineMarker\", function(event)\n"
            "    SetShared(\"timelineMarker\", event.args.marker)\n"
            "    SetShared(\"timelineSchemaMajor\", event.args.schemaMajor)\n"
            "    SetShared(\"timelineMarkerTime\", event.args.time)\n"
            "  end)\n"
            "end\n"
            "function Tick(self)\n"
            "  if GetShared(\"timelineCreated\") then return end\n"
            "  local instance, err = Timeline.Create(\"/Game/Cinematics/Intro.kbtimeline\")\n"
            "  if err then SetShared(\"timelineError\", err) return end\n"
            "  local bound = Timeline.Bind(instance, \"Hero\", "
            << target.Entity().Id() << ")\n"
            "  local seeked = Timeline.Seek(instance, 0.25)\n"
            "  local played = Timeline.Play(instance)\n"
            "  assert(Timeline.SetPlayback(instance, 1.0, false))\n"
            "  assert(CallFunction(\"Timeline.IsPlaying\", { instance = instance }))\n"
            "  assert(CallFunction(\"Timeline.Pause\", { instance = instance }))\n"
            "  assert(not CallFunction(\"Timeline.IsPlaying\", { instance = instance }))\n"
            "  assert(Timeline.Play(instance))\n"
            "  SetShared(\"timelineInstance\", instance)\n"
            "  SetShared(\"timelineCreated\", bound and seeked and played)\n"
            "end\n";
    }

    Require(scene.Assets().Discover() == 2U,
        "Timeline project discovery did not register asset and script");
    const kb::assets::AssetMetadata* timelineMetadata =
        scene.Assets().Manager().Registry().FindByPath(
            "/Game/Cinematics/Intro.kbtimeline");
    const kb::assets::AssetMetadata* scriptMetadata =
        scene.Assets().Manager().Registry().FindByPath(
            "/Game/Logic/Timeline.lua");
    Require(timelineMetadata != nullptr &&
            timelineMetadata->type == kb::scene::kTimelineAssetType &&
            scriptMetadata != nullptr,
        "Timeline/script assets were not classified by production loaders");
    scene.Components().Behaviours().Set(owner.Entity(), {
        .behaviourAssetId = scriptMetadata->id.value,
        .backend = kb::scene::BehaviourBackend::Lua,
        .enabled = true,
    });

    kb::script::ScriptRuntimeHost host{ scene };
    Require(host.Succeeded() && host.InstallSceneSystem(),
        "Timeline script runtime host did not install into the scene");
    Require(host.Functions().FindSignature("Timeline.Skip") != nullptr &&
            host.VisualGraphRuntimeBindings().Find(
                kb::visual::VisualGraphIrOpcode::CallNative,
                "Function.Timeline.Bind") != nullptr,
        "Timeline API was not registered for script and Visual Graph runtime");

    static_cast<void>(scene.Runtime().Update(0.0F));
    static_cast<void>(scene.Runtime().Update(0.0F));
    const auto timelineError = host.SharedState().Get("timelineError");
    const std::string timelineFailure = timelineError.has_value()
        ? std::string{ "Lua timeline error: " } + timelineError->AsString()
        : "Lua did not create, bind, seek and play the scene timeline";
    Require(host.SharedState().Get("timelineCreated").has_value() &&
            host.SharedState().Get("timelineCreated")->AsBool() &&
            !timelineError.has_value(),
        timelineFailure.c_str());
    const std::uint64_t instance =
        static_cast<std::uint64_t>(
            host.SharedState().Get("timelineInstance")->AsInt());
    const float timelineTime = scene.Timelines().Time(instance);
    const float targetPosition =
        scene.Transforms().Get(target.Entity()).localPosition.x;
    const std::string seekFailure =
        "Timeline seek state: exists=" +
        std::to_string(scene.Timelines().Exists(instance)) +
        " time=" + std::to_string(timelineTime) +
        " x=" + std::to_string(targetPosition);
    Require(scene.Timelines().Exists(instance) &&
            NearlyEqual(timelineTime, 0.25F) &&
            NearlyEqual(targetPosition, 2.5F),
        seekFailure.c_str());

    static_cast<void>(scene.Runtime().Update(0.3F));
    const auto runtimeMarker = host.SharedState().Get("timelineMarker");
    const auto runtimeSchema = host.SharedState().Get("timelineSchemaMajor");
    const auto runtimeMarkerTime =
        host.SharedState().Get("timelineMarkerTime");
    const std::string markerFailure =
        "Timeline runtime state: time=" +
        std::to_string(scene.Timelines().Time(instance)) +
        " x=" + std::to_string(
            scene.Transforms().Get(target.Entity()).localPosition.x) +
        " marker=" + std::to_string(
            runtimeMarker.has_value() ? runtimeMarker->AsInt(-1) : -2) +
        " schema=" + std::to_string(
            runtimeSchema.has_value() ? runtimeSchema->AsInt(-1) : -2) +
        " markerTime=" + std::to_string(
            runtimeMarkerTime.has_value()
                ? runtimeMarkerTime->AsFloat(-1.0F)
                : -2.0F);
    Require(NearlyEqual(
                scene.Transforms().Get(target.Entity()).localPosition.x,
                5.5F) &&
            runtimeMarker.has_value() &&
            static_cast<kb::scene::TimelineMarkerId>(
                runtimeMarker->AsInt()) ==
                kFirstMarker &&
            runtimeSchema->AsInt() == 1 &&
            NearlyEqual(
                runtimeMarkerTime->AsFloat(),
                0.5F),
        markerFailure.c_str());

    Require(scene.Timelines().Pause(instance),
        "Timeline pause rejected a live instance");
    static_cast<void>(scene.Runtime().Update(0.2F));
    Require(NearlyEqual(scene.Timelines().Time(instance), 0.55F),
        "Paused timeline advanced");

    const auto callTimeline = [&](const char* name, std::initializer_list<kb::script::ScriptFunctionArgument> arguments) {
        const auto result = host.Functions().Call(name, {arguments.begin(), arguments.size()},
            kb::script::ScriptFunctionCallContext{.scene = &scene, .caller = owner.Entity()});
        Require(result.executed && result.Succeeded(), name);
        return result;
    };
    using kb::script::ScriptValue;
    const ScriptValue instanceValue{instance, kb::script::ScriptValueType::Hash};
    Require(NearlyEqual(callTimeline("Timeline.Time", {{"instance", instanceValue}}).Output("time")->AsFloat(), 0.55F),
        "Timeline.Time did not read the paused cursor");
    const auto skipViaApi = [&](bool emit) {
        return callTimeline("Timeline.Skip", {{"instance", instanceValue}, {"time", ScriptValue{0.8F}}, {"emitMarkers", ScriptValue{emit}}}).Output("applied")->AsBool();
    };
    static_cast<void>(scene.Timelines().DrainMarkerEvents());
    Require(scene.Timelines().Seek(instance, 0.1F) &&
            skipViaApi(false) &&
            scene.Timelines().DrainMarkerEvents().empty(),
        "Timeline suppress-marker skip emitted a marker");
    Require(scene.Timelines().Seek(instance, 0.1F) &&
            skipViaApi(true),
        "Timeline emit-marker skip failed");
    const auto skippedMarkers = scene.Timelines().DrainMarkerEvents();
    Require(skippedMarkers.size() == 2U &&
            skippedMarkers[0].markerId == kFirstMarker &&
            skippedMarkers[1].markerId == kSecondMarker,
        "Timeline skip did not emit crossed markers deterministically");

    const auto extra = callTimeline("Timeline.Create", {{"asset", ScriptValue{std::string{"/Game/Cinematics/Intro.kbtimeline"}}}}).Output("instance")->AsUInt64();
    Require(scene.Timelines().Exists(extra) && callTimeline("Timeline.Release", {{"instance", ScriptValue{extra, kb::script::ScriptValueType::Hash}}}).Output("applied")->AsBool() &&
        !scene.Timelines().Exists(extra), "Timeline.Release did not destroy the owned instance");
    scene.Entities().Destroy(owner);
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(!scene.Timelines().Exists(instance),
        "Timeline instance outlived its owner");
    std::filesystem::remove_all(root);
}

} // namespace kb::tests
