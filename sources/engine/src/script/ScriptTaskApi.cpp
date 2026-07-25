#include "engine/script/ScriptTaskApi.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneTasks.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <cstdint>
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

ScriptFunctionCallResult Error(std::string message) {
    return ScriptFunctionCallResult{ .executed = false, .outputs = {}, .errors = { std::move(message) } };
}

ScriptFunctionCallResult NoScene() {
    return Error("task api requires an active scene");
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
