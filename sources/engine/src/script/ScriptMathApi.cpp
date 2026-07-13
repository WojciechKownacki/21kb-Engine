#include "engine/script/ScriptMathApi.hpp"

#include "engine/math/EngineMath.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <limits>
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

[[nodiscard]] ScriptFunctionCallResult FloatResult(std::string_view pin, float value) {
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ std::string{ pin }, ScriptValue{ value } } },
        .errors = {},
    };
}

ScriptFunctionCallResult Clamp(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return FloatResult("result", kb::math::Clamp(FloatArg(arguments, "value"), FloatArg(arguments, "min"), FloatArg(arguments, "max")));
}

ScriptFunctionCallResult Lerp(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return FloatResult("result", kb::math::Lerp(FloatArg(arguments, "a"), FloatArg(arguments, "b"), FloatArg(arguments, "t")));
}

ScriptFunctionCallResult InverseLerp(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return FloatResult("t", kb::math::InverseLerp(FloatArg(arguments, "a"), FloatArg(arguments, "b"), FloatArg(arguments, "value")));
}

ScriptFunctionCallResult Remap(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return FloatResult(
        "result",
        kb::math::Remap(
            FloatArg(arguments, "value"),
            FloatArg(arguments, "inMin"),
            FloatArg(arguments, "inMax"),
            FloatArg(arguments, "outMin"),
            FloatArg(arguments, "outMax")));
}

ScriptFunctionCallResult SmoothStep(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return FloatResult("result", kb::math::SmoothStep(FloatArg(arguments, "edge0"), FloatArg(arguments, "edge1"), FloatArg(arguments, "x")));
}

ScriptFunctionCallResult MoveTowards(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return FloatResult(
        "result",
        kb::math::MoveTowards(FloatArg(arguments, "current"), FloatArg(arguments, "target"), FloatArg(arguments, "maxDelta")));
}

ScriptFunctionCallResult Damp(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    const kb::math::DampResult result = kb::math::Damp(
        FloatArg(arguments, "current"),
        FloatArg(arguments, "target"),
        FloatArg(arguments, "velocity"),
        FloatArg(arguments, "smoothTime"),
        FloatArg(arguments, "deltaTime"),
        FloatArg(arguments, "maxSpeed", std::numeric_limits<float>::max()));
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "value", ScriptValue{ result.value } },
            ScriptFunctionArgument{ "velocity", ScriptValue{ result.velocity } },
        },
        .errors = {},
    };
}

bool RegisterFunction(
    ScriptRuntimeHost& host,
    std::string name,
    std::vector<ScriptFunctionPin> inputs,
    std::vector<ScriptFunctionPin> outputs,
    ScriptFunctionCallback callback) {
    ScriptFunctionDesc desc;
    desc.signature.name = std::move(name);
    desc.signature.inputs = std::move(inputs);
    desc.signature.outputs = std::move(outputs);
    desc.callback = std::move(callback);
    return host.RegisterFunction(std::move(desc));
}

} // namespace

bool ScriptMathApi::Register(ScriptRuntimeHost& host) {
    bool ok = true;
    ok = RegisterFunction(host, "Math.Clamp",
        {
            ScriptFunctionPin{ "value", ScriptValueType::Float, true },
            ScriptFunctionPin{ "min", ScriptValueType::Float, true },
            ScriptFunctionPin{ "max", ScriptValueType::Float, true },
        },
        { ScriptFunctionPin{ "result", ScriptValueType::Float, true } },
        &Clamp) && ok;
    ok = RegisterFunction(host, "Math.Lerp",
        {
            ScriptFunctionPin{ "a", ScriptValueType::Float, true },
            ScriptFunctionPin{ "b", ScriptValueType::Float, true },
            ScriptFunctionPin{ "t", ScriptValueType::Float, true },
        },
        { ScriptFunctionPin{ "result", ScriptValueType::Float, true } },
        &Lerp) && ok;
    ok = RegisterFunction(host, "Math.InverseLerp",
        {
            ScriptFunctionPin{ "a", ScriptValueType::Float, true },
            ScriptFunctionPin{ "b", ScriptValueType::Float, true },
            ScriptFunctionPin{ "value", ScriptValueType::Float, true },
        },
        { ScriptFunctionPin{ "t", ScriptValueType::Float, true } },
        &InverseLerp) && ok;
    ok = RegisterFunction(host, "Math.Remap",
        {
            ScriptFunctionPin{ "value", ScriptValueType::Float, true },
            ScriptFunctionPin{ "inMin", ScriptValueType::Float, true },
            ScriptFunctionPin{ "inMax", ScriptValueType::Float, true },
            ScriptFunctionPin{ "outMin", ScriptValueType::Float, true },
            ScriptFunctionPin{ "outMax", ScriptValueType::Float, true },
        },
        { ScriptFunctionPin{ "result", ScriptValueType::Float, true } },
        &Remap) && ok;
    ok = RegisterFunction(host, "Math.SmoothStep",
        {
            ScriptFunctionPin{ "edge0", ScriptValueType::Float, true },
            ScriptFunctionPin{ "edge1", ScriptValueType::Float, true },
            ScriptFunctionPin{ "x", ScriptValueType::Float, true },
        },
        { ScriptFunctionPin{ "result", ScriptValueType::Float, true } },
        &SmoothStep) && ok;
    ok = RegisterFunction(host, "Math.MoveTowards",
        {
            ScriptFunctionPin{ "current", ScriptValueType::Float, true },
            ScriptFunctionPin{ "target", ScriptValueType::Float, true },
            ScriptFunctionPin{ "maxDelta", ScriptValueType::Float, true },
        },
        { ScriptFunctionPin{ "result", ScriptValueType::Float, true } },
        &MoveTowards) && ok;
    ok = RegisterFunction(host, "Math.Damp",
        {
            ScriptFunctionPin{ "current", ScriptValueType::Float, true },
            ScriptFunctionPin{ "target", ScriptValueType::Float, true },
            ScriptFunctionPin{ "velocity", ScriptValueType::Float, true },
            ScriptFunctionPin{ "smoothTime", ScriptValueType::Float, true },
            ScriptFunctionPin{ "deltaTime", ScriptValueType::Float, true },
            ScriptFunctionPin{ "maxSpeed", ScriptValueType::Float, false },
        },
        {
            ScriptFunctionPin{ "value", ScriptValueType::Float, true },
            ScriptFunctionPin{ "velocity", ScriptValueType::Float, true },
        },
        &Damp) && ok;
    return ok;
}

} // namespace kb::script
