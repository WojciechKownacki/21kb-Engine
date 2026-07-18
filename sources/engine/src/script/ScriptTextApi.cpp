#include "engine/script/ScriptTextApi.hpp"

#include "engine/library/EngineLibraryParsing.hpp"
#include "engine/math/EngineMath.hpp"
#include "engine/script/ScriptFunctionRegistry.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"

#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kb::script {
namespace {

[[nodiscard]] std::string StringArg(std::span<const ScriptFunctionArgument> arguments, std::string_view name) {
    for (const ScriptFunctionArgument& argument : arguments) {
        if (argument.name == name) {
            return argument.value.AsString();
        }
    }
    return {};
}

[[nodiscard]] std::vector<ScriptFunctionPin> TextInputPins() {
    return { ScriptFunctionPin{ "text", ScriptValueType::String, true } };
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

// Every Text.Parse* function returns `ok` (Bool) plus a typed value. On
// failure `ok` is false and the value output holds its default — the value
// output is meaningful ONLY when ok is true, mirroring the TryParse*
// contract (output written only on success) at the script boundary.
ScriptFunctionCallResult ParseInt(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    std::int64_t value = 0;
    const bool ok = kb::library::TryParseInt64(StringArg(arguments, "text"), value);
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "ok", ScriptValue{ ok } },
            ScriptFunctionArgument{ "value", ScriptValue{ value } },
        },
        .errors = {},
    };
}

ScriptFunctionCallResult ParseUInt(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    std::uint64_t value = 0U;
    const bool ok = kb::library::TryParseUInt64(StringArg(arguments, "text"), value);
    // A parsed unsigned 64-bit value is carried losslessly in the Hash
    // (raw uint64) slot — the only ScriptValue alternative wide enough to
    // hold the full unsigned range without truncation.
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "ok", ScriptValue{ ok } },
            ScriptFunctionArgument{ "value", ScriptValue{ value, ScriptValueType::Hash } },
        },
        .errors = {},
    };
}

ScriptFunctionCallResult ParseFloat(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    double value = 0.0;
    const bool ok = kb::library::TryParseDouble(StringArg(arguments, "text"), value);
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "ok", ScriptValue{ ok } },
            ScriptFunctionArgument{ "value", ScriptValue{ value } },
        },
        .errors = {},
    };
}

ScriptFunctionCallResult IsGuid(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = { ScriptFunctionArgument{ "result", ScriptValue{ kb::library::TryParseGuid(StringArg(arguments, "text")) } } },
        .errors = {},
    };
}

ScriptFunctionCallResult ParseColor(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    kb::math::Color color{};
    const bool ok = kb::library::TryParseColor(StringArg(arguments, "text"), color);
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "ok", ScriptValue{ ok } },
            ScriptFunctionArgument{ "r", ScriptValue{ color.r } },
            ScriptFunctionArgument{ "g", ScriptValue{ color.g } },
            ScriptFunctionArgument{ "b", ScriptValue{ color.b } },
            ScriptFunctionArgument{ "a", ScriptValue{ color.a } },
        },
        .errors = {},
    };
}

ScriptFunctionCallResult ParseDate(const ScriptFunctionCallContext&, std::span<const ScriptFunctionArgument> arguments) {
    std::chrono::year_month_day date{};
    const bool ok = kb::library::TryParseDate(StringArg(arguments, "text"), date);
    return ScriptFunctionCallResult{
        .executed = true,
        .outputs = {
            ScriptFunctionArgument{ "ok", ScriptValue{ ok } },
            ScriptFunctionArgument{ "year", ScriptValue{ static_cast<int>(date.year()) } },
            ScriptFunctionArgument{ "month", ScriptValue{ static_cast<int>(static_cast<unsigned>(date.month())) } },
            ScriptFunctionArgument{ "day", ScriptValue{ static_cast<int>(static_cast<unsigned>(date.day())) } },
        },
        .errors = {},
    };
}

} // namespace

bool ScriptTextApi::Register(ScriptRuntimeHost& host) {
    bool ok = true;
    ok = RegisterFunction(host, "Text.ParseInt", TextInputPins(),
        {
            ScriptFunctionPin{ "ok", ScriptValueType::Bool, true },
            ScriptFunctionPin{ "value", ScriptValueType::Int64, true },
        },
        &ParseInt) && ok;
    ok = RegisterFunction(host, "Text.ParseUInt", TextInputPins(),
        {
            ScriptFunctionPin{ "ok", ScriptValueType::Bool, true },
            ScriptFunctionPin{ "value", ScriptValueType::Hash, true },
        },
        &ParseUInt) && ok;
    ok = RegisterFunction(host, "Text.ParseFloat", TextInputPins(),
        {
            ScriptFunctionPin{ "ok", ScriptValueType::Bool, true },
            ScriptFunctionPin{ "value", ScriptValueType::Double, true },
        },
        &ParseFloat) && ok;
    ok = RegisterFunction(host, "Text.IsGuid", TextInputPins(),
        { ScriptFunctionPin{ "result", ScriptValueType::Bool, true } },
        &IsGuid) && ok;
    ok = RegisterFunction(host, "Text.ParseColor", TextInputPins(),
        {
            ScriptFunctionPin{ "ok", ScriptValueType::Bool, true },
            ScriptFunctionPin{ "r", ScriptValueType::Float, true },
            ScriptFunctionPin{ "g", ScriptValueType::Float, true },
            ScriptFunctionPin{ "b", ScriptValueType::Float, true },
            ScriptFunctionPin{ "a", ScriptValueType::Float, true },
        },
        &ParseColor) && ok;
    ok = RegisterFunction(host, "Text.ParseDate", TextInputPins(),
        {
            ScriptFunctionPin{ "ok", ScriptValueType::Bool, true },
            ScriptFunctionPin{ "year", ScriptValueType::Int, true },
            ScriptFunctionPin{ "month", ScriptValueType::Int, true },
            ScriptFunctionPin{ "day", ScriptValueType::Int, true },
        },
        &ParseDate) && ok;
    return ok;
}

} // namespace kb::script
