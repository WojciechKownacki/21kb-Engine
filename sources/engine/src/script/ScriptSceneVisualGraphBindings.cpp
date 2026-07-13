#include "engine/script/ScriptSceneVisualGraphBindings.hpp"

#include "engine/script/ScriptSceneComponentApi.hpp"
#include "engine/script/ScriptValue.hpp"
#include "engine/visual/VisualGraphCompiler.hpp"

#include <array>
#include <string_view>
#include <utility>

namespace kb::script {
namespace {

struct PropertyBindingType final {
    std::string_view suffix;
    kb::visual::VisualGraphValueType graphType = kb::visual::VisualGraphValueType::Void;
};

constexpr std::array kPropertyBindingTypes{
    PropertyBindingType{ .suffix = "Bool", .graphType = kb::visual::VisualGraphValueType::Bool },
    PropertyBindingType{ .suffix = "Int", .graphType = kb::visual::VisualGraphValueType::Int },
    PropertyBindingType{ .suffix = "Float", .graphType = kb::visual::VisualGraphValueType::Float },
    PropertyBindingType{ .suffix = "String", .graphType = kb::visual::VisualGraphValueType::String },
    PropertyBindingType{ .suffix = "Entity", .graphType = kb::visual::VisualGraphValueType::Entity },
    PropertyBindingType{ .suffix = "Component", .graphType = kb::visual::VisualGraphValueType::Component },
};

[[nodiscard]] std::string TypedSymbol(std::string_view base, std::string_view suffix) {
    std::string symbol{base};
    symbol.push_back('.');
    symbol.append(suffix);
    return symbol;
}

[[nodiscard]] kb::visual::VisualGraphRuntimeBinding GetPropertyBinding(
    kb::scene::Scene& scene,
    std::string symbol,
    kb::visual::VisualGraphValueType valueType);

[[nodiscard]] kb::visual::VisualGraphRuntimeBinding SetPropertyBinding(
    kb::scene::Scene& scene,
    std::string symbol,
    kb::visual::VisualGraphValueType valueType);
[[nodiscard]] kb::visual::VisualGraphNativeBinding NativeGetPropertyBinding(
    std::string symbol,
    std::string functionName,
    kb::visual::VisualGraphValueType valueType);
[[nodiscard]] kb::visual::VisualGraphNativeBinding NativeSetPropertyBinding(
    std::string symbol,
    std::string functionName,
    kb::visual::VisualGraphValueType valueType);

[[nodiscard]] kb::scene::SceneEntity ReadSelf(kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInstruction& instruction) noexcept {
    for (const kb::visual::VisualGraphIrInput& input : instruction.inputs) {
        if (input.name == "self") {
            return kb::scene::SceneEntity{ context.ReadUInt64(input.sourceNodeId, input.sourcePin) };
        }
    }
    return kb::scene::SceneEntity{ context.ReadUInt64(0U, "self") };
}

[[nodiscard]] ScriptValue ReadScriptValue(kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInput& input) {
    switch (input.type) {
    case kb::visual::VisualGraphValueType::Bool:
        return ScriptValue{ context.ReadBool(input.sourceNodeId, input.sourcePin) };
    case kb::visual::VisualGraphValueType::Int:
        return ScriptValue{ context.ReadInt(input.sourceNodeId, input.sourcePin) };
    case kb::visual::VisualGraphValueType::Float:
        return ScriptValue{ context.ReadFloat(input.sourceNodeId, input.sourcePin) };
    case kb::visual::VisualGraphValueType::String:
        return ScriptValue{ context.ReadString(input.sourceNodeId, input.sourcePin) };
    case kb::visual::VisualGraphValueType::Entity:
        return ScriptValue{ context.ReadUInt64(input.sourceNodeId, input.sourcePin), ScriptValueType::Entity };
    case kb::visual::VisualGraphValueType::Component:
        return ScriptValue{ context.ReadUInt64(input.sourceNodeId, input.sourcePin), ScriptValueType::Component };
    case kb::visual::VisualGraphValueType::UInt32:
        return ScriptValue{ static_cast<std::uint32_t>(context.ReadUInt64(input.sourceNodeId, input.sourcePin)) };
    case kb::visual::VisualGraphValueType::Hash:
        return ScriptValue{ context.ReadUInt64(input.sourceNodeId, input.sourcePin), ScriptValueType::Hash };
    case kb::visual::VisualGraphValueType::Int64:
        return ScriptValue{ context.ReadInt64(input.sourceNodeId, input.sourcePin) };
    case kb::visual::VisualGraphValueType::Double:
        return ScriptValue{ context.ReadDouble(input.sourceNodeId, input.sourcePin) };
    case kb::visual::VisualGraphValueType::Name:
        return ScriptValue{ context.ReadString(input.sourceNodeId, input.sourcePin), ScriptValueType::Name };
    case kb::visual::VisualGraphValueType::Guid:
        return ScriptValue{ context.ReadString(input.sourceNodeId, input.sourcePin), ScriptValueType::Guid };
    case kb::visual::VisualGraphValueType::Void:
        break;
    }
    return ScriptValue{};
}

[[nodiscard]] const kb::visual::VisualGraphIrInput* FindInput(const kb::visual::VisualGraphIrInstruction& instruction, std::string_view name) noexcept {
    for (const kb::visual::VisualGraphIrInput& input : instruction.inputs) {
        if (input.name == name) {
            return &input;
        }
    }
    return nullptr;
}

void StoreOutput(
    kb::visual::VisualGraphRuntimeExecutionContext& context,
    const kb::visual::VisualGraphIrInstruction& instruction,
    const ScriptValue& value) {
    for (const kb::visual::VisualGraphIrOutput& output : instruction.outputs) {
        if (output.name == "value") {
            context.Store(instruction.sourceNodeId, output.name, value.ToVisualGraphValue());
            return;
        }
    }
}

[[nodiscard]] kb::visual::VisualGraphRuntimeBinding HasComponentBinding(kb::scene::Scene& scene) {
    return kb::visual::VisualGraphRuntimeBinding{
        .opcode = kb::visual::VisualGraphIrOpcode::GetComponent,
        .symbol = "Self.HasComponent",
        .inputs = {
            kb::visual::VisualGraphPinSignature{ .name = "component", .type = kb::visual::VisualGraphValueType::String },
            kb::visual::VisualGraphPinSignature{ .name = "self", .type = kb::visual::VisualGraphValueType::Entity, .required = false },
        },
        .outputs = {
            kb::visual::VisualGraphPinSignature{ .name = "value", .type = kb::visual::VisualGraphValueType::Bool },
        },
        .callback = [&scene](kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInstruction& instruction) {
            const kb::visual::VisualGraphIrInput* componentInput = FindInput(instruction, "component");
            const std::string componentName = componentInput == nullptr ? std::string{} : context.ReadString(componentInput->sourceNodeId, componentInput->sourcePin);
            context.Store(instruction.sourceNodeId, "value", kb::visual::VisualGraphRuntimeValue{ ScriptSceneComponentApi::HasComponent(scene, ReadSelf(context, instruction), componentName) });
        },
    };
}

[[nodiscard]] kb::visual::VisualGraphNativeBinding NativeHasComponentBinding() {
    return kb::visual::VisualGraphNativeBinding{
        .opcode = kb::visual::VisualGraphIrOpcode::GetComponent,
        .symbol = "Self.HasComponent",
        .functionName = "context.SelfHasComponent",
        .inputs = {
            kb::visual::VisualGraphPinSignature{ .name = "component", .type = kb::visual::VisualGraphValueType::String },
        },
        .outputs = {
            kb::visual::VisualGraphPinSignature{ .name = "value", .type = kb::visual::VisualGraphValueType::Bool },
        },
        .passContext = false,
    };
}

[[nodiscard]] kb::visual::VisualGraphRuntimeBinding GetPropertyBinding(kb::scene::Scene& scene) {
    return GetPropertyBinding(scene, "Self.GetProperty", kb::visual::VisualGraphValueType::Float);
}

[[nodiscard]] kb::visual::VisualGraphRuntimeBinding GetPropertyBinding(
    kb::scene::Scene& scene,
    std::string symbol,
    kb::visual::VisualGraphValueType valueType) {
    return kb::visual::VisualGraphRuntimeBinding{
        .opcode = kb::visual::VisualGraphIrOpcode::GetProperty,
        .symbol = std::move(symbol),
        .inputs = {
            kb::visual::VisualGraphPinSignature{ .name = "component", .type = kb::visual::VisualGraphValueType::String },
            kb::visual::VisualGraphPinSignature{ .name = "property", .type = kb::visual::VisualGraphValueType::String },
            kb::visual::VisualGraphPinSignature{ .name = "self", .type = kb::visual::VisualGraphValueType::Entity, .required = false },
        },
        .outputs = {
            kb::visual::VisualGraphPinSignature{ .name = "value", .type = valueType },
        },
        .callback = [&scene](kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInstruction& instruction) {
            const kb::visual::VisualGraphIrInput* componentInput = FindInput(instruction, "component");
            const kb::visual::VisualGraphIrInput* propertyInput = FindInput(instruction, "property");
            const std::string componentName = componentInput == nullptr ? std::string{} : context.ReadString(componentInput->sourceNodeId, componentInput->sourcePin);
            const std::string propertyName = propertyInput == nullptr ? std::string{} : context.ReadString(propertyInput->sourceNodeId, propertyInput->sourcePin);
            const ScriptSceneComponentPropertyResult result = ScriptSceneComponentApi::GetProperty(scene, ReadSelf(context, instruction), componentName, propertyName);
            if (result.succeeded) {
                StoreOutput(context, instruction, result.value);
            }
        },
    };
}

[[nodiscard]] kb::visual::VisualGraphRuntimeBinding SetPropertyBinding(kb::scene::Scene& scene) {
    return SetPropertyBinding(scene, "Self.SetProperty", kb::visual::VisualGraphValueType::Float);
}

[[nodiscard]] kb::visual::VisualGraphRuntimeBinding SetPropertyBinding(
    kb::scene::Scene& scene,
    std::string symbol,
    kb::visual::VisualGraphValueType valueType) {
    return kb::visual::VisualGraphRuntimeBinding{
        .opcode = kb::visual::VisualGraphIrOpcode::SetProperty,
        .symbol = std::move(symbol),
        .inputs = {
            kb::visual::VisualGraphPinSignature{ .name = "component", .type = kb::visual::VisualGraphValueType::String },
            kb::visual::VisualGraphPinSignature{ .name = "property", .type = kb::visual::VisualGraphValueType::String },
            kb::visual::VisualGraphPinSignature{ .name = "value", .type = valueType },
            kb::visual::VisualGraphPinSignature{ .name = "self", .type = kb::visual::VisualGraphValueType::Entity, .required = false },
        },
        .outputs = {
            kb::visual::VisualGraphPinSignature{ .name = "succeeded", .type = kb::visual::VisualGraphValueType::Bool },
        },
        .callback = [&scene](kb::visual::VisualGraphRuntimeExecutionContext& context, const kb::visual::VisualGraphIrInstruction& instruction) {
            const kb::visual::VisualGraphIrInput* componentInput = FindInput(instruction, "component");
            const kb::visual::VisualGraphIrInput* propertyInput = FindInput(instruction, "property");
            const kb::visual::VisualGraphIrInput* valueInput = FindInput(instruction, "value");
            const std::string componentName = componentInput == nullptr ? std::string{} : context.ReadString(componentInput->sourceNodeId, componentInput->sourcePin);
            const std::string propertyName = propertyInput == nullptr ? std::string{} : context.ReadString(propertyInput->sourceNodeId, propertyInput->sourcePin);
            const ScriptValue value = valueInput == nullptr ? ScriptValue{} : ReadScriptValue(context, *valueInput);
            const ScriptSceneComponentMutationResult result = ScriptSceneComponentApi::SetProperty(scene, ReadSelf(context, instruction), componentName, propertyName, value);
            context.Store(instruction.sourceNodeId, "succeeded", kb::visual::VisualGraphRuntimeValue{ result.succeeded });
        },
    };
}

[[nodiscard]] kb::visual::VisualGraphNativeBinding NativeGetPropertyBinding(
    std::string symbol,
    std::string functionName,
    kb::visual::VisualGraphValueType valueType) {
    return kb::visual::VisualGraphNativeBinding{
        .opcode = kb::visual::VisualGraphIrOpcode::GetProperty,
        .symbol = std::move(symbol),
        .functionName = std::move(functionName),
        .inputs = {
            kb::visual::VisualGraphPinSignature{ .name = "component", .type = kb::visual::VisualGraphValueType::String },
            kb::visual::VisualGraphPinSignature{ .name = "property", .type = kb::visual::VisualGraphValueType::String },
        },
        .outputs = {
            kb::visual::VisualGraphPinSignature{ .name = "value", .type = valueType },
        },
        .passContext = false,
    };
}

[[nodiscard]] kb::visual::VisualGraphNativeBinding NativeSetPropertyBinding(
    std::string symbol,
    std::string functionName,
    kb::visual::VisualGraphValueType valueType) {
    return kb::visual::VisualGraphNativeBinding{
        .opcode = kb::visual::VisualGraphIrOpcode::SetProperty,
        .symbol = std::move(symbol),
        .functionName = std::move(functionName),
        .inputs = {
            kb::visual::VisualGraphPinSignature{ .name = "component", .type = kb::visual::VisualGraphValueType::String },
            kb::visual::VisualGraphPinSignature{ .name = "property", .type = kb::visual::VisualGraphValueType::String },
            kb::visual::VisualGraphPinSignature{ .name = "value", .type = valueType },
        },
        .outputs = {
            kb::visual::VisualGraphPinSignature{ .name = "succeeded", .type = kb::visual::VisualGraphValueType::Bool },
        },
        .passContext = false,
    };
}

} // namespace

bool ScriptSceneVisualGraphBindings::Register(kb::visual::VisualGraphRuntimeBindingRegistry& registry, kb::scene::Scene& scene) {
    bool registered = true;
    registered = registry.Register(HasComponentBinding(scene)) && registered;
    registered = registry.Register(GetPropertyBinding(scene)) && registered;
    registered = registry.Register(SetPropertyBinding(scene)) && registered;
    for (const PropertyBindingType& type : kPropertyBindingTypes) {
        registered = registry.Register(GetPropertyBinding(scene, TypedSymbol("Self.GetProperty", type.suffix), type.graphType)) && registered;
        registered = registry.Register(SetPropertyBinding(scene, TypedSymbol("Self.SetProperty", type.suffix), type.graphType)) && registered;
    }
    return registered;
}

bool ScriptSceneVisualGraphBindings::RegisterNative(kb::visual::VisualGraphNativeBindingRegistry& registry) {
    bool registered = true;
    registered = registry.Register(NativeHasComponentBinding()) && registered;
    registered = registry.Register(NativeGetPropertyBinding(
                     "Self.GetProperty",
                     "context.SelfGetPropertyFloat",
                     kb::visual::VisualGraphValueType::Float)) &&
                 registered;
    registered = registry.Register(NativeSetPropertyBinding(
                     "Self.SetProperty",
                     "context.SelfSetPropertyFloat",
                     kb::visual::VisualGraphValueType::Float)) &&
                 registered;
    for (const PropertyBindingType& type : kPropertyBindingTypes) {
        registered = registry.Register(NativeGetPropertyBinding(
                         TypedSymbol("Self.GetProperty", type.suffix),
                         "context.SelfGetProperty" + std::string{ type.suffix },
                         type.graphType)) &&
                     registered;
        registered = registry.Register(NativeSetPropertyBinding(
                         TypedSymbol("Self.SetProperty", type.suffix),
                         "context.SelfSetProperty" + std::string{ type.suffix },
                         type.graphType)) &&
                     registered;
    }
    return registered;
}

} // namespace kb::script
