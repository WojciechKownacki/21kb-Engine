#include "engine/script/ScriptMathApi.hpp"

#include "engine/math/EngineMath.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <iterator>
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

// LIB-048: Vec3 is not a ScriptValueType pin (LIB-032/LIB-042) — every
// Vec3-shaped Math.* function decomposes into three named-prefix Float
// pins, the same convention Physics.Raycast/Transform.* already use.
[[nodiscard]] kb::math::Vec3 Vec3Arg(std::span<const ScriptFunctionArgument> arguments, std::string_view prefix) noexcept {
    return kb::math::Vec3{
        FloatArg(arguments, std::string{ prefix } + "X"),
        FloatArg(arguments, std::string{ prefix } + "Y"),
        FloatArg(arguments, std::string{ prefix } + "Z"),
    };
}

[[nodiscard]] ScriptFunctionCallResult Vec3Result(kb::math::Vec3 value) {
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "x", ScriptValue{ value.x } },
            ScriptFunctionArgument{ "y", ScriptValue{ value.y } },
            ScriptFunctionArgument{ "z", ScriptValue{ value.z } },
        },
        .errors = {},
    };
}

[[nodiscard]] std::vector<ScriptFunctionPin> Vec3Pins(std::string_view prefix) {
    return {
        ScriptFunctionPin{ std::string{ prefix } + "X", ScriptValueType::Float, true },
        ScriptFunctionPin{ std::string{ prefix } + "Y", ScriptValueType::Float, true },
        ScriptFunctionPin{ std::string{ prefix } + "Z", ScriptValueType::Float, true },
    };
}

[[nodiscard]] std::vector<ScriptFunctionPin> Vec3OutputPins() {
    return {
        ScriptFunctionPin{ "x", ScriptValueType::Float, true },
        ScriptFunctionPin{ "y", ScriptValueType::Float, true },
        ScriptFunctionPin{ "z", ScriptValueType::Float, true },
    };
}

