#include "engine/script/ScriptTimeApi.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <utility>

namespace kb::script {
namespace {

ScriptFunctionCallResult Error(std::string message) {
    return ScriptFunctionCallResult{ .executed = false, .outputs = {}, .errors = { std::move(message) } };
}

ScriptFunctionCallResult NoScene() {
    return Error("time api requires an active scene");
}

ScriptFunctionCallResult Delta(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument>) {
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "delta", ScriptValue{ context.deltaSeconds } } },
        .errors = {},
    };
}

// LIB-093: no time-scale/pause-multiplier concept exists ANYWHERE in this
// engine (confirmed by research before writing this — grepped timeScale/
// TimeScale/UnscaledDelta/unscaled across kb::scene/kb::script, zero
// hits). UnscaledDelta is therefore HONESTLY identical to Delta today —
// the same "real API surface for a future capability, not a fabricated
// distinct value" precedent LIB-071's Scene.LoadProgress already
// established (always 1.0/0.0 because loading is synchronous today) —
// once a real time-scale multiplier exists, ONLY this function's body
// needs to change; the two names are already distinct at the call site.
ScriptFunctionCallResult UnscaledDelta(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument>) {
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "delta", ScriptValue{ context.deltaSeconds } } },
        .errors = {},
    };
}

ScriptFunctionCallResult FixedDelta(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument>) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "delta", ScriptValue{ context.scene->Runtime().FixedStepSettings().fixedDeltaSeconds } } },
        .errors = {},
    };
}

ScriptFunctionCallResult Elapsed(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument>) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "elapsed", ScriptValue{ static_cast<float>(context.scene->Runtime().ElapsedSeconds()) } } },
        .errors = {},
    };
}

// LIB-093: resolves the LIB-065 POWRÓT question ("does Time.FrameIndex
// collide with World.FrameIndex, or are they the same counter?") — this
// engine has no separate global/engine-level frame counter distinct from a
// scene's own (every ScriptFunctionCallContext is already tied to exactly
// one kb::scene::Scene); Time.FrameIndex is a thin alias of the EXACT SAME
// SceneRuntime accessor World.FrameIndex (LIB-065, ScriptWorldApi.cpp)
// already wraps — both names return the identical value for the same
// scene, on purpose, not a bug.
ScriptFunctionCallResult FrameIndex(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument>) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "frame", ScriptValue{ static_cast<std::int64_t>(context.scene->Runtime().FrameIndex()) } } },
        .errors = {},
    };
}

ScriptFunctionCallResult FixedStepIndex(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument>) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "step", ScriptValue{ static_cast<std::int64_t>(context.scene->Runtime().FixedStepIndex()) } } },
        .errors = {},
    };
}

bool RegisterFunction(ScriptRuntimeHost& host, std::string name, std::vector<ScriptFunctionPin> outputs, ScriptFunctionCallback callback) {
    ScriptFunctionDesc desc;
    desc.signature.name = std::move(name);
    desc.signature.inputs = {};
    desc.signature.outputs = std::move(outputs);
    desc.callback = std::move(callback);
    return host.RegisterFunction(std::move(desc));
}

} // namespace

bool ScriptTimeApi::Register(ScriptRuntimeHost& host) {
    bool ok = true;
    ok = RegisterFunction(host, "Time.Delta", { ScriptFunctionPin{ "delta", ScriptValueType::Float, true } }, &Delta) && ok;
    ok = RegisterFunction(host, "Time.UnscaledDelta", { ScriptFunctionPin{ "delta", ScriptValueType::Float, true } }, &UnscaledDelta) && ok;
    ok = RegisterFunction(host, "Time.FixedDelta", { ScriptFunctionPin{ "delta", ScriptValueType::Float, true } }, &FixedDelta) && ok;
    ok = RegisterFunction(host, "Time.Elapsed", { ScriptFunctionPin{ "elapsed", ScriptValueType::Float, true } }, &Elapsed) && ok;
    ok = RegisterFunction(host, "Time.FrameIndex", { ScriptFunctionPin{ "frame", ScriptValueType::Int64, true } }, &FrameIndex) && ok;
    ok = RegisterFunction(host, "Time.FixedStepIndex", { ScriptFunctionPin{ "step", ScriptValueType::Int64, true } }, &FixedStepIndex) && ok;
    return ok;
}

} // namespace kb::script
