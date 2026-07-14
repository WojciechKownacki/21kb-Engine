#include "engine/library/EngineLibraryEventSchema.hpp"

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
    };
    return kCatalog;
}

const LibraryEventDesc* EngineLibraryEventRegistry::Find(std::string_view name) noexcept {
    const std::vector<LibraryEventDesc>& catalog = Catalog();
    const auto iterator = std::ranges::find_if(catalog, [name](const LibraryEventDesc& desc) { return desc.name == name; });
    return iterator == catalog.end() ? nullptr : &*iterator;
}

} // namespace kb::library
