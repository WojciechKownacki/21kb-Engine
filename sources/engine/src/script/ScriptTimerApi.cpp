#include "engine/script/ScriptTimerApi.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneTimers.hpp"
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

[[nodiscard]] float FloatArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name, float fallback = 0.0F) noexcept {
    const ScriptValue* value = FindArg(arguments, name);
    return value == nullptr ? fallback : value->AsFloat(fallback);
}

[[nodiscard]] std::uint64_t HashArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name, std::uint64_t fallback = 0U) noexcept {
    const ScriptValue* value = FindArg(arguments, name);
    return value == nullptr ? fallback : value->AsUInt64(fallback);
}

// LIB-095: "owner" is optional (omitted/invalid = no owner, TimerFired
// broadcasts to every enabled behaviour instead of a targeted dispatch —
// see ScriptRuntimeSceneSystem::DispatchFiredTimers) — same optional-entity
// idiom World.SetParent's "parent" pin already uses.
[[nodiscard]] kb::scene::SceneEntity OwnerArg(std::span<const ScriptFunctionArgument> arguments) noexcept {
    const ScriptValue* value = FindArg(arguments, "owner");
    return value == nullptr ? kb::scene::SceneEntity{} : kb::scene::SceneEntity{ value->AsUInt64(0U) };
}

ScriptFunctionCallResult Error(std::string message) {
    return ScriptFunctionCallResult{ .executed = false, .outputs = {}, .errors = { std::move(message) } };
}

ScriptFunctionCallResult NoScene() {
    return Error("timer api requires an active scene");
}

ScriptFunctionCallResult Once(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const float delaySeconds = FloatArg(arguments, "delay");
    if (delaySeconds <= 0.0F) {
        return Error("Timer.Once requires delay > 0");
    }
    const std::uint64_t id = context.scene->Timers().Once(delaySeconds, OwnerArg(arguments), context.caller);
    if (id == 0U) {
        return Error("Timer.Once failed — scene is already holding its maximum number of live timers");
    }
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "timer", ScriptValue{ id, ScriptValueType::Hash } } },
        .errors = {},
    };
}

ScriptFunctionCallResult Repeat(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const float intervalSeconds = FloatArg(arguments, "interval");
    if (intervalSeconds <= 0.0F) {
        return Error("Timer.Repeat requires interval > 0");
    }
    const std::uint64_t id = context.scene->Timers().Repeat(intervalSeconds, OwnerArg(arguments), context.caller);
    if (id == 0U) {
        return Error("Timer.Repeat failed — scene is already holding its maximum number of live timers");
    }
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "timer", ScriptValue{ id, ScriptValueType::Hash } } },
        .errors = {},
    };
}

ScriptFunctionCallResult Cancel(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const bool cancelled = context.scene->Timers().Cancel(HashArg(arguments, "timer"));
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "cancelled", ScriptValue{ cancelled } } },
        .errors = {},
    };
}

ScriptFunctionCallResult Pause(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const bool set = context.scene->Timers().Pause(HashArg(arguments, "timer"));
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "set", ScriptValue{ set } } },
        .errors = {},
    };
}

ScriptFunctionCallResult Resume(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const bool set = context.scene->Timers().Resume(HashArg(arguments, "timer"));
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "set", ScriptValue{ set } } },
        .errors = {},
    };
}

// LIB-101: creation-site diagnostics — returns the entity that CALLED
// Timer.Once/Repeat (invalid if unknown, e.g. a native-only
// SceneTimers::Once/Repeat call that didn't supply one, or an unknown/gone
// handle) — lets a hung/leaked timer be traced back to whatever created it.
ScriptFunctionCallResult Creator(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const kb::scene::SceneEntity creator = context.scene->Timers().Creator(HashArg(arguments, "timer"));
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

bool ScriptTimerApi::Register(ScriptRuntimeHost& host) {
    bool ok = true;
    ok = RegisterFunction(host, "Timer.Once",
              { ScriptFunctionPin{ "delay", ScriptValueType::Float, true }, ScriptFunctionPin{ "owner", ScriptValueType::Entity, false } },
              { ScriptFunctionPin{ "timer", ScriptValueType::Hash, true } }, &Once)
        && ok;
    ok = RegisterFunction(host, "Timer.Repeat",
              { ScriptFunctionPin{ "interval", ScriptValueType::Float, true }, ScriptFunctionPin{ "owner", ScriptValueType::Entity, false } },
              { ScriptFunctionPin{ "timer", ScriptValueType::Hash, true } }, &Repeat)
        && ok;
    ok = RegisterFunction(host, "Timer.Cancel",
              { ScriptFunctionPin{ "timer", ScriptValueType::Hash, true } },
              { ScriptFunctionPin{ "cancelled", ScriptValueType::Bool, true } }, &Cancel)
        && ok;
    ok = RegisterFunction(host, "Timer.Pause",
              { ScriptFunctionPin{ "timer", ScriptValueType::Hash, true } },
              { ScriptFunctionPin{ "set", ScriptValueType::Bool, true } }, &Pause)
        && ok;
    ok = RegisterFunction(host, "Timer.Resume",
              { ScriptFunctionPin{ "timer", ScriptValueType::Hash, true } },
              { ScriptFunctionPin{ "set", ScriptValueType::Bool, true } }, &Resume)
        && ok;
    ok = RegisterFunction(host, "Timer.Creator",
              { ScriptFunctionPin{ "timer", ScriptValueType::Hash, true } },
              { ScriptFunctionPin{ "creator", ScriptValueType::Entity, true } }, &Creator)
        && ok;
    return ok;
}

} // namespace kb::script
