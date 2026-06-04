#include "engine/script/ScriptFunctionRegistry.hpp"

#include <cstdint>
#include <ranges>
#include <set>
#include <utility>

namespace kb::script {
namespace {

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
    if (function.signature.name.empty() || function.callback == nullptr || FindSignature(function.signature.name) != nullptr) {
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

    ScriptFunctionCallResult result = iter->callback(context, normalized);
    if (!result.Succeeded()) {
        return result;
    }

    ValidateOutputs(iter->signature, result, result.errors);
    return result;
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
    return argument;
}

bool ScriptFunctionRegistry::IsCompatible(ScriptValue value, ScriptValueType expectedType) noexcept {
    if (value.Type() == expectedType) {
        return true;
    }
    return (expectedType == ScriptValueType::Float && value.Type() == ScriptValueType::Int) ||
           ((expectedType == ScriptValueType::Entity || expectedType == ScriptValueType::Component) && value.Type() == ScriptValueType::Int && value.AsInt() >= 0);
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
