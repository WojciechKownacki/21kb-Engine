#include "engine/visual/VisualGraphNativeCodeGenerator.hpp"

#include <cctype>
#include <ranges>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace kb::visual {
namespace {

[[nodiscard]] bool IsCppKeyword(std::string_view text) noexcept {
    static constexpr std::string_view kKeywords[] = {
        "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor", "bool", "break", "case", "catch", "char", "char8_t", "char16_t",
        "char32_t", "class", "compl", "concept", "const", "consteval", "constexpr", "constinit", "const_cast", "continue", "co_await", "co_return",
        "co_yield", "decltype", "default", "delete", "do", "double", "dynamic_cast", "else", "enum", "explicit", "export", "extern", "false", "float",
        "for", "friend", "goto", "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept", "not", "not_eq", "nullptr", "operator",
        "or", "or_eq", "private", "protected", "public", "register", "reinterpret_cast", "requires", "return", "short", "signed", "sizeof", "static",
        "static_assert", "static_cast", "struct", "switch", "template", "this", "thread_local", "throw", "true", "try", "typedef", "typeid", "typename",
        "union", "unsigned", "using", "virtual", "void", "volatile", "wchar_t", "while", "xor", "xor_eq",
    };
    for (const std::string_view keyword : kKeywords) {
        if (keyword == text) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool IsIdentifierStart(char ch) noexcept {
    const unsigned char value = static_cast<unsigned char>(ch);
    return std::isalpha(value) != 0 || ch == '_';
}

[[nodiscard]] bool IsIdentifierContinue(char ch) noexcept {
    const unsigned char value = static_cast<unsigned char>(ch);
    return std::isalnum(value) != 0 || ch == '_';
}

[[nodiscard]] bool IsValidIdentifier(std::string_view text) noexcept {
    if (text.empty() || !IsIdentifierStart(text.front()) || IsCppKeyword(text)) {
        return false;
    }
    for (const char ch : text.substr(1U)) {
        if (!IsIdentifierContinue(ch)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool IsValidNamespace(std::string_view text) noexcept {
    if (text.empty()) {
        return true;
    }

    std::size_t offset = 0U;
    while (offset < text.size()) {
        const std::size_t separator = text.find("::", offset);
        const std::string_view part = separator == std::string_view::npos ? text.substr(offset) : text.substr(offset, separator - offset);
        if (!IsValidIdentifier(part)) {
            return false;
        }
        if (separator == std::string_view::npos) {
            return true;
        }
        offset = separator + 2U;
    }
    return false;
}

[[nodiscard]] bool IsValidIncludePath(std::string_view text) noexcept {
    if (text.empty()) {
        return false;
    }
    for (const char ch : text) {
        if (ch == '\n' || ch == '\r' || ch == '"' || ch == '\\') {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string SanitizedIdentifier(std::string_view text, std::string_view fallback) {
    std::string output;
    output.reserve(text.size());
    for (char ch : text) {
        const unsigned char value = static_cast<unsigned char>(ch);
        output.push_back(std::isalnum(value) != 0 ? ch : '_');
    }
    if (output.empty() || std::isdigit(static_cast<unsigned char>(output.front())) != 0) {
        output.insert(0, fallback);
    }
    if (IsCppKeyword(output)) {
        output.push_back('_');
    }
    return output;
}

void WriteNamespaceOpen(std::ostringstream& stream, std::string_view namespaceName) {
    if (namespaceName.empty()) {
        return;
    }
    stream << "namespace " << namespaceName << " {\n\n";
}

void WriteNamespaceClose(std::ostringstream& stream, std::string_view namespaceName) {
    if (namespaceName.empty()) {
        return;
    }
    stream << "} // namespace " << namespaceName << "\n";
}

void AddCodegenError(std::vector<std::string>& errors, std::vector<VisualGraphDiagnostic>& diagnostics, std::uint32_t nodeId, std::string message) {
    errors.push_back(message);
    diagnostics.push_back(VisualGraphDiagnostics::Error(VisualGraphDiagnosticStage::NativeCodegen, nodeId, std::move(message)));
}

void WriteSourceIncludes(std::ostringstream& stream, const std::vector<std::string>& sourceIncludes, std::vector<std::string>& errors, std::vector<VisualGraphDiagnostic>& diagnostics) {
    for (const std::string& includePath : sourceIncludes) {
        if (!IsValidIncludePath(includePath)) {
            AddCodegenError(errors, diagnostics, 0U, "visual graph native code include path is invalid");
            continue;
        }
        stream << "#include \"" << includePath << "\"\n";
    }
    if (!sourceIncludes.empty()) {
        stream << "\n";
    }
}

[[nodiscard]] bool IsCustomEventFunction(const VisualGraphIrFunction& function) noexcept {
    return !function.customEventName.empty();
}

[[nodiscard]] std::string FunctionName(const VisualGraphIrFunction& function) {
    if (IsCustomEventFunction(function)) {
        return "Event_" + SanitizedIdentifier(function.customEventName, "CustomEvent");
    }
    return ToString(function.event);
}

void WriteFunctionDecl(std::ostringstream& stream, const VisualGraphIrFunction& function) {
    stream << "    void " << FunctionName(function) << "(VisualGraphNativeExecutionContext& context);\n";
}

[[nodiscard]] std::string NodeFunctionName(std::string_view prefix, const VisualGraphIrFunction& function, std::uint32_t nodeId) {
    return std::string{prefix} + "_" + FunctionName(function) + "_" + std::to_string(nodeId);
}

[[nodiscard]] const VisualGraphIrInstruction* FindInstruction(const VisualGraphIrFunction& function, std::uint32_t nodeId) noexcept {
    const auto iter = std::ranges::find_if(function.instructions, [nodeId](const VisualGraphIrInstruction& instruction) {
        return instruction.sourceNodeId == nodeId;
    });
    return iter == function.instructions.end() ? nullptr : &*iter;
}

[[nodiscard]] const VisualGraphIrInput* FindInput(const VisualGraphIrInstruction& instruction, std::string_view name) noexcept {
    const auto iter = std::ranges::find_if(instruction.inputs, [name](const VisualGraphIrInput& input) {
        return input.name == name;
    });
    return iter == instruction.inputs.end() ? nullptr : &*iter;
}

[[nodiscard]] std::string ReadFunctionFor(VisualGraphValueType type) {
    switch (type) {
    case VisualGraphValueType::Bool:
        return "ReadBool";
    case VisualGraphValueType::Int:
        return "ReadInt";
    case VisualGraphValueType::Float:
        return "ReadFloat";
    case VisualGraphValueType::String:
        return "ReadString";
    case VisualGraphValueType::Entity:
        return "ReadEntity";
    case VisualGraphValueType::Component:
        return "ReadComponent";
    case VisualGraphValueType::Int64:
        return "ReadInt64";
    case VisualGraphValueType::UInt32:
        return "ReadUInt32";
    case VisualGraphValueType::Double:
        return "ReadDouble";
    case VisualGraphValueType::Name:
        return "ReadName";
    case VisualGraphValueType::Guid:
        return "ReadGuid";
    case VisualGraphValueType::Hash:
        return "ReadHash";
    case VisualGraphValueType::Void:
        break;
    }
    return "ReadVoid";
}

[[nodiscard]] std::string StoreFunctionFor(VisualGraphValueType type) {
    switch (type) {
    case VisualGraphValueType::Bool:
        return "StoreBool";
    case VisualGraphValueType::Int:
        return "StoreInt";
    case VisualGraphValueType::Float:
        return "StoreFloat";
    case VisualGraphValueType::String:
        return "StoreString";
    case VisualGraphValueType::Entity:
        return "StoreEntity";
    case VisualGraphValueType::Component:
        return "StoreComponent";
    case VisualGraphValueType::Int64:
        return "StoreInt64";
    case VisualGraphValueType::UInt32:
        return "StoreUInt32";
    case VisualGraphValueType::Double:
        return "StoreDouble";
    case VisualGraphValueType::Name:
        return "StoreName";
    case VisualGraphValueType::Guid:
        return "StoreGuid";
    case VisualGraphValueType::Hash:
        return "StoreHash";
    case VisualGraphValueType::Void:
        break;
    }
    return "StoreVoid";
}

[[nodiscard]] std::string ResultReadFunctionFor(VisualGraphValueType type) {
    switch (type) {
    case VisualGraphValueType::Bool:
        return "ReadBool";
    case VisualGraphValueType::Int:
        return "ReadInt";
    case VisualGraphValueType::Float:
        return "ReadFloat";
    case VisualGraphValueType::String:
        return "ReadString";
    case VisualGraphValueType::Entity:
        return "ReadEntity";
    case VisualGraphValueType::Component:
        return "ReadComponent";
    case VisualGraphValueType::Int64:
        return "ReadInt64";
    case VisualGraphValueType::UInt32:
        return "ReadUInt32";
    case VisualGraphValueType::Double:
        return "ReadDouble";
    case VisualGraphValueType::Name:
        return "ReadName";
    case VisualGraphValueType::Guid:
        return "ReadGuid";
    case VisualGraphValueType::Hash:
        return "ReadHash";
    case VisualGraphValueType::Void:
        break;
    }
    return "ReadVoid";
}

[[nodiscard]] std::string NativeEventValueTypeFor(VisualGraphValueType type) {
    switch (type) {
    case VisualGraphValueType::Bool:
        return "VisualGraphNativeValueType::Bool";
    case VisualGraphValueType::Int:
        return "VisualGraphNativeValueType::Int";
    case VisualGraphValueType::Float:
        return "VisualGraphNativeValueType::Float";
    case VisualGraphValueType::String:
        return "VisualGraphNativeValueType::String";
    case VisualGraphValueType::Entity:
        return "VisualGraphNativeValueType::Entity";
    case VisualGraphValueType::Component:
        return "VisualGraphNativeValueType::Component";
    case VisualGraphValueType::Int64:
        return "VisualGraphNativeValueType::Int64";
    case VisualGraphValueType::UInt32:
        return "VisualGraphNativeValueType::UInt32";
    case VisualGraphValueType::Double:
        return "VisualGraphNativeValueType::Double";
    case VisualGraphValueType::Name:
        return "VisualGraphNativeValueType::Name";
    case VisualGraphValueType::Guid:
        return "VisualGraphNativeValueType::Guid";
    case VisualGraphValueType::Hash:
        return "VisualGraphNativeValueType::Hash";
    case VisualGraphValueType::Void:
        break;
    }
    return "VisualGraphNativeValueType::Void";
}

[[nodiscard]] std::string CppStringLiteral(std::string_view text) {
    std::string output;
    output.reserve(text.size() + 2U);
    output.push_back('"');
    for (const char ch : text) {
        if (ch == '\\' || ch == '"') {
            output.push_back('\\');
            output.push_back(ch);
            continue;
        }
        if (ch == '\n') {
            output += "\\n";
            continue;
        }
        if (ch == '\r') {
            output += "\\r";
            continue;
        }
        if (ch == '\t') {
            output += "\\t";
            continue;
        }
        output.push_back(static_cast<unsigned char>(ch) < 0x20U ? '?' : ch);
    }
    output.push_back('"');
    return output;
}

void ValidateNativeBinding(
    const VisualGraphIrInstruction& instruction,
    const VisualGraphNativeBinding& binding,
    std::vector<std::string>& errors) {
    if (!binding.callScriptFunction && binding.statement.empty() && binding.outputs.size() > 1U) {
        errors.push_back("binding '" + binding.symbol + "' has multiple outputs and requires an explicit statement");
    }

    VisualGraphBindingSignatureValidator::Validate(binding.symbol, binding.inputs, binding.outputs, instruction, errors);
}

[[nodiscard]] std::string ReadInputExpression(const VisualGraphIrInput& input) {
    return "context." + ReadFunctionFor(input.type) + "(" + std::to_string(input.sourceNodeId) + "U, " + CppStringLiteral(input.sourcePin) + ")";
}

[[nodiscard]] bool IsEventPayloadInput(const VisualGraphIrInput& input) noexcept {
    return input.type != VisualGraphValueType::Void && input.name != "exec" && !(input.name == "target" && input.type == VisualGraphValueType::Entity);
}

[[nodiscard]] const VisualGraphIrInput* FindEventTargetInput(const VisualGraphIrInstruction& instruction) noexcept {
    for (const VisualGraphIrInput& input : instruction.inputs) {
        if (input.name == "target" && input.type == VisualGraphValueType::Entity) {
            return &input;
        }
    }
    return nullptr;
}

void WriteEmitEventCall(std::ostringstream& stream, const VisualGraphIrInstruction& instruction) {
    const std::string symbol = CppStringLiteral(instruction.symbol);
    const VisualGraphIrInput* targetInput = FindEventTargetInput(instruction);
    std::size_t payloadCount = 0U;
    for (const VisualGraphIrInput& input : instruction.inputs) {
        if (IsEventPayloadInput(input)) {
            ++payloadCount;
        }
    }

    if (payloadCount == 0U) {
        if (targetInput == nullptr) {
            stream << "    context.EmitEvent(" << symbol << ");\n";
        } else {
            stream << "    context.EmitEventTo(" << ReadInputExpression(*targetInput) << ", " << symbol << ");\n";
        }
        return;
    }

    const std::string argumentsName = "eventArguments_" + std::to_string(instruction.sourceNodeId);
    stream << "    const VisualGraphNativeEventArgument " << argumentsName << "[] = {\n";
    for (const VisualGraphIrInput& input : instruction.inputs) {
        if (!IsEventPayloadInput(input)) {
            continue;
        }
        stream << "        VisualGraphNativeEventArgument{"
               << CppStringLiteral(input.name) << ", "
               << NativeEventValueTypeFor(input.type) << ", "
               << ReadInputExpression(input) << "},\n";
    }
    stream << "    };\n";
    if (targetInput == nullptr) {
        stream << "    context.EmitEvent(" << symbol << ", std::span<const VisualGraphNativeEventArgument>{" << argumentsName << "});\n";
    } else {
        stream << "    context.EmitEventTo(" << ReadInputExpression(*targetInput) << ", " << symbol << ", std::span<const VisualGraphNativeEventArgument>{" << argumentsName << "});\n";
    }
}

[[nodiscard]] bool RequiresNativeBinding(VisualGraphIrOpcode opcode) noexcept {
    return opcode == VisualGraphIrOpcode::CallNative || opcode == VisualGraphIrOpcode::GetComponent || opcode == VisualGraphIrOpcode::GetProperty ||
           opcode == VisualGraphIrOpcode::SetProperty;
}

void WriteNativeFunctionArguments(std::ostringstream& stream, const VisualGraphIrInstruction& instruction, const VisualGraphNativeBinding& binding) {
    bool needsComma = false;
    if (binding.passContext) {
        stream << "context";
        needsComma = true;
    }
    for (const VisualGraphPinSignature& input : binding.inputs) {
        const VisualGraphIrInput* irInput = FindInput(instruction, input.name);
        if (irInput == nullptr) {
            continue;
        }
        if (needsComma) {
            stream << ", ";
        }
        stream << ReadInputExpression(*irInput);
        needsComma = true;
    }
}

void WriteScriptFunctionArgumentsArray(std::ostringstream& stream, const VisualGraphIrInstruction& instruction, const VisualGraphNativeBinding& binding, std::string_view argumentsName) {
    stream << "    const VisualGraphNativeEventArgument " << argumentsName << "[] = {\n";
    for (const VisualGraphPinSignature& input : binding.inputs) {
        const VisualGraphIrInput* irInput = FindInput(instruction, input.name);
        if (irInput == nullptr) {
            continue;
        }
        stream << "        VisualGraphNativeEventArgument{"
               << CppStringLiteral(input.name) << ", "
               << NativeEventValueTypeFor(input.type) << ", "
               << ReadInputExpression(*irInput) << "},\n";
    }
    stream << "    };\n";
}

[[nodiscard]] std::string ScriptFunctionNameFromBinding(const VisualGraphNativeBinding& binding) {
    static constexpr std::string_view kPrefix = "Function.";
    if (binding.symbol.starts_with(kPrefix)) {
        return std::string{binding.symbol.substr(kPrefix.size())};
    }
    return binding.symbol;
}

void WriteScriptFunctionBindingCall(std::ostringstream& stream, const VisualGraphIrInstruction& instruction, const VisualGraphNativeBinding& binding) {
    const std::string argumentsName = "functionArguments_" + std::to_string(instruction.sourceNodeId);
    const std::string functionName = CppStringLiteral(ScriptFunctionNameFromBinding(binding));
    if (binding.inputs.empty()) {
        if (binding.outputs.empty()) {
            stream << "    context.CallFunction(" << functionName << ", std::span<const VisualGraphNativeEventArgument>{});\n";
            return;
        }
        stream << "    const VisualGraphNativeFunctionResult functionResult_" << instruction.sourceNodeId << " = context.CallFunction(" << functionName
               << ", std::span<const VisualGraphNativeEventArgument>{});\n";
    } else {
        WriteScriptFunctionArgumentsArray(stream, instruction, binding, argumentsName);
        if (binding.outputs.empty()) {
            stream << "    context.CallFunction(" << functionName << ", std::span<const VisualGraphNativeEventArgument>{" << argumentsName << "});\n";
            return;
        }
        stream << "    const VisualGraphNativeFunctionResult functionResult_" << instruction.sourceNodeId << " = context.CallFunction(" << functionName
               << ", std::span<const VisualGraphNativeEventArgument>{" << argumentsName << "});\n";
    }

    for (const VisualGraphPinSignature& output : binding.outputs) {
        stream << "    context." << StoreFunctionFor(output.type) << "(" << instruction.sourceNodeId << "U, " << CppStringLiteral(output.name) << ", functionResult_"
               << instruction.sourceNodeId << "." << ResultReadFunctionFor(output.type) << "(" << CppStringLiteral(output.name) << "));\n";
    }
}

void WriteNativeBindingCall(std::ostringstream& stream, const VisualGraphIrInstruction& instruction, const VisualGraphNativeBinding& binding) {
    if (binding.callScriptFunction) {
        WriteScriptFunctionBindingCall(stream, instruction, binding);
        return;
    }
    if (!binding.statement.empty()) {
        stream << "    " << binding.statement << "\n";
        return;
    }

    if (binding.outputs.size() == 1U && binding.outputs[0].type != VisualGraphValueType::Void) {
        const VisualGraphPinSignature& output = binding.outputs[0];
        stream << "    context." << StoreFunctionFor(output.type) << "(" << instruction.sourceNodeId << "U, " << CppStringLiteral(output.name) << ", " << binding.functionName << "(";
        WriteNativeFunctionArguments(stream, instruction, binding);
        stream << "));\n";
        return;
    }

    stream << "    " << binding.functionName << "(";
    WriteNativeFunctionArguments(stream, instruction, binding);
    stream << ");\n";
}

void WriteInstructionOperation(
    std::ostringstream& stream,
    const VisualGraphIrInstruction& instruction,
    const VisualGraphNativeBindingRegistry* bindings,
    bool requireNativeBindings,
    std::vector<std::string>& errors,
    std::vector<VisualGraphDiagnostic>& diagnostics) {
    stream << "    // node " << instruction.sourceNodeId << ": " << ToString(instruction.opcode);
    if (!instruction.symbol.empty()) {
        stream << " " << instruction.symbol;
    }
    stream << "\n";

    if (bindings != nullptr) {
        if (const VisualGraphNativeBinding* binding = bindings->Find(instruction.opcode, instruction.symbol); binding != nullptr) {
            const std::size_t errorCount = errors.size();
            ValidateNativeBinding(instruction, *binding, errors);
            if (errors.size() == errorCount) {
                WriteNativeBindingCall(stream, instruction, *binding);
            } else {
                for (std::size_t index = errorCount; index < errors.size(); ++index) {
                    diagnostics.push_back(VisualGraphDiagnostics::Error(VisualGraphDiagnosticStage::NativeCodegen, instruction.sourceNodeId, errors[index]));
                }
                stream << "    // native binding signature mismatch for " << instruction.symbol << "\n";
            }
            return;
        }
    }

    if (requireNativeBindings && RequiresNativeBinding(instruction.opcode)) {
        AddCodegenError(errors, diagnostics, instruction.sourceNodeId, "node " + std::to_string(instruction.sourceNodeId) + " " + instruction.symbol + " requires a direct native binding");
        stream << "    // missing required native binding for " << instruction.symbol << "\n";
        return;
    }

    const std::string symbol = CppStringLiteral(instruction.symbol);
    switch (instruction.opcode) {
    case VisualGraphIrOpcode::CallNative:
        stream << "    context.CallNative(" << symbol << ");\n";
        break;
    case VisualGraphIrOpcode::EmitEvent:
        WriteEmitEventCall(stream, instruction);
        break;
    case VisualGraphIrOpcode::GetComponent:
        stream << "    context.RequireComponent(" << symbol << ");\n";
        break;
    case VisualGraphIrOpcode::GetProperty:
    case VisualGraphIrOpcode::SetProperty:
    case VisualGraphIrOpcode::Branch:
    case VisualGraphIrOpcode::Sequence:
    case VisualGraphIrOpcode::Wait:
        stream << "    context.Trace(" << CppStringLiteral(ToString(instruction.opcode)) << ", " << symbol << ");\n";
        break;
    }
}

void WriteExecuteCall(std::ostringstream& stream, std::string_view className, std::string_view functionName) {
    stream << "    " << className << "::" << functionName << "(context);\n";
}

void WriteEvaluateDependencies(
    std::ostringstream& stream,
    std::string_view className,
    const VisualGraphIrFunction& function,
    const VisualGraphIrInstruction& instruction) {
    std::vector<std::uint32_t> emittedDependencies;
    for (const VisualGraphIrInput& input : instruction.inputs) {
        if (std::ranges::find(emittedDependencies, input.sourceNodeId) != emittedDependencies.end()) {
            continue;
        }
        if (FindInstruction(function, input.sourceNodeId) == nullptr) {
            continue;
        }
        WriteExecuteCall(stream, className, NodeFunctionName("Evaluate", function, input.sourceNodeId));
        emittedDependencies.push_back(input.sourceNodeId);
    }
}

void WriteEvaluateBody(
    std::ostringstream& stream,
    std::string_view className,
    const VisualGraphIrFunction& function,
    const VisualGraphIrInstruction& instruction,
    const VisualGraphNativeBindingRegistry* bindings,
    bool requireNativeBindings,
    std::vector<std::string>& errors,
    std::vector<VisualGraphDiagnostic>& diagnostics) {
    stream << "void " << className << "::" << NodeFunctionName("Evaluate", function, instruction.sourceNodeId) << "(VisualGraphNativeExecutionContext& context) {\n";
    stream << "    if (context.IsNodeEvaluated(" << instruction.sourceNodeId << "U)) {\n";
    stream << "        return;\n";
    stream << "    }\n";
    WriteEvaluateDependencies(stream, className, function, instruction);
    WriteInstructionOperation(stream, instruction, bindings, requireNativeBindings, errors, diagnostics);
    stream << "    context.MarkNodeEvaluated(" << instruction.sourceNodeId << "U);\n";
    stream << "}\n\n";
}

void WriteExecuteBody(
    std::ostringstream& stream,
    std::string_view className,
    const VisualGraphIrFunction& function,
    const VisualGraphIrInstruction& instruction,
    const VisualGraphNativeBindingRegistry* bindings,
    bool requireNativeBindings,
    std::vector<std::string>& errors,
    std::vector<VisualGraphDiagnostic>& diagnostics) {
    stream << "void " << className << "::" << NodeFunctionName("Execute", function, instruction.sourceNodeId) << "(VisualGraphNativeExecutionContext& context) {\n";
    const VisualGraphIrInput* waitTask =
        instruction.opcode == VisualGraphIrOpcode::Wait ? FindInput(instruction, "task") : nullptr;
    if (waitTask != nullptr) {
        stream << "    std::uint64_t task = context.WaitTask(" << instruction.sourceNodeId << "U);\n";
        stream << "    if (task == 0U) {\n";
        WriteEvaluateDependencies(stream, className, function, instruction);
        stream << "        task = " << ReadInputExpression(*waitTask) << ";\n";
        stream << "        if (task == 0U) {\n";
        stream << "            context.ReportError(\"visual graph Wait requires a valid task handle\");\n";
        stream << "            return;\n";
        stream << "        }\n";
        stream << "        context.SetWaitTask(" << instruction.sourceNodeId << "U, task);\n";
        stream << "    }\n";
        stream << "    if (context.IsTaskRunning(task)) {\n";
        stream << "        context.Suspend(" << function.eventNodeId << "U, " << instruction.sourceNodeId << "U);\n";
        stream << "        return;\n";
        stream << "    }\n";
        stream << "    context.ClearWaitTask(" << instruction.sourceNodeId << "U);\n";
        if (instruction.nextNodeId != 0U && FindInstruction(function, instruction.nextNodeId) != nullptr) {
            WriteExecuteCall(stream, className, NodeFunctionName("Execute", function, instruction.nextNodeId));
        }
        stream << "}\n\n";
        return;
    }
    stream << "    if (!context.IsNodeEvaluated(" << instruction.sourceNodeId << "U)) {\n";
    WriteEvaluateDependencies(stream, className, function, instruction);

    if (instruction.opcode == VisualGraphIrOpcode::Branch) {
        stream << "        context.MarkNodeEvaluated(" << instruction.sourceNodeId << "U);\n";
        stream << "    }\n";
        const VisualGraphIrInput* condition = FindInput(instruction, "condition");
        const std::string conditionExpression = condition == nullptr ? "false" : ReadInputExpression(*condition);
        stream << "    if (" << conditionExpression << ") {\n";
        if (instruction.trueNodeId != 0U && FindInstruction(function, instruction.trueNodeId) != nullptr) {
            stream << "        " << className << "::" << NodeFunctionName("Execute", function, instruction.trueNodeId) << "(context);\n";
        }
        stream << "    } else {\n";
        if (instruction.falseNodeId != 0U && FindInstruction(function, instruction.falseNodeId) != nullptr) {
            stream << "        " << className << "::" << NodeFunctionName("Execute", function, instruction.falseNodeId) << "(context);\n";
        }
        stream << "    }\n";
        stream << "}\n\n";
        return;
    }

    if (instruction.opcode == VisualGraphIrOpcode::Wait) {
        stream << "        context.MarkNodeEvaluated(" << instruction.sourceNodeId << "U);\n";
        stream << "    }\n";
        if (instruction.nextNodeId != 0U) {
            stream << "    context.Suspend(" << function.eventNodeId << "U, " << instruction.nextNodeId << "U);\n";
        }
        stream << "}\n\n";
        return;
    }

    WriteInstructionOperation(stream, instruction, bindings, requireNativeBindings, errors, diagnostics);
    stream << "        context.MarkNodeEvaluated(" << instruction.sourceNodeId << "U);\n";
    stream << "    }\n";
    if (instruction.nextNodeId != 0U && FindInstruction(function, instruction.nextNodeId) != nullptr) {
        WriteExecuteCall(stream, className, NodeFunctionName("Execute", function, instruction.nextNodeId));
    }
    stream << "}\n\n";
}

void WriteFunctionBody(std::ostringstream& stream, std::string_view className, const VisualGraphIrFunction& function) {
    stream << "void " << className << "::" << FunctionName(function) << "(VisualGraphNativeExecutionContext& context) {\n";
    if (function.entryNodeId == 0U || FindInstruction(function, function.entryNodeId) == nullptr) {
        stream << "    static_cast<void>(context);\n";
    } else {
        stream << "    switch (context.TakeContinuation(" << function.eventNodeId << "U)) {\n";
        for (const VisualGraphIrInstruction& instruction : function.instructions) {
            stream << "    case " << instruction.sourceNodeId << "U:\n";
            WriteExecuteCall(stream, className, NodeFunctionName("Execute", function, instruction.sourceNodeId));
            stream << "        return;\n";
        }
        stream << "    default:\n";
        WriteExecuteCall(stream, className, NodeFunctionName("Execute", function, function.entryNodeId));
        stream << "        return;\n";
        stream << "    }\n";
    }
    stream << "}\n\n";
}

} // namespace

VisualGraphNativeCode VisualGraphNativeCodeGenerator::Generate(const VisualGraphIrModule& module, const VisualGraphNativeCodegenDesc& desc) {
    if (!IsValidNamespace(desc.namespaceName)) {
        return VisualGraphNativeCode{
            .errors = {"visual graph native code namespace is not a valid C++ namespace"},
            .diagnostics = {VisualGraphDiagnostics::Error(VisualGraphDiagnosticStage::NativeCodegen, "visual graph native code namespace is not a valid C++ namespace")},
        };
    }

    const std::string className = SanitizedIdentifier(desc.className.empty() ? module.graphName : desc.className, "GeneratedVisualGraph");
    const std::string headerFileName = className + ".generated.hpp";
    const std::string sourceFileName = className + ".generated.cpp";

    std::ostringstream header;
    header << "#pragma once\n\n";
    header << "#include <cstdint>\n";
    header << "#include <span>\n";
    header << "#include <string_view>\n\n";
    header << "#include <variant>\n\n";
    WriteNamespaceOpen(header, desc.namespaceName);
    header << "enum class VisualGraphNativeValueType : std::uint8_t {\n";
    header << "    Void,\n";
    header << "    Bool,\n";
    header << "    Int,\n";
    header << "    Float,\n";
    header << "    String,\n";
    header << "    Entity,\n";
    header << "    Component,\n";
    header << "    Int64,\n";
    header << "    UInt32,\n";
    header << "    Double,\n";
    header << "    Name,\n";
    header << "    Guid,\n";
    header << "    Hash,\n";
    header << "};\n\n";
    header << "using VisualGraphNativeEventValue = std::variant<std::monostate, bool, int, float, std::string_view, std::uint64_t, std::int64_t, double>;\n\n";
    header << "struct VisualGraphNativeEventArgument {\n";
    header << "    std::string_view name;\n";
    header << "    VisualGraphNativeValueType type = VisualGraphNativeValueType::Void;\n";
    header << "    VisualGraphNativeEventValue value;\n";
    header << "};\n\n";
    header << "class VisualGraphNativeFunctionResult {\n";
    header << "public:\n";
    header << "    bool ReadBool(std::string_view name) const;\n";
    header << "    int ReadInt(std::string_view name) const;\n";
    header << "    float ReadFloat(std::string_view name) const;\n";
    header << "    std::string_view ReadString(std::string_view name) const;\n";
    header << "    std::uint64_t ReadEntity(std::string_view name) const;\n";
    header << "    std::uint64_t ReadComponent(std::string_view name) const;\n";
    header << "    std::int64_t ReadInt64(std::string_view name) const;\n";
    header << "    std::uint32_t ReadUInt32(std::string_view name) const;\n";
    header << "    double ReadDouble(std::string_view name) const;\n";
    header << "    std::string_view ReadName(std::string_view name) const;\n";
    header << "    std::string_view ReadGuid(std::string_view name) const;\n";
    header << "    std::uint64_t ReadHash(std::string_view name) const;\n";
    header << "};\n\n";
    header << "class VisualGraphNativeExecutionContext {\n";
    header << "public:\n";
    header << "    void CallNative(std::string_view name);\n";
    header << "    VisualGraphNativeFunctionResult CallFunction(std::string_view name, std::span<const VisualGraphNativeEventArgument> arguments);\n";
    header << "    void EmitEvent(std::string_view name);\n";
    header << "    void EmitEvent(std::string_view name, std::span<const VisualGraphNativeEventArgument> arguments);\n";
    header << "    void EmitEventTo(std::uint64_t targetEntity, std::string_view name);\n";
    header << "    void EmitEventTo(std::uint64_t targetEntity, std::string_view name, std::span<const VisualGraphNativeEventArgument> arguments);\n";
    header << "    void RequireComponent(std::string_view name);\n";
    header << "    void Trace(std::string_view opcode, std::string_view symbol);\n";
    header << "    void ReportError(std::string_view message);\n";
    header << "    void Suspend(std::uint32_t eventNodeId, std::uint32_t nextNodeId);\n";
    header << "    std::uint32_t TakeContinuation(std::uint32_t eventNodeId);\n";
    header << "    bool IsTaskRunning(std::uint64_t taskId);\n";
    header << "    void SetWaitTask(std::uint32_t nodeId, std::uint64_t taskId);\n";
    header << "    std::uint64_t WaitTask(std::uint32_t nodeId);\n";
    header << "    void ClearWaitTask(std::uint32_t nodeId);\n";
    header << "    bool ReadBool(std::uint32_t sourceNodeId, std::string_view sourcePin);\n";
    header << "    int ReadInt(std::uint32_t sourceNodeId, std::string_view sourcePin);\n";
    header << "    float ReadFloat(std::uint32_t sourceNodeId, std::string_view sourcePin);\n";
    header << "    std::string_view ReadString(std::uint32_t sourceNodeId, std::string_view sourcePin);\n";
    header << "    std::uint64_t ReadEntity(std::uint32_t sourceNodeId, std::string_view sourcePin);\n";
    header << "    std::uint64_t ReadComponent(std::uint32_t sourceNodeId, std::string_view sourcePin);\n";
    header << "    void StoreBool(std::uint32_t sourceNodeId, std::string_view sourcePin, bool value);\n";
    header << "    void StoreInt(std::uint32_t sourceNodeId, std::string_view sourcePin, int value);\n";
    header << "    void StoreFloat(std::uint32_t sourceNodeId, std::string_view sourcePin, float value);\n";
    header << "    void StoreString(std::uint32_t sourceNodeId, std::string_view sourcePin, std::string_view value);\n";
    header << "    void StoreEntity(std::uint32_t sourceNodeId, std::string_view sourcePin, std::uint64_t value);\n";
    header << "    void StoreComponent(std::uint32_t sourceNodeId, std::string_view sourcePin, std::uint64_t value);\n";
    header << "    std::int64_t ReadInt64(std::uint32_t sourceNodeId, std::string_view sourcePin);\n";
    header << "    std::uint32_t ReadUInt32(std::uint32_t sourceNodeId, std::string_view sourcePin);\n";
    header << "    double ReadDouble(std::uint32_t sourceNodeId, std::string_view sourcePin);\n";
    header << "    std::string_view ReadName(std::uint32_t sourceNodeId, std::string_view sourcePin);\n";
    header << "    std::string_view ReadGuid(std::uint32_t sourceNodeId, std::string_view sourcePin);\n";
    header << "    std::uint64_t ReadHash(std::uint32_t sourceNodeId, std::string_view sourcePin);\n";
    header << "    void StoreInt64(std::uint32_t sourceNodeId, std::string_view sourcePin, std::int64_t value);\n";
    header << "    void StoreUInt32(std::uint32_t sourceNodeId, std::string_view sourcePin, std::uint32_t value);\n";
    header << "    void StoreDouble(std::uint32_t sourceNodeId, std::string_view sourcePin, double value);\n";
    header << "    void StoreName(std::uint32_t sourceNodeId, std::string_view sourcePin, std::string_view value);\n";
    header << "    void StoreGuid(std::uint32_t sourceNodeId, std::string_view sourcePin, std::string_view value);\n";
    header << "    void StoreHash(std::uint32_t sourceNodeId, std::string_view sourcePin, std::uint64_t value);\n";
    header << "    bool SelfHasComponent(std::string_view component);\n";
    header << "    bool SelfGetPropertyBool(std::string_view component, std::string_view property);\n";
    header << "    int SelfGetPropertyInt(std::string_view component, std::string_view property);\n";
    header << "    float SelfGetPropertyFloat(std::string_view component, std::string_view property);\n";
    header << "    std::string_view SelfGetPropertyString(std::string_view component, std::string_view property);\n";
    header << "    std::uint64_t SelfGetPropertyEntity(std::string_view component, std::string_view property);\n";
    header << "    std::uint64_t SelfGetPropertyComponent(std::string_view component, std::string_view property);\n";
    header << "    bool SelfSetPropertyBool(std::string_view component, std::string_view property, bool value);\n";
    header << "    bool SelfSetPropertyInt(std::string_view component, std::string_view property, int value);\n";
    header << "    bool SelfSetPropertyFloat(std::string_view component, std::string_view property, float value);\n";
    header << "    bool SelfSetPropertyString(std::string_view component, std::string_view property, std::string_view value);\n";
    header << "    bool SelfSetPropertyEntity(std::string_view component, std::string_view property, std::uint64_t value);\n";
    header << "    bool SelfSetPropertyComponent(std::string_view component, std::string_view property, std::uint64_t value);\n";
    header << "    bool SharedHas(std::string_view key);\n";
    header << "    bool SharedRemove(std::string_view key);\n";
    header << "    bool SharedGetBool(std::string_view key);\n";
    header << "    int SharedGetInt(std::string_view key);\n";
    header << "    float SharedGetFloat(std::string_view key);\n";
    header << "    std::string_view SharedGetString(std::string_view key);\n";
    header << "    std::uint64_t SharedGetEntity(std::string_view key);\n";
    header << "    std::uint64_t SharedGetComponent(std::string_view key);\n";
    header << "    bool SharedSetBool(std::string_view key, bool value);\n";
    header << "    bool SharedSetInt(std::string_view key, int value);\n";
    header << "    bool SharedSetFloat(std::string_view key, float value);\n";
    header << "    bool SharedSetString(std::string_view key, std::string_view value);\n";
    header << "    bool SharedSetEntity(std::string_view key, std::uint64_t value);\n";
    header << "    bool SharedSetComponent(std::string_view key, std::uint64_t value);\n";
    header << "    bool IsNodeEvaluated(std::uint32_t sourceNodeId);\n";
    header << "    void MarkNodeEvaluated(std::uint32_t sourceNodeId);\n";
    header << "};\n\n";
    header << "class " << className << " final {\n";
    header << "public:\n";
    for (const VisualGraphIrFunction& function : module.functions) {
        WriteFunctionDecl(header, function);
    }
    header << "\n";
    header << "private:\n";
    for (const VisualGraphIrFunction& function : module.functions) {
        for (const VisualGraphIrInstruction& instruction : function.instructions) {
            header << "    void " << NodeFunctionName("Execute", function, instruction.sourceNodeId) << "(VisualGraphNativeExecutionContext& context);\n";
            header << "    void " << NodeFunctionName("Evaluate", function, instruction.sourceNodeId) << "(VisualGraphNativeExecutionContext& context);\n";
        }
    }
    header << "};\n\n";
    WriteNamespaceClose(header, desc.namespaceName);

    std::ostringstream source;
    std::vector<std::string> errors;
    std::vector<VisualGraphDiagnostic> diagnostics;
    source << "#include \"" << headerFileName << "\"\n\n";
    WriteSourceIncludes(source, desc.sourceIncludes, errors, diagnostics);
    WriteNamespaceOpen(source, desc.namespaceName);
    for (const VisualGraphIrFunction& function : module.functions) {
        WriteFunctionBody(source, className, function);
        for (const VisualGraphIrInstruction& instruction : function.instructions) {
            WriteExecuteBody(source, className, function, instruction, desc.bindings, desc.requireNativeBindings, errors, diagnostics);
            WriteEvaluateBody(source, className, function, instruction, desc.bindings, desc.requireNativeBindings, errors, diagnostics);
        }
    }
    WriteNamespaceClose(source, desc.namespaceName);

    return VisualGraphNativeCode{
        .header = header.str(),
        .source = source.str(),
        .headerFileName = headerFileName,
        .sourceFileName = sourceFileName,
        .errors = std::move(errors),
        .diagnostics = std::move(diagnostics),
    };
}

} // namespace kb::visual
