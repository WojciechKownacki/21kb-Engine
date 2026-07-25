#include "engine/script/ScriptFunctionRegistry.hpp"

#include "engine/library/EngineLibraryTextEncoding.hpp"

#include <cstdint>
#include <exception>
#include <ranges>
#include <set>
#include <utility>

namespace kb::script {
namespace {

// LIB-037: a unified input limit shared by every script function call
// (Native, Lua, Visual Graph all funnel through ValidateInputs). Rejecting
// an oversized String argument here — rather than letting it flow into a
// callback that might log it, store it, or forward it to another
// system — keeps one enforcement point instead of every callback needing
// its own bound. kb::library::LibraryInputLimits::maxStringLength documents
// this same value for callers that want to check it before calling.
constexpr std::size_t kMaxScriptStringArgumentLength = 65536U;

// LIB-038: reentrancy guard. A callback that (directly, or through a chain
// of other functions calling each other) calls back into
// ScriptFunctionRegistry::Call on the same registry increments
// callDepth_; past this many nested calls, Call() rejects the call with a
// diagnostic instead of recursing until the stack overflows. 64 comfortably
// covers legitimate call chains (a handful of gameplay functions calling
// each other) while catching runaway recursion long before the stack is at
// risk.
constexpr std::size_t kMaxCallDepth = 64U;

// RAII so callDepth_ is decremented on every exit path (the normal return
// and the two catch blocks below all exit through this destructor), not
// duplicated at each return statement.
class CallDepthGuard final {
public:
    explicit CallDepthGuard(std::size_t& depth) noexcept
        : depth_(depth) {
        ++depth_;
    }
    ~CallDepthGuard() noexcept { --depth_; }
    CallDepthGuard(const CallDepthGuard&) = delete;
    CallDepthGuard& operator=(const CallDepthGuard&) = delete;

private:
    std::size_t& depth_;
};

[[nodiscard]] std::string TypeMismatchMessage(std::string_view functionName, std::string_view pinName, ScriptValueType actual, ScriptValueType expected) {
    return "script function '" + std::string{functionName} + "' pin '" + std::string{pinName} + "' is " + ToString(actual) + " but expects " + ToString(expected);
}

} // namespace

std::optional<ScriptValue> ScriptFunctionCallResult::Output(std::string_view name) const {
    const auto iter = std::ranges::find_if(outputs, [name](const ScriptFunctionArgument& output) {
        return output.name == name;
    });
    if (iter == outputs.end()) {
        return std::nullopt;
    }
    return iter->value;
}

bool ScriptFunctionRegistry::Register(ScriptFunctionDesc function) {
    if (locked_ || function.signature.name.empty() || function.signature.description.empty() || function.callback == nullptr || FindSignature(function.signature.name) != nullptr) {
        return false;
    }
    if (!HasValidPins(function.signature.inputs) || !HasValidPins(function.signature.outputs)) {
        return false;
    }
    functions_.push_back(std::move(function));
    return true;
}

const ScriptFunctionSignature* ScriptFunctionRegistry::FindSignature(std::string_view name) const noexcept {
    const auto iter = std::ranges::find_if(functions_, [name](const ScriptFunctionDesc& function) {
        return function.signature.name == name;
    });
    return iter == functions_.end() ? nullptr : &iter->signature;
}

bool ScriptFunctionRegistry::MarkDeprecated(std::string_view name, std::string message) noexcept {
    const auto iter = std::ranges::find_if(functions_, [name](const ScriptFunctionDesc& function) {
        return function.signature.name == name;
    });
    if (iter == functions_.end()) {
        return false;
    }
    iter->signature.deprecationMessage = std::move(message);
    return true;
}

const std::vector<ScriptFunctionDesc>& ScriptFunctionRegistry::Functions() const noexcept {
    return functions_;
}

ScriptFunctionCallResult ScriptFunctionRegistry::Call(
    std::string_view name,
    std::span<const ScriptFunctionArgument> arguments,
    const ScriptFunctionCallContext& context) const {
    const auto iter = std::ranges::find_if(functions_, [name](const ScriptFunctionDesc& function) {
        return function.signature.name == name;
    });
    if (iter == functions_.end()) {
        return ScriptFunctionCallResult{
            .errors = {"script function '" + std::string{name} + "' is not registered"},
        };
    }

    std::vector<ScriptFunctionArgument> normalized;
    std::vector<std::string> errors;
    ValidateInputs(iter->signature, arguments, normalized, errors);
    if (!errors.empty()) {
        return ScriptFunctionCallResult{ .errors = std::move(errors) };
    }

    // LIB-038: a callback can itself call back into Call() on this same
    // registry (directly, or through a chain of other functions calling
    // each other) — reject before invoking once the depth limit is
    // reached, rather than recursing until the native stack overflows,
    // which would crash the process instead of returning a diagnostic.
    if (callDepth_ >= kMaxCallDepth) {
        return ScriptFunctionCallResult{
            .errors = {
                "script function '" + std::string{ name } + "' exceeded the maximum call depth (" +
                std::to_string(kMaxCallDepth) + "); this usually means a reentrant or mutually recursive call chain" },
        };
    }

    // A registered callback (Native directly, or kb::library helpers like
    // EntityHandle::Validate() called from within one) can throw. This is
    // the single choke point every caller goes through — Native dispatch,
    // Lua's CallFunction, and the future Visual Graph CallNative node — so
    // catching here turns a thrown exception into an error result instead
    // of letting it unwind across the Lua C boundary, where PUC-Lua's
    // longjmp-based lua_pcall cannot catch a C++ exception (this engine's
    // Lua core is compiled as C, not C++) and the process would terminate.
    ScriptFunctionCallResult result;
    try {
        const CallDepthGuard depthGuard{ callDepth_ };
        result = iter->callback(context, normalized);
    } catch (const std::exception& exception) {
        return ScriptFunctionCallResult{
            .errors = { "script function '" + std::string{ name } + "' threw an exception: " + exception.what() },
        };
    } catch (...) {
        return ScriptFunctionCallResult{
            .errors = { "script function '" + std::string{ name } + "' threw a non-standard exception" },
        };
    }
    // LIB-025: the call was actually attempted (reached the callback),
    // regardless of whether it - or the output validation below - ends up
    // succeeding, so the caller learns it used a deprecated function either
    // way. A call rejected before this point (unknown name, bad input
    // type, reentrancy limit) never reaches here, so it never warns - there
    // was no real invocation to warn about.
    if (!iter->signature.deprecationMessage.empty()) {
        result.warnings.push_back(iter->signature.deprecationMessage);
    }

    if (!result.Succeeded()) {
        return result;
    }

    ValidateOutputs(iter->signature, result, result.errors);
    return result;
}

void ScriptFunctionRegistry::Lock() noexcept {
    locked_ = true;
}

bool ScriptFunctionRegistry::IsLocked() const noexcept {
    return locked_;
}

bool ScriptFunctionRegistry::HasValidPins(const std::vector<ScriptFunctionPin>& pins) {
    std::set<std::string_view> names;
    for (const ScriptFunctionPin& pin : pins) {
        if (pin.name.empty() || pin.type == ScriptValueType::Void) {
            return false;
        }
        if (!names.insert(pin.name).second) {
            return false;
        }
    }
    return true;
}

const ScriptFunctionPin* ScriptFunctionRegistry::FindPin(std::span<const ScriptFunctionPin> pins, std::string_view name) noexcept {
    const auto iter = std::ranges::find_if(pins, [name](const ScriptFunctionPin& pin) {
        return pin.name == name;
    });
    return iter == pins.end() ? nullptr : &*iter;
}

const ScriptFunctionArgument* ScriptFunctionRegistry::FindArgument(std::span<const ScriptFunctionArgument> arguments, std::string_view name) noexcept {
    const auto iter = std::ranges::find_if(arguments, [name](const ScriptFunctionArgument& argument) {
        return argument.name == name;
    });
    return iter == arguments.end() ? nullptr : &*iter;
}

ScriptFunctionArgument ScriptFunctionRegistry::CoerceArgument(const ScriptFunctionArgument& argument, ScriptValueType expectedType) {
    if (argument.value.Type() == expectedType) {
        return argument;
    }
    if (expectedType == ScriptValueType::Float && argument.value.Type() == ScriptValueType::Int) {
        return ScriptFunctionArgument{
            .name = argument.name,
            .value = ScriptValue{ static_cast<float>(argument.value.AsInt()) },
        };
    }
    if ((expectedType == ScriptValueType::Entity || expectedType == ScriptValueType::Component) && argument.value.Type() == ScriptValueType::Int && argument.value.AsInt() >= 0) {
        return ScriptFunctionArgument{
            .name = argument.name,
            .value = ScriptValue{ static_cast<std::uint64_t>(argument.value.AsInt()), expectedType },
        };
    }
    // LIB-050: a non-negative Int coerces to UInt32. The Lua bridge infers
    // a small non-negative integer literal (a Random/Noise seed or index)
    // as Int (PucLuaValueBridge::FromLua), so without this a Lua call to a
    // UInt32-pinned function (Math.Random01, Math.Noise2D/3D, ...) would be
    // rejected as a type mismatch — the API was registered but uncallable
    // from Lua. A non-negative `int` (0..INT32_MAX) always fits UInt32, so
    // the narrowing cast is lossless; a negative Int is left unconverted
    // and fails validation loudly rather than wrapping to a huge seed.
    if (expectedType == ScriptValueType::UInt32 && argument.value.Type() == ScriptValueType::Int && argument.value.AsInt() >= 0) {
        return ScriptFunctionArgument{
            .name = argument.name,
            .value = ScriptValue{ static_cast<std::uint32_t>(argument.value.AsInt()) },
        };
    }
    // LIB-058: a non-negative Int coerces to Hash — an opaque handle (an
    // Array/Set/Map/Queue/Stack handle, LIB-058) is a small non-negative id
    // the Lua bridge marshals as Int, so a Hash-pinned handle argument
    // would otherwise be rejected on the round trip Create -> handle ->
    // Push. Same non-negative rule and lossless-widening reasoning as the
    // Entity/Component coercion above (all three share the uint64 storage).
    if (expectedType == ScriptValueType::Hash && argument.value.Type() == ScriptValueType::Int && argument.value.AsInt() >= 0) {
        return ScriptFunctionArgument{
            .name = argument.name,
            .value = ScriptValue{ static_cast<std::uint64_t>(argument.value.AsInt()), ScriptValueType::Hash },
        };
    }
    // LIB-041: complete the coercions for the remaining expanded value types
    // so a Lua caller can actually reach functions pinned to them. The Lua
    // bridge marshals numeric literals as Int/Float and text as String
    // (PucLuaValueBridge::FromLua), so without these an Int64/Double/Name/Guid
    // pin was registered but uncallable from Lua. Every widening is lossless
    // (int->int64, int/float->double); String->Name/Guid re-tags the same text
    // as its intended semantic type.
    if (expectedType == ScriptValueType::Int64 && argument.value.Type() == ScriptValueType::Int) {
        return ScriptFunctionArgument{ .name = argument.name, .value = ScriptValue{ static_cast<std::int64_t>(argument.value.AsInt()) } };
    }
    if (expectedType == ScriptValueType::Double && argument.value.Type() == ScriptValueType::Int) {
        return ScriptFunctionArgument{ .name = argument.name, .value = ScriptValue{ static_cast<double>(argument.value.AsInt()) } };
    }
    if (expectedType == ScriptValueType::Double && argument.value.Type() == ScriptValueType::Float) {
        return ScriptFunctionArgument{ .name = argument.name, .value = ScriptValue{ static_cast<double>(argument.value.AsFloat()) } };
    }
    if (expectedType == ScriptValueType::Name && argument.value.Type() == ScriptValueType::String) {
        return ScriptFunctionArgument{ .name = argument.name, .value = ScriptValue{ argument.value.AsString(), ScriptValueType::Name } };
    }
    if (expectedType == ScriptValueType::Guid && argument.value.Type() == ScriptValueType::String) {
        return ScriptFunctionArgument{ .name = argument.name, .value = ScriptValue{ argument.value.AsString(), ScriptValueType::Guid } };
    }
    return argument;
}

bool ScriptFunctionRegistry::IsCompatible(ScriptValue value, ScriptValueType expectedType) noexcept {
    if (value.Type() == expectedType) {
        return true;
    }
    return (expectedType == ScriptValueType::Float && value.Type() == ScriptValueType::Int) ||
           ((expectedType == ScriptValueType::Entity || expectedType == ScriptValueType::Component) && value.Type() == ScriptValueType::Int && value.AsInt() >= 0) ||
           // LIB-050: a non-negative Int satisfies a UInt32 pin (see
           // CoerceArgument for why the Lua Random/Noise seed/index path
           // needs this). Negative Ints stay incompatible and fail loudly.
           (expectedType == ScriptValueType::UInt32 && value.Type() == ScriptValueType::Int && value.AsInt() >= 0) ||
           // LIB-058: a non-negative Int satisfies a Hash pin (an opaque
           // collection handle marshalled from Lua as Int).
           (expectedType == ScriptValueType::Hash && value.Type() == ScriptValueType::Int && value.AsInt() >= 0) ||
           // LIB-041: the remaining expanded value types accept the Int/Float/
           // String a Lua caller actually produces (see CoerceArgument). All
           // lossless widenings; String->Name/Guid re-tags the text.
           (expectedType == ScriptValueType::Int64 && value.Type() == ScriptValueType::Int) ||
           (expectedType == ScriptValueType::Double && (value.Type() == ScriptValueType::Int || value.Type() == ScriptValueType::Float)) ||
           (expectedType == ScriptValueType::Name && value.Type() == ScriptValueType::String) ||
           (expectedType == ScriptValueType::Guid && value.Type() == ScriptValueType::String);
}

void ScriptFunctionRegistry::ValidateInputs(
    const ScriptFunctionSignature& signature,
    std::span<const ScriptFunctionArgument> arguments,
    std::vector<ScriptFunctionArgument>& normalized,
    std::vector<std::string>& errors) {
    normalized.reserve(signature.inputs.size());
    std::set<std::string_view> seenArguments;
    for (const ScriptFunctionArgument& argument : arguments) {
        if (argument.name.empty()) {
            errors.push_back("script function '" + signature.name + "' received an unnamed input argument");
            continue;
        }
        if (!seenArguments.insert(argument.name).second) {
            errors.push_back("script function '" + signature.name + "' received duplicate input argument '" + argument.name + "'");
            continue;
        }
        if (FindPin(signature.inputs, argument.name) == nullptr) {
            errors.push_back("script function '" + signature.name + "' received unknown input argument '" + argument.name + "'");
        }
    }
    for (const ScriptFunctionPin& input : signature.inputs) {
        const ScriptFunctionArgument* argument = FindArgument(arguments, input.name);
        if (argument == nullptr) {
            if (input.required) {
                errors.push_back("script function '" + signature.name + "' missing required input '" + input.name + "'");
            }
            continue;
        }
        if (!IsCompatible(argument->value, input.type)) {
            errors.push_back(TypeMismatchMessage(signature.name, input.name, argument->value.Type(), input.type));
            continue;
        }
        if (argument->value.Type() == ScriptValueType::String && argument->value.AsString().size() > kMaxScriptStringArgumentLength) {
            errors.push_back(
                "script function '" + signature.name + "' input '" + input.name + "' exceeds the maximum string length (" +
                std::to_string(kMaxScriptStringArgumentLength) + " bytes)");
            continue;
        }
        // LIB-064: UTF-8 is the only encoding a public String value may
        // use — enforced at this same choke point (Native, Lua, Visual
        // Graph all funnel through ValidateInputs) so a malformed byte
        // sequence never reaches a callback that might log, store, or
        // forward it further.
        if (argument->value.Type() == ScriptValueType::String && !kb::library::IsValidUtf8(argument->value.AsString())) {
            errors.push_back("script function '" + signature.name + "' input '" + input.name + "' is not valid UTF-8");
            continue;
        }
        normalized.push_back(CoerceArgument(*argument, input.type));
    }
}

void ScriptFunctionRegistry::ValidateOutputs(
    const ScriptFunctionSignature& signature,
    const ScriptFunctionCallResult& result,
    std::vector<std::string>& errors) {
    std::set<std::string_view> seenOutputs;
    for (const ScriptFunctionArgument& output : result.outputs) {
        if (output.name.empty()) {
            errors.push_back("script function '" + signature.name + "' returned an unnamed output argument");
            continue;
        }
        if (!seenOutputs.insert(output.name).second) {
            errors.push_back("script function '" + signature.name + "' returned duplicate output argument '" + output.name + "'");
            continue;
        }
        if (FindPin(signature.outputs, output.name) == nullptr) {
            errors.push_back("script function '" + signature.name + "' returned unknown output argument '" + output.name + "'");
        }
    }
    for (const ScriptFunctionPin& output : signature.outputs) {
        const auto value = result.Output(output.name);
        if (!value.has_value()) {
            if (output.required) {
                errors.push_back("script function '" + signature.name + "' missing required output '" + output.name + "'");
            }
            continue;
        }
        if (!IsCompatible(*value, output.type)) {
            errors.push_back(TypeMismatchMessage(signature.name, output.name, value->Type(), output.type));
        }
    }
}

} // namespace kb::script
