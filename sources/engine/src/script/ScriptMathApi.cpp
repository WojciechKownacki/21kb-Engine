#include "engine/script/ScriptMathApi.hpp"

#include "engine/math/EngineMath.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <cstdint>
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

// LIB-050: seed/index are UInt32 (LIB-041), not Float — a seed is an
// opaque identifier, not a continuous quantity that Lerp/Clamp/etc would
// ever operate on.
[[nodiscard]] std::uint32_t UInt32Arg(std::span<const ScriptFunctionArgument> arguments, std::string_view name, std::uint32_t fallback = 0U) noexcept {
    const ScriptValue* value = FindArg(arguments, name);
    return value == nullptr ? fallback : value->AsUInt32(fallback);
}

[[nodiscard]] int IntArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name, int fallback = 0) noexcept {
    const ScriptValue* value = FindArg(arguments, name);
    return value == nullptr ? fallback : value->AsInt(fallback);
}

// LIB-051: RandomStream's state ({seed, counter}, both UInt32) decomposes
// into two named pins — streamSeed/streamCounter — the same convention as
// Vec3/Quat's <prefix>X/Y/Z/W, just with descriptive names instead of a
// prefix since a Random* function only ever takes one stream.
[[nodiscard]] kb::math::RandomStream RandomStreamArg(std::span<const ScriptFunctionArgument> arguments) noexcept {
    return kb::math::RandomStream{ UInt32Arg(arguments, "streamSeed"), UInt32Arg(arguments, "streamCounter") };
}

[[nodiscard]] std::vector<ScriptFunctionPin> RandomStreamPins() {
    return {
        ScriptFunctionPin{ "streamSeed", ScriptValueType::UInt32, true },
        ScriptFunctionPin{ "streamCounter", ScriptValueType::UInt32, true },
    };
}

// Every RandomStream-consuming function returns BOTH its result AND the
// advanced stream (never mutates the input stream — LIB-032 forbids
// references across the script boundary, so the caller must explicitly
// re-thread streamSeed/streamCounter into its next call, exactly like
// Damp's caller re-threads velocity).
[[nodiscard]] ScriptFunctionCallResult ValueAndStreamResult(ScriptValue value, kb::math::RandomStream stream) {
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "value", std::move(value) },
            ScriptFunctionArgument{ "streamSeed", ScriptValue{ stream.seed } },
            ScriptFunctionArgument{ "streamCounter", ScriptValue{ stream.counter } },
        },
        .errors = {},
    };
}

[[nodiscard]] std::vector<ScriptFunctionPin> ValueAndStreamOutputPins(ScriptValueType valueType) {
    return {
        ScriptFunctionPin{ "value", valueType, true },
        ScriptFunctionPin{ "streamSeed", ScriptValueType::UInt32, true },
        ScriptFunctionPin{ "streamCounter", ScriptValueType::UInt32, true },
    };
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

// LIB-049: same decompose-into-named-scalar-pins convention as Vec3Arg/
// Vec3Result/Vec3Pins, for Quat-shaped Math.* functions.
[[nodiscard]] kb::math::Quat QuatArg(std::span<const ScriptFunctionArgument> arguments, std::string_view prefix) noexcept {
    return kb::math::Quat{
        FloatArg(arguments, std::string{ prefix } + "X"),
        FloatArg(arguments, std::string{ prefix } + "Y"),
        FloatArg(arguments, std::string{ prefix } + "Z"),
        FloatArg(arguments, std::string{ prefix } + "W", 1.0F),
    };
}

[[nodiscard]] ScriptFunctionCallResult QuatResult(kb::math::Quat value) {
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "x", ScriptValue{ value.x } },
            ScriptFunctionArgument{ "y", ScriptValue{ value.y } },
            ScriptFunctionArgument{ "z", ScriptValue{ value.z } },
            ScriptFunctionArgument{ "w", ScriptValue{ value.w } },
        },
        .errors = {},
    };
}

[[nodiscard]] std::vector<ScriptFunctionPin> QuatPins(std::string_view prefix) {
    return {
        ScriptFunctionPin{ std::string{ prefix } + "X", ScriptValueType::Float, true },
        ScriptFunctionPin{ std::string{ prefix } + "Y", ScriptValueType::Float, true },
        ScriptFunctionPin{ std::string{ prefix } + "Z", ScriptValueType::Float, true },
        ScriptFunctionPin{ std::string{ prefix } + "W", ScriptValueType::Float, true },
    };
}

