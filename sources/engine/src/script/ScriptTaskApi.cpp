#include "engine/script/ScriptTaskApi.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/library/EngineLibraryTaskFactories.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneTasks.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kb::script {
namespace {

const ScriptValue* FindArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name) {
    for (const ScriptFunctionArgument& argument : arguments) {
        if (argument.name == name) {
            return &argument.value;
        }
    }
    return nullptr;
}

[[nodiscard]] std::uint64_t HashArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name, std::uint64_t fallback = 0U) noexcept {
    const ScriptValue* value = FindArg(arguments, name);
    return value == nullptr ? fallback : value->AsUInt64(fallback);
}

[[nodiscard]] float FloatArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name, float fallback = 0.0F) noexcept {
    const ScriptValue* value = FindArg(arguments, name);
    return value == nullptr ? fallback : value->AsFloat(fallback);
}

[[nodiscard]] int IntArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name, int fallback = 0) noexcept {
    const ScriptValue* value = FindArg(arguments, name);
    return value == nullptr ? fallback : value->AsInt(fallback);
}

[[nodiscard]] std::string StringArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name) {
    const ScriptValue* value = FindArg(arguments, name);
    return value == nullptr ? std::string{} : value->AsString();
}

[[nodiscard]] kb::scene::SceneEntity OwnerArg(std::span<const ScriptFunctionArgument> arguments) noexcept {
    const ScriptValue* value = FindArg(arguments, "owner");
    return value == nullptr ? kb::scene::SceneEntity{} : kb::scene::SceneEntity{ value->AsUInt64(0U) };
}

[[nodiscard]] kb::assets::AssetId ResolveAssetReference(kb::scene::Scene& scene, std::string_view reference) {
    kb::assets::AssetId id{};
    if (kb::assets::TryParseAssetId(reference, id) && id.IsValid()) {
        return scene.Assets().Manager().Registry().Find(id) != nullptr ? id : kb::assets::AssetId{};
    }
    const kb::assets::AssetMetadata* metadata =
        scene.Assets().Manager().Registry().FindByPath(std::filesystem::path{ reference });
    return metadata == nullptr ? kb::assets::AssetId{} : metadata->id;
}

ScriptFunctionCallResult Error(std::string message) {
    return ScriptFunctionCallResult{ .executed = false, .outputs = {}, .errors = { std::move(message) } };
}

ScriptFunctionCallResult NoScene() {
    return Error("task api requires an active scene");
}

ScriptFunctionCallResult StartedTask(std::uint64_t taskId, std::string_view operation) {
    if (taskId == 0U) {
        return Error(std::string{ operation } + " failed — scene is already holding its maximum number of live tasks");
    }
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "task", ScriptValue{ taskId, ScriptValueType::Hash } } },
        .errors = {},
    };
}

ScriptFunctionCallResult WaitSeconds(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const float seconds = FloatArg(arguments, "seconds");
    if (seconds <= 0.0F) {
        return Error("Task.WaitSeconds requires seconds > 0");
    }
    return StartedTask(
        context.scene->Tasks().Start(kb::library::MakeWaitSecondsTask(seconds), OwnerArg(arguments), context.caller),
        "Task.WaitSeconds");
}

ScriptFunctionCallResult WaitFixedSteps(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const int steps = IntArg(arguments, "steps");
    if (steps <= 0) {
        return Error("Task.WaitFixedSteps requires steps > 0");
    }
    return StartedTask(
        context.scene->Tasks().StartFixedStep(
            kb::library::MakeWaitFixedStepsTask(static_cast<std::size_t>(steps)),
            OwnerArg(arguments),
            context.caller),
        "Task.WaitFixedSteps");
}

ScriptFunctionCallResult WaitEvent(
    ScriptRuntimeHost& host,
    const ScriptFunctionCallContext& context,
    std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const std::string eventName = StringArg(arguments, "event");
    if (eventName.empty()) {
        return Error("Task.WaitEvent requires a non-empty event name");
    }
    const kb::scene::SceneEntity owner = OwnerArg(arguments);
    std::shared_ptr<const ScriptEventObservation> observation = host.Runtime().Events().Observe(eventName, owner);
    if (!observation) {
        return Error("Task.WaitEvent could not allocate an event observation");
    }
    return StartedTask(
        context.scene->Tasks().Start(kb::library::MakeWaitEventTask(std::move(observation)), owner, context.caller),
        "Task.WaitEvent");
}

ScriptFunctionCallResult WaitAsset(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const kb::assets::AssetId assetId = ResolveAssetReference(*context.scene, StringArg(arguments, "reference"));
    if (!assetId.IsValid()) {
        return Error("Task.WaitAsset requires a registered asset reference");
    }
    return StartedTask(
        context.scene->Tasks().Start(
            kb::library::MakeWaitAssetLoadTask(context.scene->Assets().Manager(), assetId),
            OwnerArg(arguments),
            context.caller),
        "Task.WaitAsset");
}

