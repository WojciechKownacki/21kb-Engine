#include "engine/script/ScriptAnimatorApi.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAnimators.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <span>
#include <string>
#include <utility>

namespace kb::script {
namespace {

const ScriptValue* Arg(std::span<const ScriptFunctionArgument> arguments, std::string_view name) {
    for (const auto& argument : arguments) if (argument.name == name) return &argument.value;
    return nullptr;
}

ScriptFunctionCallResult Error(std::string message) {
    return ScriptFunctionCallResult{ .executed = false, .outputs = {}, .errors = { std::move(message) } };
}

kb::scene::SceneEntity Target(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    const ScriptValue* value = Arg(arguments, "entity");
    return value == nullptr ? context.caller : kb::scene::SceneEntity{ value->AsUInt64() };
}

ScriptFunctionCallResult Applied(bool value, std::string failure) {
    return value
        ? ScriptFunctionCallResult{ .executed = true, .outputs = { { "applied", ScriptValue{ true } } }, .errors = {} }
        : Error(std::move(failure));
}

ScriptFunctionCallResult Play(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) return Error("Animator.Play requires an active scene");
    const float normalized = Arg(arguments, "normalizedTime") == nullptr ? 0.0F : Arg(arguments, "normalizedTime")->AsFloat();
    return Applied(context.scene->Animators().Play(
        Target(context, arguments), Arg(arguments, "layer")->AsString(), Arg(arguments, "state")->AsString(), normalized),
        "Animator.Play received an invalid animator, layer, state, or normalized time");
}

ScriptFunctionCallResult CrossFade(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) return Error("Animator.CrossFade requires an active scene");
    const float normalized = Arg(arguments, "normalizedTime") == nullptr ? 0.0F : Arg(arguments, "normalizedTime")->AsFloat();
    return Applied(context.scene->Animators().CrossFade(
        Target(context, arguments), Arg(arguments, "layer")->AsString(), Arg(arguments, "state")->AsString(),
        Arg(arguments, "duration")->AsFloat(), normalized),
        "Animator.CrossFade received an invalid animator/state/time or an already active transition");
}

ScriptFunctionCallResult SetSpeed(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) return Error("Animator.SetSpeed requires an active scene");
    return Applied(context.scene->Animators().SetSpeed(Target(context, arguments), Arg(arguments, "speed")->AsFloat()),
        "Animator.SetSpeed requires an attached animator and a finite non-negative speed");
}

template <kb::scene::AnimatorParameterType Type>
ScriptFunctionCallResult SetParameter(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) return Error("Animator parameter API requires an active scene");
    const kb::scene::SceneEntity entity = Target(context, arguments);
    const std::string name = Arg(arguments, "name")->AsString();
    bool applied = false;
    if constexpr (Type == kb::scene::AnimatorParameterType::Bool) applied = context.scene->Animators().SetBool(entity, name, Arg(arguments, "value")->AsBool());
    if constexpr (Type == kb::scene::AnimatorParameterType::Int) applied = context.scene->Animators().SetInt(entity, name, Arg(arguments, "value")->AsInt());
    if constexpr (Type == kb::scene::AnimatorParameterType::Float) applied = context.scene->Animators().SetFloat(entity, name, Arg(arguments, "value")->AsFloat());
    if constexpr (Type == kb::scene::AnimatorParameterType::Trigger) applied = context.scene->Animators().SetTrigger(entity, name);
    return Applied(applied, "Animator parameter name/type did not match the attached controller");
}

ScriptFunctionCallResult ResetTrigger(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) return Error("Animator.ResetTrigger requires an active scene");
    return Applied(context.scene->Animators().ResetTrigger(Target(context, arguments), Arg(arguments, "name")->AsString()),
        "Animator trigger name did not match the attached controller");
}

ScriptFunctionCallResult State(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) return Error("Animator.State requires an active scene");
    const auto state = context.scene->Animators().State(Target(context, arguments), Arg(arguments, "layer")->AsString());
    if (!state) return Error("Animator.State could not resolve the animator layer");
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            { "state", ScriptValue{ std::string{ state->state } } },
            { "previousState", ScriptValue{ std::string{ state->previousState } } },
            { "normalizedTime", ScriptValue{ state->normalizedTime } },
            { "transitionProgress", ScriptValue{ state->transitionProgress } },
            { "transitioning", ScriptValue{ state->transitioning } },
        },
        .errors = {},
    };
}

