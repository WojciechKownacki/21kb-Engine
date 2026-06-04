#include "engine/script/ScriptSharedVisualGraphBindings.hpp"

#include <array>
#include <optional>
#include <string_view>
#include <utility>

namespace kb::script {
namespace {

struct SharedBindingType final {
    std::string_view suffix;
    ScriptValueType scriptType = ScriptValueType::Void;
    kb::visual::VisualGraphValueType graphType = kb::visual::VisualGraphValueType::Void;
};

constexpr std::array kSharedBindingTypes{
    SharedBindingType{ .suffix = "Bool", .scriptType = ScriptValueType::Bool, .graphType = kb::visual::VisualGraphValueType::Bool },
    SharedBindingType{ .suffix = "Int", .scriptType = ScriptValueType::Int, .graphType = kb::visual::VisualGraphValueType::Int },
    SharedBindingType{ .suffix = "Float", .scriptType = ScriptValueType::Float, .graphType = kb::visual::VisualGraphValueType::Float },
    SharedBindingType{ .suffix = "String", .scriptType = ScriptValueType::String, .graphType = kb::visual::VisualGraphValueType::String },
    SharedBindingType{ .suffix = "Entity", .scriptType = ScriptValueType::Entity, .graphType = kb::visual::VisualGraphValueType::Entity },
    SharedBindingType{ .suffix = "Component", .scriptType = ScriptValueType::Component, .graphType = kb::visual::VisualGraphValueType::Component },
};

[[nodiscard]] std::string TypedSymbol(std::string_view base, std::string_view suffix) {
    std::string symbol{base};
    symbol.push_back('.');
    symbol.append(suffix);
    return symbol;
}

[[nodiscard]] const kb::visual::VisualGraphIrInput* FindInput(const kb::visual::VisualGraphIrInstruction& instruction, std::string_view name) noexcept {
    for (const kb::visual::VisualGraphIrInput& input : instruction.inputs) {
        if (input.name == name) {
            return &input;
        }
    }
    return nullptr;
}

[[nodiscard]] std::string ReadKey(kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInstruction& instruction) {
    const kb::visual::VisualGraphIrInput* keyInput = FindInput(instruction, "key");
    return keyInput == nullptr ? std::string{} : context.ReadString(keyInput->sourceNodeId, keyInput->sourcePin);
}

[[nodiscard]] ScriptValue ReadScriptValue(
    kb::visual::VisualGraphRuntimeExecutionContext& context,
    const kb::visual::VisualGraphIrInput& input,
    ScriptValueType type) {
    switch (type) {
    case ScriptValueType::Bool:
        return ScriptValue{ context.ReadBool(input.sourceNodeId, input.sourcePin) };
    case ScriptValueType::Int:
        return ScriptValue{ context.ReadInt(input.sourceNodeId, input.sourcePin) };
    case ScriptValueType::Float:
        return ScriptValue{ context.ReadFloat(input.sourceNodeId, input.sourcePin) };
    case ScriptValueType::String:
        return ScriptValue{ context.ReadString(input.sourceNodeId, input.sourcePin) };
    case ScriptValueType::Entity:
    case ScriptValueType::Component:
        return ScriptValue{ context.ReadUInt64(input.sourceNodeId, input.sourcePin), type };
    case ScriptValueType::Void:
        break;
    }
    return ScriptValue{};
}

[[nodiscard]] kb::visual::VisualGraphRuntimeBinding HasBinding(ScriptSharedState& sharedState) {
    return kb::visual::VisualGraphRuntimeBinding{
        .opcode = kb::visual::VisualGraphIrOpcode::GetProperty,
        .symbol = "Shared.Has",
        .inputs = {
            kb::visual::VisualGraphPinSignature{ .name = "key", .type = kb::visual::VisualGraphValueType::String },
        },
        .outputs = {
            kb::visual::VisualGraphPinSignature{ .name = "value", .type = kb::visual::VisualGraphValueType::Bool },
        },
        .callback = [&sharedState](kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInstruction& instruction) {
            context.Store(instruction.sourceNodeId, "value", kb::visual::VisualGraphRuntimeValue{ sharedState.Has(ReadKey(context, instruction)) });
        },
    };
}

[[nodiscard]] kb::visual::VisualGraphRuntimeBinding RemoveBinding(ScriptSharedState& sharedState) {
    return kb::visual::VisualGraphRuntimeBinding{
        .opcode = kb::visual::VisualGraphIrOpcode::SetProperty,
        .symbol = "Shared.Remove",
        .inputs = {
            kb::visual::VisualGraphPinSignature{ .name = "key", .type = kb::visual::VisualGraphValueType::String },
        },
        .outputs = {
            kb::visual::VisualGraphPinSignature{ .name = "succeeded", .type = kb::visual::VisualGraphValueType::Bool },
        },
        .callback = [&sharedState](kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInstruction& instruction) {
            context.Store(instruction.sourceNodeId, "succeeded", kb::visual::VisualGraphRuntimeValue{ sharedState.Remove(ReadKey(context, instruction)) });
        },
    };
}

[[nodiscard]] kb::visual::VisualGraphRuntimeBinding GetBinding(ScriptSharedState& sharedState, std::string symbol, const SharedBindingType& type) {
    return kb::visual::VisualGraphRuntimeBinding{
        .opcode = kb::visual::VisualGraphIrOpcode::GetProperty,
        .symbol = std::move(symbol),
        .inputs = {
            kb::visual::VisualGraphPinSignature{ .name = "key", .type = kb::visual::VisualGraphValueType::String },
        },
        .outputs = {
            kb::visual::VisualGraphPinSignature{ .name = "value", .type = type.graphType },
        },
        .callback = [&sharedState, type](kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInstruction& instruction) {
            const std::optional<ScriptValue> value = sharedState.Get(ReadKey(context, instruction));
            if (value.has_value() && value->Type() == type.scriptType) {
                context.Store(instruction.sourceNodeId, "value", value->ToVisualGraphValue());
            }
        },
    };
}

[[nodiscard]] kb::visual::VisualGraphRuntimeBinding SetBinding(ScriptSharedState& sharedState, std::string symbol, const SharedBindingType& type) {
    return kb::visual::VisualGraphRuntimeBinding{
        .opcode = kb::visual::VisualGraphIrOpcode::SetProperty,
        .symbol = std::move(symbol),
        .inputs = {
            kb::visual::VisualGraphPinSignature{ .name = "key", .type = kb::visual::VisualGraphValueType::String },
            kb::visual::VisualGraphPinSignature{ .name = "value", .type = type.graphType },
        },
        .outputs = {
            kb::visual::VisualGraphPinSignature{ .name = "succeeded", .type = kb::visual::VisualGraphValueType::Bool },
        },
        .callback = [&sharedState, type](kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInstruction& instruction) {
            const kb::visual::VisualGraphIrInput* valueInput = FindInput(instruction, "value");
            const bool succeeded = valueInput != nullptr && sharedState.Set(ReadKey(context, instruction), ReadScriptValue(context, *valueInput, type.scriptType));
            context.Store(instruction.sourceNodeId, "succeeded", kb::visual::VisualGraphRuntimeValue{ succeeded });
        },
    };
}

[[nodiscard]] kb::visual::VisualGraphNativeBinding NativeHasBinding() {
    return kb::visual::VisualGraphNativeBinding{
        .opcode = kb::visual::VisualGraphIrOpcode::GetProperty,
        .symbol = "Shared.Has",
        .functionName = "context.SharedHas",
        .inputs = {
            kb::visual::VisualGraphPinSignature{ .name = "key", .type = kb::visual::VisualGraphValueType::String },
        },
        .outputs = {
            kb::visual::VisualGraphPinSignature{ .name = "value", .type = kb::visual::VisualGraphValueType::Bool },
        },
        .passContext = false,
    };
}

[[nodiscard]] kb::visual::VisualGraphNativeBinding NativeRemoveBinding() {
    return kb::visual::VisualGraphNativeBinding{
        .opcode = kb::visual::VisualGraphIrOpcode::SetProperty,
        .symbol = "Shared.Remove",
        .functionName = "context.SharedRemove",
        .inputs = {
            kb::visual::VisualGraphPinSignature{ .name = "key", .type = kb::visual::VisualGraphValueType::String },
        },
        .outputs = {
            kb::visual::VisualGraphPinSignature{ .name = "succeeded", .type = kb::visual::VisualGraphValueType::Bool },
        },
        .passContext = false,
    };
}

[[nodiscard]] kb::visual::VisualGraphNativeBinding NativeGetBinding(std::string symbol, std::string functionName, const SharedBindingType& type) {
    return kb::visual::VisualGraphNativeBinding{
        .opcode = kb::visual::VisualGraphIrOpcode::GetProperty,
        .symbol = std::move(symbol),
        .functionName = std::move(functionName),
        .inputs = {
            kb::visual::VisualGraphPinSignature{ .name = "key", .type = kb::visual::VisualGraphValueType::String },
        },
        .outputs = {
            kb::visual::VisualGraphPinSignature{ .name = "value", .type = type.graphType },
        },
        .passContext = false,
    };
}

[[nodiscard]] kb::visual::VisualGraphNativeBinding NativeSetBinding(std::string symbol, std::string functionName, const SharedBindingType& type) {
    return kb::visual::VisualGraphNativeBinding{
        .opcode = kb::visual::VisualGraphIrOpcode::SetProperty,
        .symbol = std::move(symbol),
        .functionName = std::move(functionName),
        .inputs = {
            kb::visual::VisualGraphPinSignature{ .name = "key", .type = kb::visual::VisualGraphValueType::String },
            kb::visual::VisualGraphPinSignature{ .name = "value", .type = type.graphType },
        },
        .outputs = {
            kb::visual::VisualGraphPinSignature{ .name = "succeeded", .type = kb::visual::VisualGraphValueType::Bool },
        },
        .passContext = false,
    };
}

} // namespace

bool ScriptSharedVisualGraphBindings::Register(kb::visual::VisualGraphRuntimeBindingRegistry& registry, ScriptSharedState& sharedState) {
    bool registered = true;
    registered = registry.Register(HasBinding(sharedState)) && registered;
    registered = registry.Register(RemoveBinding(sharedState)) && registered;
    for (const SharedBindingType& type : kSharedBindingTypes) {
        registered = registry.Register(GetBinding(sharedState, TypedSymbol("Shared.Get", type.suffix), type)) && registered;
        registered = registry.Register(SetBinding(sharedState, TypedSymbol("Shared.Set", type.suffix), type)) && registered;
    }
    return registered;
}

bool ScriptSharedVisualGraphBindings::RegisterNative(kb::visual::VisualGraphNativeBindingRegistry& registry) {
    bool registered = true;
    registered = registry.Register(NativeHasBinding()) && registered;
    registered = registry.Register(NativeRemoveBinding()) && registered;
    for (const SharedBindingType& type : kSharedBindingTypes) {
        registered = registry.Register(NativeGetBinding(TypedSymbol("Shared.Get", type.suffix), "context.SharedGet" + std::string{ type.suffix }, type)) && registered;
        registered = registry.Register(NativeSetBinding(TypedSymbol("Shared.Set", type.suffix), "context.SharedSet" + std::string{ type.suffix }, type)) && registered;
    }
    return registered;
}

} // namespace kb::script