ScriptFunctionCallResult WaitScene(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    std::string sceneName = StringArg(arguments, "scene");
    if (sceneName.empty()) {
        return Error("Task.WaitScene requires a non-empty scene name");
    }
    return StartedTask(
        context.scene->Tasks().Start(
            kb::library::MakeWaitSceneLoadTask(*context.scene, std::move(sceneName)),
            OwnerArg(arguments),
            context.caller),
        "Task.WaitScene");
}

// LIB-097: no Task.Start exists here on purpose. Lua and Visual Graph own
// their respective coroutine state, while a SceneTasks body is a native C++
// poll callback; only native C++ can call kb::scene::SceneTasks::Start.
// IsRunning/Cancel are the real, non-fabricated surface a script CAN use:
// observing/controlling a task a native plugin already started and handed
// the id of (e.g. via a TaskCompleted/TaskFailed event argument, or a
// native function's own output pin).
ScriptFunctionCallResult IsRunning(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const bool running = context.scene->Tasks().Exists(HashArg(arguments, "task"));
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "running", ScriptValue{ running } } },
        .errors = {},
    };
}

ScriptFunctionCallResult Cancel(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const bool cancelled = context.scene->Tasks().Cancel(HashArg(arguments, "task"));
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "cancelled", ScriptValue{ cancelled } } },
        .errors = {},
    };
}

// LIB-101: creation-site diagnostics — returns whatever creator entity the
// NATIVE caller passed to SceneTasks::Start/StartFixedStep (invalid if
// none was supplied, or the handle is unknown/gone) — a Task's creation
// site is never a script call (see the class doc comment above), so unlike
// Timer.Creator this can never resolve to context.caller automatically.
ScriptFunctionCallResult Creator(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const kb::scene::SceneEntity creator = context.scene->Tasks().Creator(HashArg(arguments, "task"));
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "creator", ScriptValue{ creator.Id(), ScriptValueType::Entity } } },
        .errors = {},
    };
}

bool RegisterFunction(ScriptRuntimeHost& host, std::string name, std::vector<ScriptFunctionPin> inputs, std::vector<ScriptFunctionPin> outputs, ScriptFunctionCallback callback) {
    ScriptFunctionDesc desc;
    desc.signature.name = std::move(name);
    desc.signature.inputs = std::move(inputs);
    desc.signature.outputs = std::move(outputs);
    desc.callback = std::move(callback);
    return host.RegisterFunction(std::move(desc));
}

} // namespace

bool ScriptTaskApi::Register(ScriptRuntimeHost& host) {
    bool ok = true;
    ok = RegisterFunction(host, "Task.WaitSeconds",
              { ScriptFunctionPin{ "seconds", ScriptValueType::Float, true }, ScriptFunctionPin{ "owner", ScriptValueType::Entity, false } },
              { ScriptFunctionPin{ "task", ScriptValueType::Hash, true } }, &WaitSeconds)
        && ok;
    ok = RegisterFunction(host, "Task.WaitFixedSteps",
              { ScriptFunctionPin{ "steps", ScriptValueType::Int, true }, ScriptFunctionPin{ "owner", ScriptValueType::Entity, false } },
              { ScriptFunctionPin{ "task", ScriptValueType::Hash, true } }, &WaitFixedSteps)
        && ok;
    ok = RegisterFunction(host, "Task.WaitEvent",
              { ScriptFunctionPin{ "event", ScriptValueType::String, true }, ScriptFunctionPin{ "owner", ScriptValueType::Entity, false } },
              { ScriptFunctionPin{ "task", ScriptValueType::Hash, true } },
              [&host](const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
                  return WaitEvent(host, context, arguments);
              })
        && ok;
    ok = RegisterFunction(host, "Task.WaitAsset",
              { ScriptFunctionPin{ "reference", ScriptValueType::String, true }, ScriptFunctionPin{ "owner", ScriptValueType::Entity, false } },
              { ScriptFunctionPin{ "task", ScriptValueType::Hash, true } }, &WaitAsset)
        && ok;
    ok = RegisterFunction(host, "Task.WaitScene",
              { ScriptFunctionPin{ "scene", ScriptValueType::String, true }, ScriptFunctionPin{ "owner", ScriptValueType::Entity, false } },
              { ScriptFunctionPin{ "task", ScriptValueType::Hash, true } }, &WaitScene)
        && ok;
    ok = RegisterFunction(host, "Task.IsRunning",
              { ScriptFunctionPin{ "task", ScriptValueType::Hash, true } },
              { ScriptFunctionPin{ "running", ScriptValueType::Bool, true } }, &IsRunning)
        && ok;
    ok = RegisterFunction(host, "Task.Cancel",
              { ScriptFunctionPin{ "task", ScriptValueType::Hash, true } },
              { ScriptFunctionPin{ "cancelled", ScriptValueType::Bool, true } }, &Cancel)
        && ok;
    ok = RegisterFunction(host, "Task.Creator",
              { ScriptFunctionPin{ "task", ScriptValueType::Hash, true } },
              { ScriptFunctionPin{ "creator", ScriptValueType::Entity, true } }, &Creator)
        && ok;
    return ok;
}

} // namespace kb::script