[[nodiscard]] std::vector<ScriptFunctionPin> ConcatPins(std::vector<ScriptFunctionPin> lhs, std::vector<ScriptFunctionPin> rhs) {
    lhs.insert(lhs.end(), std::make_move_iterator(rhs.begin()), std::make_move_iterator(rhs.end()));
    return lhs;
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

ScriptFunctionCallResult Min(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return FloatResult("result", kb::math::Min(FloatArg(arguments, "a"), FloatArg(arguments, "b")));
}

ScriptFunctionCallResult Max(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return FloatResult("result", kb::math::Max(FloatArg(arguments, "a"), FloatArg(arguments, "b")));
}

ScriptFunctionCallResult Abs(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return FloatResult("result", kb::math::Abs(FloatArg(arguments, "value")));
}

ScriptFunctionCallResult Sign(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return FloatResult("result", kb::math::Sign(FloatArg(arguments, "value")));
}

ScriptFunctionCallResult Floor(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return FloatResult("result", kb::math::Floor(FloatArg(arguments, "value")));
}

ScriptFunctionCallResult Ceil(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return FloatResult("result", kb::math::Ceil(FloatArg(arguments, "value")));
}

ScriptFunctionCallResult Round(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return FloatResult("result", kb::math::Round(FloatArg(arguments, "value")));
}

ScriptFunctionCallResult Frac(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return FloatResult("result", kb::math::Frac(FloatArg(arguments, "value")));
}

ScriptFunctionCallResult Mod(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return FloatResult("result", kb::math::Mod(FloatArg(arguments, "value"), FloatArg(arguments, "divisor")));
}

ScriptFunctionCallResult Sqrt(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return FloatResult("result", kb::math::Sqrt(FloatArg(arguments, "value")));
}

ScriptFunctionCallResult Pow(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return FloatResult("result", kb::math::Pow(FloatArg(arguments, "base"), FloatArg(arguments, "exponent")));
}

ScriptFunctionCallResult Exp(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return FloatResult("result", kb::math::Exp(FloatArg(arguments, "value")));
}

ScriptFunctionCallResult Log(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return FloatResult("result", kb::math::Log(FloatArg(arguments, "value")));
}

[[nodiscard]] ScriptFunctionCallResult DomainError(std::string_view functionName, std::string_view detail) {
    return ScriptFunctionCallResult{
        .executed = false,
        .outputs = {},
        .errors = { std::string{ functionName } + ": " + std::string{ detail } },
    };
}

ScriptFunctionCallResult Sin(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return FloatResult("result", kb::math::Sin(kb::math::Radians{ FloatArg(arguments, "angle") }));
}

ScriptFunctionCallResult Cos(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return FloatResult("result", kb::math::Cos(kb::math::Radians{ FloatArg(arguments, "angle") }));
}

ScriptFunctionCallResult Tan(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return FloatResult("result", kb::math::Tan(kb::math::Radians{ FloatArg(arguments, "angle") }));
}

// LIB-047: Asin/Acos have a genuinely restricted input domain ([-1,1]).
// Unlike every other Math.* function so far, an out-of-domain call here
// reports a real ScriptFunctionCallResult error (executed=false) instead
// of letting std::asin/std::asin's IEEE-754 NaN silently flow into a
// script graph, where it could reach e.g. Transform.SetRotation many
// nodes later with no indication of where the invalid value came from —
// exactly what "zdefiniowana domena błędu" (LIB-047) requires.
ScriptFunctionCallResult Asin(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    const float value = FloatArg(arguments, "value");
    if (value < -1.0F || value > 1.0F) {
        return DomainError("Math.Asin", "value must be in [-1, 1]");
    }
    return FloatResult("result", kb::math::Asin(value).Value());
}

ScriptFunctionCallResult Acos(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    const float value = FloatArg(arguments, "value");
    if (value < -1.0F || value > 1.0F) {
        return DomainError("Math.Acos", "value must be in [-1, 1]");
    }
    return FloatResult("result", kb::math::Acos(value).Value());
}

ScriptFunctionCallResult Atan(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return FloatResult("result", kb::math::Atan(FloatArg(arguments, "value")).Value());
}

ScriptFunctionCallResult Atan2(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return FloatResult("result", kb::math::Atan2(FloatArg(arguments, "y"), FloatArg(arguments, "x")).Value());
}

ScriptFunctionCallResult VecDot(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return FloatResult("result", kb::math::Dot(Vec3Arg(arguments, "a"), Vec3Arg(arguments, "b")));
}

ScriptFunctionCallResult VecCross(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return Vec3Result(kb::math::Cross(Vec3Arg(arguments, "a"), Vec3Arg(arguments, "b")));
}

ScriptFunctionCallResult VecLength(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return FloatResult("result", kb::math::Length(Vec3Arg(arguments, "value")));
}

ScriptFunctionCallResult VecNormalize(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return Vec3Result(kb::math::Normalize(Vec3Arg(arguments, "value")));
}

ScriptFunctionCallResult VecDistance(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return FloatResult("result", kb::math::Distance(Vec3Arg(arguments, "a"), Vec3Arg(arguments, "b")));
}

ScriptFunctionCallResult VecProject(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return Vec3Result(kb::math::Project(Vec3Arg(arguments, "value"), Vec3Arg(arguments, "onto")));
}

ScriptFunctionCallResult VecReflect(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return Vec3Result(kb::math::Reflect(Vec3Arg(arguments, "incident"), Vec3Arg(arguments, "normal")));
}

ScriptFunctionCallResult VecRefract(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return Vec3Result(kb::math::Refract(Vec3Arg(arguments, "incident"), Vec3Arg(arguments, "normal"), FloatArg(arguments, "eta")));
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
    ok = RegisterFunction(host, "Math.Min",
        {
            ScriptFunctionPin{ "a", ScriptValueType::Float, true },
            ScriptFunctionPin{ "b", ScriptValueType::Float, true },
        },
        { ScriptFunctionPin{ "result", ScriptValueType::Float, true } },
        &Min) && ok;
    ok = RegisterFunction(host, "Math.Max",
        {
            ScriptFunctionPin{ "a", ScriptValueType::Float, true },
            ScriptFunctionPin{ "b", ScriptValueType::Float, true },
        },
        { ScriptFunctionPin{ "result", ScriptValueType::Float, true } },
        &Max) && ok;
    ok = RegisterFunction(host, "Math.Abs",
        { ScriptFunctionPin{ "value", ScriptValueType::Float, true } },
        { ScriptFunctionPin{ "result", ScriptValueType::Float, true } },
        &Abs) && ok;
    ok = RegisterFunction(host, "Math.Sign",
        { ScriptFunctionPin{ "value", ScriptValueType::Float, true } },
        { ScriptFunctionPin{ "result", ScriptValueType::Float, true } },
        &Sign) && ok;
    ok = RegisterFunction(host, "Math.Floor",
        { ScriptFunctionPin{ "value", ScriptValueType::Float, true } },
        { ScriptFunctionPin{ "result", ScriptValueType::Float, true } },
        &Floor) && ok;
    ok = RegisterFunction(host, "Math.Ceil",
        { ScriptFunctionPin{ "value", ScriptValueType::Float, true } },
        { ScriptFunctionPin{ "result", ScriptValueType::Float, true } },
        &Ceil) && ok;
    ok = RegisterFunction(host, "Math.Round",
        { ScriptFunctionPin{ "value", ScriptValueType::Float, true } },
        { ScriptFunctionPin{ "result", ScriptValueType::Float, true } },
        &Round) && ok;
    ok = RegisterFunction(host, "Math.Frac",
        { ScriptFunctionPin{ "value", ScriptValueType::Float, true } },
        { ScriptFunctionPin{ "result", ScriptValueType::Float, true } },
        &Frac) && ok;
    ok = RegisterFunction(host, "Math.Mod",
        {
            ScriptFunctionPin{ "value", ScriptValueType::Float, true },
            ScriptFunctionPin{ "divisor", ScriptValueType::Float, true },
        },
        { ScriptFunctionPin{ "result", ScriptValueType::Float, true } },
        &Mod) && ok;
    ok = RegisterFunction(host, "Math.Sqrt",
        { ScriptFunctionPin{ "value", ScriptValueType::Float, true } },
        { ScriptFunctionPin{ "result", ScriptValueType::Float, true } },
        &Sqrt) && ok;
    ok = RegisterFunction(host, "Math.Pow",
        {
            ScriptFunctionPin{ "base", ScriptValueType::Float, true },
            ScriptFunctionPin{ "exponent", ScriptValueType::Float, true },
        },
        { ScriptFunctionPin{ "result", ScriptValueType::Float, true } },
        &Pow) && ok;
    ok = RegisterFunction(host, "Math.Exp",
        { ScriptFunctionPin{ "value", ScriptValueType::Float, true } },
        { ScriptFunctionPin{ "result", ScriptValueType::Float, true } },
        &Exp) && ok;
    ok = RegisterFunction(host, "Math.Log",
        { ScriptFunctionPin{ "value", ScriptValueType::Float, true } },
        { ScriptFunctionPin{ "result", ScriptValueType::Float, true } },
        &Log) && ok;
    ok = RegisterFunction(host, "Math.Sin",
        { ScriptFunctionPin{ "angle", ScriptValueType::Float, true } },
        { ScriptFunctionPin{ "result", ScriptValueType::Float, true } },
        &Sin) && ok;
    ok = RegisterFunction(host, "Math.Cos",
        { ScriptFunctionPin{ "angle", ScriptValueType::Float, true } },
        { ScriptFunctionPin{ "result", ScriptValueType::Float, true } },
        &Cos) && ok;
    ok = RegisterFunction(host, "Math.Tan",
        { ScriptFunctionPin{ "angle", ScriptValueType::Float, true } },
        { ScriptFunctionPin{ "result", ScriptValueType::Float, true } },
        &Tan) && ok;
    ok = RegisterFunction(host, "Math.Asin",
        { ScriptFunctionPin{ "value", ScriptValueType::Float, true } },
        { ScriptFunctionPin{ "result", ScriptValueType::Float, true } },
        &Asin) && ok;
    ok = RegisterFunction(host, "Math.Acos",
        { ScriptFunctionPin{ "value", ScriptValueType::Float, true } },
        { ScriptFunctionPin{ "result", ScriptValueType::Float, true } },
        &Acos) && ok;
    ok = RegisterFunction(host, "Math.Atan",
        { ScriptFunctionPin{ "value", ScriptValueType::Float, true } },
        { ScriptFunctionPin{ "result", ScriptValueType::Float, true } },
        &Atan) && ok;
    ok = RegisterFunction(host, "Math.Atan2",
        {
            ScriptFunctionPin{ "y", ScriptValueType::Float, true },
            ScriptFunctionPin{ "x", ScriptValueType::Float, true },
        },
        { ScriptFunctionPin{ "result", ScriptValueType::Float, true } },
        &Atan2) && ok;
    ok = RegisterFunction(host, "Math.Dot",
        ConcatPins(Vec3Pins("a"), Vec3Pins("b")),
        { ScriptFunctionPin{ "result", ScriptValueType::Float, true } },
        &VecDot) && ok;
    ok = RegisterFunction(host, "Math.Cross",
        ConcatPins(Vec3Pins("a"), Vec3Pins("b")),
        Vec3OutputPins(),
        &VecCross) && ok;
    ok = RegisterFunction(host, "Math.Length",
        Vec3Pins("value"),
        { ScriptFunctionPin{ "result", ScriptValueType::Float, true } },
        &VecLength) && ok;
    ok = RegisterFunction(host, "Math.Normalize",
        Vec3Pins("value"),
        Vec3OutputPins(),
        &VecNormalize) && ok;
    ok = RegisterFunction(host, "Math.Distance",
        ConcatPins(Vec3Pins("a"), Vec3Pins("b")),
        { ScriptFunctionPin{ "result", ScriptValueType::Float, true } },
        &VecDistance) && ok;
    ok = RegisterFunction(host, "Math.Project",
        ConcatPins(Vec3Pins("value"), Vec3Pins("onto")),
        Vec3OutputPins(),
        &VecProject) && ok;
    ok = RegisterFunction(host, "Math.Reflect",
        ConcatPins(Vec3Pins("incident"), Vec3Pins("normal")),
        Vec3OutputPins(),
        &VecReflect) && ok;
    ok = RegisterFunction(host, "Math.Refract",
        ConcatPins(ConcatPins(Vec3Pins("incident"), Vec3Pins("normal")), std::vector<ScriptFunctionPin>{ ScriptFunctionPin{ "eta", ScriptValueType::Float, true } }),
        Vec3OutputPins(),
        &VecRefract) && ok;
    return ok;
}

} // namespace kb::script