[[nodiscard]] std::vector<ScriptFunctionPin> QuatOutputPins() {
    return {
        ScriptFunctionPin{ "x", ScriptValueType::Float, true },
        ScriptFunctionPin{ "y", ScriptValueType::Float, true },
        ScriptFunctionPin{ "z", ScriptValueType::Float, true },
        ScriptFunctionPin{ "w", ScriptValueType::Float, true },
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

ScriptFunctionCallResult VecAngle(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return FloatResult("result", kb::math::Angle(Vec3Arg(arguments, "a"), Vec3Arg(arguments, "b")).Value());
}

ScriptFunctionCallResult VecSignedAngle(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return FloatResult("result", kb::math::SignedAngle(Vec3Arg(arguments, "a"), Vec3Arg(arguments, "b"), Vec3Arg(arguments, "axis")).Value());
}

ScriptFunctionCallResult QuatSlerp(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return QuatResult(kb::math::Slerp(QuatArg(arguments, "a"), QuatArg(arguments, "b"), FloatArg(arguments, "t")));
}

ScriptFunctionCallResult QuatLookRotation(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return QuatResult(kb::math::LookRotation(Vec3Arg(arguments, "forward"), Vec3Arg(arguments, "up")));
}

ScriptFunctionCallResult QuatFromToRotation(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return QuatResult(kb::math::FromToRotation(Vec3Arg(arguments, "from"), Vec3Arg(arguments, "to")));
}

ScriptFunctionCallResult QuatRotateTowards(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return QuatResult(kb::math::RotateTowards(QuatArg(arguments, "from"), QuatArg(arguments, "to"), kb::math::Radians{ FloatArg(arguments, "maxDelta") }));
}

ScriptFunctionCallResult Random01(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return FloatResult("result", kb::math::Random01(UInt32Arg(arguments, "seed"), UInt32Arg(arguments, "index")));
}

ScriptFunctionCallResult NoiseFn1D(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return FloatResult("result", kb::math::Noise1D(FloatArg(arguments, "x"), UInt32Arg(arguments, "seed")));
}

ScriptFunctionCallResult NoiseFn2D(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return FloatResult("result", kb::math::Noise2D(FloatArg(arguments, "x"), FloatArg(arguments, "y"), UInt32Arg(arguments, "seed")));
}

ScriptFunctionCallResult NoiseFn3D(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return FloatResult("result", kb::math::Noise3D(FloatArg(arguments, "x"), FloatArg(arguments, "y"), FloatArg(arguments, "z"), UInt32Arg(arguments, "seed")));
}

ScriptFunctionCallResult RandomSeedFn(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    const kb::math::RandomStream stream = kb::math::MakeRandomStream(UInt32Arg(arguments, "seed"));
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "streamSeed", ScriptValue{ stream.seed } },
            ScriptFunctionArgument{ "streamCounter", ScriptValue{ stream.counter } },
        },
        .errors = {},
    };
}

ScriptFunctionCallResult RandomNextUInt32Fn(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    const kb::math::RandomStreamUInt32Result result = kb::math::NextUInt32(RandomStreamArg(arguments));
    return ValueAndStreamResult(ScriptValue{ result.value }, result.stream);
}

ScriptFunctionCallResult RandomNextFloat01Fn(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    const kb::math::RandomStreamFloatResult result = kb::math::NextFloat01(RandomStreamArg(arguments));
    return ValueAndStreamResult(ScriptValue{ result.value }, result.stream);
}

ScriptFunctionCallResult RandomRangeFn(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    const kb::math::RandomStreamRangeResult result = kb::math::NextRange(RandomStreamArg(arguments), FloatArg(arguments, "min"), FloatArg(arguments, "max"));
    return ValueAndStreamResult(ScriptValue{ result.value }, result.stream);
}

ScriptFunctionCallResult RandomRangeIntFn(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    const kb::math::RandomStreamIntRangeResult result = kb::math::NextIntRange(RandomStreamArg(arguments), IntArg(arguments, "min"), IntArg(arguments, "max"));
    return ValueAndStreamResult(ScriptValue{ result.value }, result.stream);
}

