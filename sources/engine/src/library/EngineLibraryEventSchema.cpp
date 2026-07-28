#include "engine/library/EngineLibraryEventSchema.hpp"
#include "engine/scene/SceneTimelines.hpp"

#include <algorithm>

namespace kb::library {

namespace {

using kb::script::ScriptFunctionPin;
using kb::script::ScriptValueType;

// Every one of these five carries exactly the same shape — see
// ScriptRuntimeSceneSystem.cpp::DispatchPendingSceneLifecycleEvents.
[[nodiscard]] std::vector<LibraryEventArgumentDesc> SceneLifecycleArguments() {
    return {
        ScriptFunctionPin{ "sceneId", ScriptValueType::Hash, true },
        ScriptFunctionPin{ "sceneName", ScriptValueType::String, true },
    };
}

// LIB-108: all six OnCollision*/OnTrigger* events carry the identical shape —
// see ScriptRuntimeSceneSystem.cpp::DispatchPendingCollisionEvents.
[[nodiscard]] std::vector<LibraryEventArgumentDesc> CollisionArguments() {
    return {
        ScriptFunctionPin{ "other", ScriptValueType::Entity, true },
        ScriptFunctionPin{ "pointX", ScriptValueType::Float, true },
        ScriptFunctionPin{ "pointY", ScriptValueType::Float, true },
        ScriptFunctionPin{ "pointZ", ScriptValueType::Float, true },
        ScriptFunctionPin{ "normalX", ScriptValueType::Float, true },
        ScriptFunctionPin{ "normalY", ScriptValueType::Float, true },
        ScriptFunctionPin{ "normalZ", ScriptValueType::Float, true },
    };
}

[[nodiscard]] std::vector<LibraryEventArgumentDesc> UIElementArguments() {
    return {
        ScriptFunctionPin{ "owner", ScriptValueType::Entity, true },
        ScriptFunctionPin{ "element", ScriptValueType::Hash, true },
    };
}

[[nodiscard]] std::vector<LibraryEventArgumentDesc> UIPointerArguments() {
    std::vector<LibraryEventArgumentDesc> arguments = UIElementArguments();
    arguments.push_back(ScriptFunctionPin{ "x", ScriptValueType::Float, true });
    arguments.push_back(ScriptFunctionPin{ "y", ScriptValueType::Float, true });
    return arguments;
}

} // namespace

const std::vector<LibraryEventDesc>& EngineLibraryEventRegistry::Catalog() {
    // Argument shapes verified directly against ScriptRuntimeSceneSystem.cpp
    // before writing this (DispatchPendingSceneLifecycleEvents/
    // DispatchFiredTimers/DispatchCompletedTasks/
    // DispatchCompletedFixedStepTasks) — RunEngineLibraryEventSchemaRegistryTest
    // cross-checks this list against a REAL dispatch through
    // ScriptRuntimeSceneSystem::ExecuteFrame so it cannot silently drift.
    static const std::vector<LibraryEventDesc> kCatalog{
        LibraryEventDesc{
            .name = "SceneLoading",
            .id = kb::script::ComputeEventId("SceneLoading"),
            .arguments = SceneLifecycleArguments(),
        },
        LibraryEventDesc{
            .name = "SceneLoaded",
            .id = kb::script::ComputeEventId("SceneLoaded"),
            .arguments = SceneLifecycleArguments(),
        },
        LibraryEventDesc{
            .name = "SceneActivated",
            .id = kb::script::ComputeEventId("SceneActivated"),
            .arguments = SceneLifecycleArguments(),
        },
        LibraryEventDesc{
            .name = "SceneUnloading",
            .id = kb::script::ComputeEventId("SceneUnloading"),
            .arguments = SceneLifecycleArguments(),
        },
        LibraryEventDesc{
            .name = "SceneUnloaded",
            .id = kb::script::ComputeEventId("SceneUnloaded"),
            .arguments = SceneLifecycleArguments(),
        },
        LibraryEventDesc{
            .name = "TimerFired",
            .id = kb::script::ComputeEventId("TimerFired"),
            .arguments = { ScriptFunctionPin{ "timer", ScriptValueType::Hash, true } },
        },
        LibraryEventDesc{
            .name = "TaskCompleted",
            .id = kb::script::ComputeEventId("TaskCompleted"),
            .arguments = { ScriptFunctionPin{ "task", ScriptValueType::Hash, true } },
        },
        LibraryEventDesc{
            .name = "TaskFailed",
            .id = kb::script::ComputeEventId("TaskFailed"),
            .arguments = { ScriptFunctionPin{ "task", ScriptValueType::Hash, true } },
        },
        // LIB-108 (audit gap closed 2026-07-18): the physics collision/trigger,
        // audio-marker, and prefab-instantiation events the engine ALSO emits
        // from ScriptRuntimeSceneSystem (DispatchPendingCollisionEvents/
        // DispatchPendingAudioMarkerEvents/DispatchPendingPrefabInstantiated
        // Events) — previously missing from this schema, so scripts had no
        // versioned contract for them despite the engine emitting them.
        LibraryEventDesc{
            .name = "OnCollisionEnter",
            .id = kb::script::ComputeEventId("OnCollisionEnter"),
            .arguments = CollisionArguments(),
        },
        LibraryEventDesc{
            .name = "OnCollisionStay",
            .id = kb::script::ComputeEventId("OnCollisionStay"),
            .arguments = CollisionArguments(),
        },
        LibraryEventDesc{
            .name = "OnCollisionExit",
            .id = kb::script::ComputeEventId("OnCollisionExit"),
            .arguments = CollisionArguments(),
        },
        LibraryEventDesc{
            .name = "OnTriggerEnter",
            .id = kb::script::ComputeEventId("OnTriggerEnter"),
            .arguments = CollisionArguments(),
        },
        LibraryEventDesc{
            .name = "OnTriggerStay",
            .id = kb::script::ComputeEventId("OnTriggerStay"),
            .arguments = CollisionArguments(),
        },
        LibraryEventDesc{
            .name = "OnTriggerExit",
            .id = kb::script::ComputeEventId("OnTriggerExit"),
            .arguments = CollisionArguments(),
        },
        LibraryEventDesc{
            .name = "OnAudioMarker",
            .id = kb::script::ComputeEventId("OnAudioMarker"),
            .arguments = {
                ScriptFunctionPin{ "voice", ScriptValueType::Int, true },
                ScriptFunctionPin{ "marker", ScriptValueType::String, true },
                ScriptFunctionPin{ "positionSeconds", ScriptValueType::Float, true },
            },
        },
        LibraryEventDesc{
            .name = "OnPrefabInstantiated",
            .id = kb::script::ComputeEventId("OnPrefabInstantiated"),
            .arguments = {
                ScriptFunctionPin{ "root", ScriptValueType::Entity, true },
                ScriptFunctionPin{ "count", ScriptValueType::Int, true },
            },
        },
        LibraryEventDesc{
            .name = "OnAnimationEvent",
            .id = kb::script::ComputeEventId("OnAnimationEvent"),
            .arguments = {
                ScriptFunctionPin{ "schemaMajor", ScriptValueType::Int, true },
                ScriptFunctionPin{ "schemaMinor", ScriptValueType::Int, true },
                ScriptFunctionPin{ "event", ScriptValueType::Hash, true },
                ScriptFunctionPin{ "clip", ScriptValueType::Hash, true },
                ScriptFunctionPin{ "layer", ScriptValueType::String, true },
                ScriptFunctionPin{ "state", ScriptValueType::String, true },
                ScriptFunctionPin{ "normalizedTime", ScriptValueType::Float, true },
            },
        },
        LibraryEventDesc{
            .name = "OnTimelineMarker",
            .id = kb::script::ComputeEventId("OnTimelineMarker"),
            .version = {
                static_cast<std::uint16_t>(
                    kb::scene::TimelineMarkerEvent::kSchemaMajor),
                static_cast<std::uint16_t>(
                    kb::scene::TimelineMarkerEvent::kSchemaMinor),
            },
            .arguments = {
                ScriptFunctionPin{ "schemaMajor", ScriptValueType::Int, true },
                ScriptFunctionPin{ "schemaMinor", ScriptValueType::Int, true },
                ScriptFunctionPin{ "instance", ScriptValueType::Hash, true },
                ScriptFunctionPin{ "asset", ScriptValueType::Hash, true },
                ScriptFunctionPin{ "marker", ScriptValueType::Hash, true },
                ScriptFunctionPin{ "time", ScriptValueType::Float, true },
            },
        },
        // LIB-176: UI interactions are engine-emitted ScriptEventBus events.
        // `owner` is the UIDocument component entity; it is also the event
        // sender/target, while callback lifetime remains Events.Subscribe's
        // explicit owner and unsubscribe contract.
        LibraryEventDesc{
            .name = "UI.Click",
            .id = kb::script::ComputeEventId("UI.Click"),
            .arguments = UIPointerArguments(),
        },
        LibraryEventDesc{
            .name = "UI.Pointer",
            .id = kb::script::ComputeEventId("UI.Pointer"),
            .arguments = UIPointerArguments(),
        },
        LibraryEventDesc{
            .name = "UI.Submit",
            .id = kb::script::ComputeEventId("UI.Submit"),
            .arguments = {
                ScriptFunctionPin{ "owner", ScriptValueType::Entity, true },
                ScriptFunctionPin{ "element", ScriptValueType::Hash, true },
                ScriptFunctionPin{ "text", ScriptValueType::String, true },
            },
        },
        LibraryEventDesc{
            .name = "UI.Changed",
            .id = kb::script::ComputeEventId("UI.Changed"),
            .arguments = {
                ScriptFunctionPin{ "owner", ScriptValueType::Entity, true },
                ScriptFunctionPin{ "element", ScriptValueType::Hash, true },
                ScriptFunctionPin{ "value", ScriptValueType::Float, true },
            },
        },
        LibraryEventDesc{
            .name = "UI.Focus",
            .id = kb::script::ComputeEventId("UI.Focus"),
            .arguments = {
                ScriptFunctionPin{ "owner", ScriptValueType::Entity, true },
                ScriptFunctionPin{ "element", ScriptValueType::Hash, true },
                ScriptFunctionPin{ "focused", ScriptValueType::Bool, true },
            },
        },
        LibraryEventDesc{
            .name = "UI.Navigation",
            .id = kb::script::ComputeEventId("UI.Navigation"),
            .arguments = {
                ScriptFunctionPin{ "owner", ScriptValueType::Entity, true },
                ScriptFunctionPin{ "element", ScriptValueType::Hash, true },
                ScriptFunctionPin{ "direction", ScriptValueType::String, true },
            },
        },
    };
    return kCatalog;
}

const LibraryEventDesc* EngineLibraryEventRegistry::Find(std::string_view name) noexcept {
    const std::vector<LibraryEventDesc>& catalog = Catalog();
    const auto iterator = std::ranges::find_if(catalog, [name](const LibraryEventDesc& desc) { return desc.name == name; });
    return iterator == catalog.end() ? nullptr : &*iterator;
}

} // namespace kb::library
