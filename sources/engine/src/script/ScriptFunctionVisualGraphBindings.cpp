#include "engine/script/ScriptFunctionVisualGraphBindings.hpp"

#include <optional>
#include <string>
#include <utility>

namespace kb::script {
namespace {

[[nodiscard]] kb::visual::VisualGraphValueType ToGraphType(ScriptValueType type) noexcept {
    return ToVisualGraphValueType(type);
}

[[nodiscard]] ScriptValue ToScriptValue(const kb::visual::VisualGraphRuntimeValue& value, ScriptValueType expectedType) {
    switch (expectedType) {
    case ScriptValueType::Bool:
        return ScriptValue{ value.AsBool() };
    case ScriptValueType::Int:
        return ScriptValue{ value.AsInt() };
    case ScriptValueType::Float:
        return ScriptValue{ value.AsFloat() };
    case ScriptValueType::String:
        return ScriptValue{ value.AsString() };
    case ScriptValueType::Entity:
    case ScriptValueType::Component:
    case ScriptValueType::Hash:
        return ScriptValue{ value.AsUInt64(), expectedType };
    case ScriptValueType::UInt32:
        return ScriptValue{ static_cast<std::uint32_t>(value.AsUInt64()) };
    case ScriptValueType::Int64:
        return ScriptValue{ value.AsInt64() };
    case ScriptValueType::Double:
        return ScriptValue{ value.AsDouble() };
    case ScriptValueType::Name:
    case ScriptValueType::Guid:
        return ScriptValue{ value.AsString(), expectedType };
    case ScriptValueType::Void:
        break;
    }
    return ScriptValue{};
}

[[nodiscard]] std::string FunctionSymbol(std::string_view functionName) {
    return "Function." + std::string{ functionName };
}

[[nodiscard]] std::vector<kb::visual::VisualGraphPinSignature> ToGraphPins(const std::vector<ScriptFunctionPin>& pins) {
    std::vector<kb::visual::VisualGraphPinSignature> graphPins;
    graphPins.reserve(pins.size());
    for (const ScriptFunctionPin& pin : pins) {
        graphPins.push_back(kb::visual::VisualGraphPinSignature{
            .name = pin.name,
            .type = ToGraphType(pin.type),
            .required = pin.required,
        });
    }
    return graphPins;
}

[[nodiscard]] const kb::visual::VisualGraphIrInput* FindInput(const kb::visual::VisualGraphIrInstruction& instruction, std::string_view name) noexcept {
    for (const kb::visual::VisualGraphIrInput& input : instruction.inputs) {
        if (input.name == name) {
            return &input;
        }
    }
    return nullptr;
}

[[nodiscard]] std::vector<ScriptFunctionArgument> ReadArguments(
    kb::visual::VisualGraphRuntimeExecutionContext& context,
    const kb::visual::VisualGraphIrInstruction& instruction,
    const ScriptFunctionSignature& signature) {
    std::vector<ScriptFunctionArgument> arguments;
    arguments.reserve(signature.inputs.size());
    for (const ScriptFunctionPin& pin : signature.inputs) {
        const kb::visual::VisualGraphIrInput* input = FindInput(instruction, pin.name);
        if (input == nullptr) {
            continue;
        }
        const kb::visual::VisualGraphRuntimeValue* value = context.TryRead(input->sourceNodeId, input->sourcePin);
        if (value == nullptr) {
            continue;
        }
        arguments.push_back(ScriptFunctionArgument{
            .name = pin.name,
            .value = ToScriptValue(*value, pin.type),
        });
    }
    return arguments;
}

void StoreOutputs(
    kb::visual::VisualGraphRuntimeExecutionContext& context,
    const kb::visual::VisualGraphIrInstruction& instruction,
    const ScriptFunctionSignature& signature,
    const ScriptFunctionCallResult& result) {
    for (const ScriptFunctionPin& output : signature.outputs) {
        const std::optional<ScriptValue> value = result.Output(output.name);
        if (value.has_value()) {
            context.Store(instruction.sourceNodeId, output.name, value->ToVisualGraphValue());
        }
    }
}

[[nodiscard]] kb::visual::VisualGraphRuntimeBinding RuntimeBinding(
    const ScriptFunctionRegistry& functions,
    const ScriptFunctionSignature& signature,
    kb::scene::Scene& scene) {
    return kb::visual::VisualGraphRuntimeBinding{
        .opcode = kb::visual::VisualGraphIrOpcode::CallNative,
        .symbol = FunctionSymbol(signature.name),
        .inputs = ToGraphPins(signature.inputs),
        .outputs = ToGraphPins(signature.outputs),
        .callback = [&functions, &scene, signature](kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInstruction& instruction) {
            const std::vector<ScriptFunctionArgument> arguments = ReadArguments(context, instruction, signature);
            const ScriptFunctionCallResult result = functions.Call(signature.name, arguments, ScriptFunctionCallContext{
                                                                                                      .scene = &scene,
                                                                                                      .caller = kb::scene::SceneEntity{ context.ReadUInt64(0U, "self") },
                                                                                                      .callerBackend = kb::scene::BehaviourBackend::VisualGraph,
                                                                                                      .deltaSeconds = context.ReadFloat(0U, "deltaSeconds"),
                                                                                                  });
            if (!result.Succeeded()) {
                for (const std::string& error : result.errors) {
                    context.ReportError(error);
                }
                return;
            }
            StoreOutputs(context, instruction, signature, result);
        },
    };
}

[[nodiscard]] kb::visual::VisualGraphNativeBinding NativeBinding(const ScriptFunctionSignature& signature) {
    return kb::visual::VisualGraphNativeBinding{
        .opcode = kb::visual::VisualGraphIrOpcode::CallNative,
        .symbol = FunctionSymbol(signature.name),
        .functionName = "context.CallFunction",
        .inputs = ToGraphPins(signature.inputs),
        .outputs = ToGraphPins(signature.outputs),
        .passContext = false,
        .callScriptFunction = true,
    };
}

} // namespace

bool ScriptFunctionVisualGraphBindings::Register(
    kb::visual::VisualGraphRuntimeBindingRegistry& registry,
    const ScriptFunctionRegistry& functions,
    kb::scene::Scene& scene) {
    bool registered = true;
    for (const ScriptFunctionDesc& function : functions.Functions()) {
        registered = registry.Register(RuntimeBinding(functions, function.signature, scene)) && registered;
    }
    return registered;
}

bool ScriptFunctionVisualGraphBindings::RegisterNative(
    kb::visual::VisualGraphNativeBindingRegistry& registry,
    const ScriptFunctionRegistry& functions) {
    bool registered = true;
    for (const ScriptFunctionDesc& function : functions.Functions()) {
        registered = registry.Register(NativeBinding(function.signature)) && registered;
    }
    return registered;
}

bool ScriptFunctionVisualGraphBindings::RegisterFunction(
    kb::visual::VisualGraphRuntimeBindingRegistry& runtimeBindings,
    kb::visual::VisualGraphNativeBindingRegistry& nativeBindings,
    const ScriptFunctionRegistry& functions,
    const ScriptFunctionSignature& signature,
    kb::scene::Scene& scene) {
    return runtimeBindings.Register(RuntimeBinding(functions, signature, scene)) && nativeBindings.Register(NativeBinding(signature));
}

} // namespace kb::script
