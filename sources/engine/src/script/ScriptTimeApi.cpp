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

// LIB-094: Delta is the SCALED, pause-aware value — scale = 0 whenever the
// scene is paused (Runtime().IsPlaying()==false), regardless of the
// configured Time.Scale, and otherwise scale = Runtime().TimeScale().
// Deliberately scoped to ONLY this script-visible value: the raw
// deltaSeconds threaded through SceneRuntimeService::Update/
// ScriptRuntimeSceneSystem is never touched, so physics/ECS/elapsedSeconds/
// frameIndex all keep advancing at real wall-clock rate even while
// gameplay code driven by Time.Delta is frozen or slowed. A missing scene
// (context.scene == nullptr) is treated as scale=1 (unscaled), preserving
// the pre-LIB-094 "Time.Delta works without a scene" behavior rather than
// making scale-awareness a new hard requirement.
ScriptFunctionCallResult Delta(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument>) {
    const float scale = context.scene == nullptr ? 1.0F : (context.scene->Runtime().IsPlaying() ? context.scene->Runtime().TimeScale() : 0.0F);
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "delta", ScriptValue{ context.deltaSeconds * scale } } },
        .errors = {},
    };
}

// LIB-094: UnscaledDelta is the RAW wall-clock deltaSeconds — deliberately
// NEVER multiplied by Time.Scale and NEVER zeroed by pause, so a real-time
// UI countdown or a pause-menu animation can keep advancing at true
// wall-clock rate even while Time.Delta reads 0. Before LIB-094 this was a
// literal duplicate of Delta (no scale/pause concept existed yet, LIB-093);
// now that Time.Scale/pause exist, the two genuinely diverge.
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
    // LIB-093: report the delta a script's FixedTick ACTUALLY runs at — the
    // ScriptRuntimeSceneSystem's own frame-settings step, which it stamps onto
    // the scene each frame (SceneRuntime::ScriptFixedDeltaSeconds) — NOT the
    // physics SceneRuntimeFixedStepSettings step, which is configured
    // independently and can differ (e.g. 0.01 script vs 0.02 physics).
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "delta", ScriptValue{ context.scene->Runtime().ScriptFixedDeltaSeconds() } } },
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

// LIB-094: Scale getter — always succeeds with a scene (no domain to
// validate on read), defaults to 1.0 for a missing scene, mirroring the
// same "unscaled" fallback Delta above uses.
ScriptFunctionCallResult Scale(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument>) {
    const float scale = context.scene == nullptr ? 1.0F : context.scene->Runtime().TimeScale();
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "scale", ScriptValue{ scale } } },
        .errors = {},
    };
}

// LIB-094: SetScale is the actual validation boundary (LIB-064's
// validate-at-the-boundary precedent, also used by Math.Asin/Acos's domain
// check, LIB-047) — a negative scale is UNCONDITIONALLY rejected with an
// honest error rather than silently clamped to 0, since a negative time
// scale is not a meaningful "slow motion," it's invalid input.
ScriptFunctionCallResult SetScale(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) {
        return NoScene();
    }
    const ScriptValue* scaleValue = nullptr;
    for (const ScriptFunctionArgument& argument : arguments) {
        if (argument.name == "scale") {
            scaleValue = &argument.value;
            break;
        }
    }
    const float scale = scaleValue == nullptr ? 1.0F : scaleValue->AsFloat(1.0F);
    if (scale < 0.0F) {
        return Error("Time.SetScale rejects a negative scale");
    }
    context.scene->Runtime().SetTimeScale(scale);
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "set", ScriptValue{ true } } },
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

bool RegisterFunction(ScriptRuntimeHost& host, std::string name, std::vector<ScriptFunctionPin> outputs, ScriptFunctionCallback callback) {
    return RegisterFunction(host, std::move(name), {}, std::move(outputs), std::move(callback));
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
    ok = RegisterFunction(host, "Time.Scale", { ScriptFunctionPin{ "scale", ScriptValueType::Float, true } }, &Scale) && ok;
    ok = RegisterFunction(host, "Time.SetScale", { ScriptFunctionPin{ "scale", ScriptValueType::Float, false } }, { ScriptFunctionPin{ "set", ScriptValueType::Bool, true } }, &SetScale) && ok;
    return ok;
}

} // namespace kb::script
