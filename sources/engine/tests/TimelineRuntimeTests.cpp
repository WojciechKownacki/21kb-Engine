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
#include "engine/scene/TimelineAssetIO.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <filesystem>
#include <fstream>

namespace kb::tests {

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

    static_cast<void>(scene.Timelines().DrainMarkerEvents());
    Require(scene.Timelines().Seek(instance, 0.1F) &&
            scene.Timelines().Skip(
                instance, 0.8F,
                kb::scene::TimelineSkipMarkerPolicy::Suppress) &&
            scene.Timelines().DrainMarkerEvents().empty(),
        "Timeline suppress-marker skip emitted a marker");
    Require(scene.Timelines().Seek(instance, 0.1F) &&
            scene.Timelines().Skip(
                instance, 0.8F,
                kb::scene::TimelineSkipMarkerPolicy::EmitCrossed),
        "Timeline emit-marker skip failed");
    const auto skippedMarkers = scene.Timelines().DrainMarkerEvents();
    Require(skippedMarkers.size() == 2U &&
            skippedMarkers[0].markerId == kFirstMarker &&
            skippedMarkers[1].markerId == kSecondMarker,
        "Timeline skip did not emit crossed markers deterministically");

    scene.Entities().Destroy(owner);
    static_cast<void>(scene.Runtime().Update(0.0F));
    Require(!scene.Timelines().Exists(instance),
        "Timeline instance outlived its owner");
    std::filesystem::remove_all(root);
}

} // namespace kb::tests