ScriptFunctionCallResult Speed(const ScriptFunctionCallContext& context, std::span<const ScriptFunctionArgument> arguments) {
    if (context.scene == nullptr) return Error("Animator.Speed requires an active scene");
    const kb::scene::SceneEntity entity = Target(context, arguments);
    if (!context.scene->Animators().Exists(entity)) return Error("Animator.Speed target has no attached animator");
    return ScriptFunctionCallResult{ .executed = true, .outputs = { { "speed", ScriptValue{ context.scene->Animators().Speed(entity) } } }, .errors = {} };
}

bool RegisterFunction(
    ScriptRuntimeHost& host, std::string name, std::vector<ScriptFunctionPin> inputs,
    std::vector<ScriptFunctionPin> outputs, ScriptFunctionCallback callback) {
    ScriptFunctionDesc function{};
    function.signature.name = std::move(name);
    function.signature.inputs = std::move(inputs);
    function.signature.outputs = std::move(outputs);
    function.callback = std::move(callback);
    return host.RegisterFunction(std::move(function));
}

std::vector<ScriptFunctionPin> Targeted(std::vector<ScriptFunctionPin> inputs) {
    inputs.push_back({ "entity", ScriptValueType::Entity, false });
    return inputs;
}

} // namespace

bool ScriptAnimatorApi::Register(ScriptRuntimeHost& host) {
    const auto applied = std::vector<ScriptFunctionPin>{ { "applied", ScriptValueType::Bool, true } };
    return RegisterFunction(host, "Animator.Play", Targeted({ { "layer", ScriptValueType::String, true }, { "state", ScriptValueType::String, true }, { "normalizedTime", ScriptValueType::Float, false } }), applied, &Play) &&
        RegisterFunction(host, "Animator.CrossFade", Targeted({ { "layer", ScriptValueType::String, true }, { "state", ScriptValueType::String, true }, { "duration", ScriptValueType::Float, true }, { "normalizedTime", ScriptValueType::Float, false } }), applied, &CrossFade) &&
        RegisterFunction(host, "Animator.SetSpeed", Targeted({ { "speed", ScriptValueType::Float, true } }), applied, &SetSpeed) &&
        RegisterFunction(host, "Animator.Speed", Targeted({}), { { "speed", ScriptValueType::Float, true } }, &Speed) &&
        RegisterFunction(host, "Animator.SetBool", Targeted({ { "name", ScriptValueType::String, true }, { "value", ScriptValueType::Bool, true } }), applied, &SetParameter<kb::scene::AnimatorParameterType::Bool>) &&
        RegisterFunction(host, "Animator.SetInt", Targeted({ { "name", ScriptValueType::String, true }, { "value", ScriptValueType::Int, true } }), applied, &SetParameter<kb::scene::AnimatorParameterType::Int>) &&
        RegisterFunction(host, "Animator.SetFloat", Targeted({ { "name", ScriptValueType::String, true }, { "value", ScriptValueType::Float, true } }), applied, &SetParameter<kb::scene::AnimatorParameterType::Float>) &&
        RegisterFunction(host, "Animator.SetTrigger", Targeted({ { "name", ScriptValueType::String, true } }), applied, &SetParameter<kb::scene::AnimatorParameterType::Trigger>) &&
        RegisterFunction(host, "Animator.ResetTrigger", Targeted({ { "name", ScriptValueType::String, true } }), applied, &ResetTrigger) &&
        RegisterFunction(host, "Animator.State", Targeted({ { "layer", ScriptValueType::String, true } }), {
            { "state", ScriptValueType::String, true },
            { "previousState", ScriptValueType::String, true },
            { "normalizedTime", ScriptValueType::Float, true },
            { "transitionProgress", ScriptValueType::Float, true },
            { "transitioning", ScriptValueType::Bool, true },
        }, &State);
}

} // namespace kb::script