// LIB-052: Easing is exposed to scripts as its ordinal (Int), the same
// pattern ScriptSceneComponentApi already uses for enum-typed component
// fields (CameraProjection, LightKind, ...) — there is no dedicated enum
// ScriptValueType. Since Evaluate()'s switch has no default case (every
// enumerator is handled explicitly, so adding a new Easing value is a
// compile error everywhere it isn't), an out-of-range ordinal cast to
// Easing would be undefined behavior; this validates the ordinal against
// the real enum range and reports a domain error instead (LIB-047's
// pattern for Asin/Acos), rather than casting an unchecked int.
ScriptFunctionCallResult Ease(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    const int easingOrdinal = IntArg(arguments, "easing");
    if (easingOrdinal < 0 || easingOrdinal > static_cast<int>(kb::math::Easing::InOutBounce)) {
        return DomainError("Math.Ease", "easing must be a valid Easing ordinal");
    }
    return FloatResult("result", kb::math::Evaluate(static_cast<kb::math::Easing>(easingOrdinal), FloatArg(arguments, "t")));
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
    ok = RegisterFunction(host, "Math.Angle",
        ConcatPins(Vec3Pins("a"), Vec3Pins("b")),
        { ScriptFunctionPin{ "result", ScriptValueType::Float, true } },
        &VecAngle) && ok;
    ok = RegisterFunction(host, "Math.SignedAngle",
        ConcatPins(ConcatPins(Vec3Pins("a"), Vec3Pins("b")), Vec3Pins("axis")),
        { ScriptFunctionPin{ "result", ScriptValueType::Float, true } },
        &VecSignedAngle) && ok;
    ok = RegisterFunction(host, "Math.Slerp",
        ConcatPins(ConcatPins(QuatPins("a"), QuatPins("b")), std::vector<ScriptFunctionPin>{ ScriptFunctionPin{ "t", ScriptValueType::Float, true } }),
        QuatOutputPins(),
        &QuatSlerp) && ok;
    ok = RegisterFunction(host, "Math.LookRotation",
        ConcatPins(Vec3Pins("forward"), Vec3Pins("up")),
        QuatOutputPins(),
        &QuatLookRotation) && ok;
    ok = RegisterFunction(host, "Math.FromToRotation",
        ConcatPins(Vec3Pins("from"), Vec3Pins("to")),
        QuatOutputPins(),
        &QuatFromToRotation) && ok;
    ok = RegisterFunction(host, "Math.RotateTowards",
        ConcatPins(ConcatPins(QuatPins("from"), QuatPins("to")), std::vector<ScriptFunctionPin>{ ScriptFunctionPin{ "maxDelta", ScriptValueType::Float, true } }),
        QuatOutputPins(),
        &QuatRotateTowards) && ok;
    ok = RegisterFunction(host, "Math.Random01",
        {
            ScriptFunctionPin{ "seed", ScriptValueType::UInt32, true },
            ScriptFunctionPin{ "index", ScriptValueType::UInt32, true },
        },
        { ScriptFunctionPin{ "result", ScriptValueType::Float, true } },
        &Random01) && ok;
    ok = RegisterFunction(host, "Math.Noise1D",
        {
            ScriptFunctionPin{ "x", ScriptValueType::Float, true },
            ScriptFunctionPin{ "seed", ScriptValueType::UInt32, true },
        },
        { ScriptFunctionPin{ "result", ScriptValueType::Float, true } },
        &NoiseFn1D) && ok;
    ok = RegisterFunction(host, "Math.Noise2D",
        {
            ScriptFunctionPin{ "x", ScriptValueType::Float, true },
            ScriptFunctionPin{ "y", ScriptValueType::Float, true },
            ScriptFunctionPin{ "seed", ScriptValueType::UInt32, true },
        },
        { ScriptFunctionPin{ "result", ScriptValueType::Float, true } },
        &NoiseFn2D) && ok;
    ok = RegisterFunction(host, "Math.Noise3D",
        {
            ScriptFunctionPin{ "x", ScriptValueType::Float, true },
            ScriptFunctionPin{ "y", ScriptValueType::Float, true },
            ScriptFunctionPin{ "z", ScriptValueType::Float, true },
            ScriptFunctionPin{ "seed", ScriptValueType::UInt32, true },
        },
        { ScriptFunctionPin{ "result", ScriptValueType::Float, true } },
        &NoiseFn3D) && ok;
    ok = RegisterFunction(host, "Math.RandomSeed",
        { ScriptFunctionPin{ "seed", ScriptValueType::UInt32, true } },
        RandomStreamPins(),
        &RandomSeedFn) && ok;
    ok = RegisterFunction(host, "Math.RandomNextUInt32",
        RandomStreamPins(),
        ValueAndStreamOutputPins(ScriptValueType::UInt32),
        &RandomNextUInt32Fn) && ok;
    ok = RegisterFunction(host, "Math.RandomNextFloat01",
        RandomStreamPins(),
        ValueAndStreamOutputPins(ScriptValueType::Float),
        &RandomNextFloat01Fn) && ok;
    ok = RegisterFunction(host, "Math.RandomRange",
        ConcatPins(RandomStreamPins(), std::vector<ScriptFunctionPin>{ ScriptFunctionPin{ "min", ScriptValueType::Float, true }, ScriptFunctionPin{ "max", ScriptValueType::Float, true } }),
        ValueAndStreamOutputPins(ScriptValueType::Float),
        &RandomRangeFn) && ok;
    ok = RegisterFunction(host, "Math.RandomRangeInt",
        ConcatPins(RandomStreamPins(), std::vector<ScriptFunctionPin>{ ScriptFunctionPin{ "min", ScriptValueType::Int, true }, ScriptFunctionPin{ "max", ScriptValueType::Int, true } }),
        ValueAndStreamOutputPins(ScriptValueType::Int),
        &RandomRangeIntFn) && ok;
    ok = RegisterFunction(host, "Math.Ease",
        {
            ScriptFunctionPin{ "easing", ScriptValueType::Int, true },
            ScriptFunctionPin{ "t", ScriptValueType::Float, true },
        },
        { ScriptFunctionPin{ "result", ScriptValueType::Float, true } },
        &Ease) && ok;
    return ok;
}

} // namespace kb::script
