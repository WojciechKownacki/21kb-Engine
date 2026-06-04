#include "engine/visual/VisualGraphRuntimeExecutor.hpp"

#include <ranges>

namespace kb::visual {
namespace {

void AppendErrors(VisualGraphRuntimeExecutionResult& target, VisualGraphRuntimeExecutionResult source) {
    target.errors.insert(target.errors.end(), source.errors.begin(), source.errors.end());
    target.diagnostics.insert(target.diagnostics.end(), source.diagnostics.begin(), source.diagnostics.end());
}

void AddRuntimeError(VisualGraphRuntimeExecutionResult& result, std::uint32_t nodeId, std::string message) {
    result.errors.push_back(message);
    result.diagnostics.push_back(VisualGraphDiagnostics::Error(VisualGraphDiagnosticStage::Runtime, nodeId, std::move(message)));
}

void AddRuntimeError(VisualGraphRuntimeExecutionResult& result, std::uint32_t nodeId, std::string_view pinName, std::string message) {
    result.errors.push_back(message);
    result.diagnostics.push_back(VisualGraphDiagnostics::Error(VisualGraphDiagnosticStage::Runtime, nodeId, std::string{pinName}, std::move(message)));
}

} // namespace

VisualGraphRuntimeExecutor::VisualGraphRuntimeExecutor(const VisualGraphRuntimeBindingRegistry& bindings) noexcept
    : bindings_(bindings) {}

VisualGraphRuntimeExecutionResult VisualGraphRuntimeExecutor::Execute(
    const VisualGraphRuntimeArtifact& artifact,
    VisualGraphLifecycleEvent event,
    VisualGraphRuntimeExecutionContext& context) const {
    return ExecuteFunction(FindLifecycleFunction(artifact, event), context);
}

VisualGraphRuntimeExecutionResult VisualGraphRuntimeExecutor::ExecuteCustomEvent(
    const VisualGraphRuntimeArtifact& artifact,
    std::string_view eventName,
    VisualGraphRuntimeExecutionContext& context) const {
    return ExecuteCustomEvent(artifact, eventName, std::span<const VisualGraphCustomEventArgument>{}, context);
}

VisualGraphRuntimeExecutionResult VisualGraphRuntimeExecutor::ExecuteCustomEvent(
    const VisualGraphRuntimeArtifact& artifact,
    std::string_view eventName,
    std::span<const VisualGraphCustomEventArgument> arguments,
    VisualGraphRuntimeExecutionContext& context) const {
    const VisualGraphIrFunction* function = FindCustomEventFunction(artifact, eventName);
    if (function == nullptr) {
        return {};
    }
    VisualGraphRuntimeExecutionResult argumentResult = StoreCustomEventArguments(*function, arguments, context);
    if (!argumentResult.Succeeded()) {
        return argumentResult;
    }
    return ExecuteFunction(function, context);
}

VisualGraphRuntimeExecutionResult VisualGraphRuntimeExecutor::ExecuteFunction(const VisualGraphIrFunction* function, VisualGraphRuntimeExecutionContext& context) const {
    VisualGraphRuntimeExecutionResult result{};
    if (function == nullptr) {
        return result;
    }
    result.executed = true;
    if (function->entryNodeId == 0U) {
        return result;
    }

    context.BeginExecutionPass();
    const InstructionMap instructions = BuildInstructionMap(*function);
    NodeSet executing;
    NodeSet evaluatedNodes;
    VisualGraphRuntimeExecutionResult executed = ExecuteNode(instructions, function->entryNodeId, context, executing, evaluatedNodes, true);
    executed.executed = true;
    return executed;
}

VisualGraphRuntimeExecutionResult VisualGraphRuntimeExecutor::ExecuteNode(
    const InstructionMap& instructions,
    std::uint32_t nodeId,
    VisualGraphRuntimeExecutionContext& context,
    NodeSet& executing,
    NodeSet& evaluatedNodes,
    bool followExecution) const {
    VisualGraphRuntimeExecutionResult result{};
    if (nodeId == 0U) {
        return result;
    }
    if (!executing.insert(nodeId).second) {
        AddRuntimeError(result, nodeId, "visual graph runtime cycle detected at node " + std::to_string(nodeId));
        return result;
    }

    const auto instructionIter = instructions.find(nodeId);
    if (instructionIter == instructions.end()) {
        executing.erase(nodeId);
        AddRuntimeError(result, nodeId, "visual graph runtime missing instruction for node " + std::to_string(nodeId));
        return result;
    }
    const VisualGraphIrInstruction* instruction = instructionIter->second;

    if (!evaluatedNodes.contains(nodeId)) {
        for (const VisualGraphIrInput& input : instruction->inputs) {
            if (evaluatedNodes.contains(input.sourceNodeId)) {
                continue;
            }
            if (!instructions.contains(input.sourceNodeId)) {
                if (context.TryRead(input.sourceNodeId, input.sourcePin) != nullptr) {
                    continue;
                }
            }
            AppendErrors(result, ExecuteNode(instructions, input.sourceNodeId, context, executing, evaluatedNodes, false));
            if (!result.Succeeded()) {
                executing.erase(nodeId);
                return result;
            }
        }

        if (instruction->opcode == VisualGraphIrOpcode::EmitEvent) {
            context.EmitEvent(instruction->symbol);
        } else if (instruction->opcode != VisualGraphIrOpcode::Branch && instruction->opcode != VisualGraphIrOpcode::Sequence) {
            const VisualGraphRuntimeBinding* binding = FindBinding(*instruction);
            if (binding == nullptr) {
                AddRuntimeError(result, instruction->sourceNodeId, "visual graph runtime missing binding for node " + std::to_string(instruction->sourceNodeId) + " " + instruction->symbol);
            } else {
                AppendErrors(result, ValidateBindingSignature(*instruction, *binding));
                if (result.Succeeded()) {
                    binding->callback(context, *instruction);
                    AppendErrors(result, ValidateProducedOutputs(*instruction, *binding, context));
                }
            }
        }

        if (!result.Succeeded()) {
            executing.erase(nodeId);
            return result;
        }
        evaluatedNodes.insert(nodeId);
    }

    if (instruction->opcode == VisualGraphIrOpcode::Branch && followExecution) {
        const auto condition = std::ranges::find_if(instruction->inputs, [](const VisualGraphIrInput& input) {
            return input.name == "condition";
        });
        const bool conditionValue = condition == instruction->inputs.end() ? false : context.ReadBool(condition->sourceNodeId, condition->sourcePin);
        executing.erase(nodeId);
        return ExecuteNode(instructions, conditionValue ? instruction->trueNodeId : instruction->falseNodeId, context, executing, evaluatedNodes, true);
    }

    const std::uint32_t nextNodeId = instruction->nextNodeId;
    executing.erase(nodeId);
    if (!result.Succeeded()) {
        return result;
    }
    if (followExecution && nextNodeId != 0U) {
        AppendErrors(result, ExecuteNode(instructions, nextNodeId, context, executing, evaluatedNodes, true));
    }
    return result;
}

const VisualGraphRuntimeBinding* VisualGraphRuntimeExecutor::FindBinding(const VisualGraphIrInstruction& instruction) const noexcept {
    return bindings_.Find(instruction.opcode, instruction.symbol);
}

VisualGraphRuntimeExecutionResult VisualGraphRuntimeExecutor::ValidateBindingSignature(const VisualGraphIrInstruction& instruction, const VisualGraphRuntimeBinding& binding) {
    VisualGraphRuntimeExecutionResult result{};
    const std::size_t errorCount = result.errors.size();
    VisualGraphBindingSignatureValidator::Validate(binding.symbol, binding.inputs, binding.outputs, instruction, result.errors);
    for (std::size_t index = errorCount; index < result.errors.size(); ++index) {
        result.diagnostics.push_back(VisualGraphDiagnostics::Error(VisualGraphDiagnosticStage::Runtime, instruction.sourceNodeId, result.errors[index]));
    }
    return result;
}

VisualGraphRuntimeExecutionResult VisualGraphRuntimeExecutor::ValidateProducedOutputs(
    const VisualGraphIrInstruction& instruction,
    const VisualGraphRuntimeBinding& binding,
    const VisualGraphRuntimeExecutionContext& context) {
    VisualGraphRuntimeExecutionResult result{};
    for (const VisualGraphPinSignature& output : binding.outputs) {
        if (!context.WasStoredInCurrentExecution(instruction.sourceNodeId, output.name)) {
            if (output.required) {
                AddRuntimeError(result, instruction.sourceNodeId, output.name, "visual graph runtime binding '" + binding.symbol + "' did not produce required output '" + output.name + "'");
            }
            continue;
        }
        const VisualGraphRuntimeValue* value = context.TryRead(instruction.sourceNodeId, output.name);
        if (value == nullptr) {
            if (output.required) {
                AddRuntimeError(result, instruction.sourceNodeId, output.name, "visual graph runtime binding '" + binding.symbol + "' did not produce required output '" + output.name + "'");
            }
            continue;
        }
        if (output.type != VisualGraphValueType::Void && value->Type() != output.type) {
            AddRuntimeError(result, instruction.sourceNodeId, output.name,
                "visual graph runtime binding '" + binding.symbol + "' produced output '" + output.name + "' as " + ToString(value->Type()) + " but graph expects " + ToString(output.type));
        }
    }
    return result;
}

const VisualGraphIrFunction* VisualGraphRuntimeExecutor::FindLifecycleFunction(const VisualGraphRuntimeArtifact& artifact, VisualGraphLifecycleEvent event) noexcept {
    const auto iter = std::ranges::find_if(artifact.module.functions, [event](const VisualGraphIrFunction& function) {
        return function.customEventName.empty() && function.event == event;
    });
    return iter == artifact.module.functions.end() ? nullptr : &*iter;
}

const VisualGraphIrFunction* VisualGraphRuntimeExecutor::FindCustomEventFunction(const VisualGraphRuntimeArtifact& artifact, std::string_view eventName) noexcept {
    const auto iter = std::ranges::find_if(artifact.module.functions, [eventName](const VisualGraphIrFunction& function) {
        return function.customEventName == eventName;
    });
    return iter == artifact.module.functions.end() ? nullptr : &*iter;
}

VisualGraphRuntimeExecutionResult VisualGraphRuntimeExecutor::StoreCustomEventArguments(
    const VisualGraphIrFunction& function,
    std::span<const VisualGraphCustomEventArgument> arguments,
    VisualGraphRuntimeExecutionContext& context) {
    VisualGraphRuntimeExecutionResult result{};
    result.executed = true;
    for (const VisualGraphIrOutput& output : function.eventOutputs) {
        const auto argument = std::ranges::find_if(arguments, [&output](const VisualGraphCustomEventArgument& candidate) {
            return candidate.name == output.name;
        });
        if (argument == arguments.end()) {
            AddRuntimeError(result, function.eventNodeId, output.name, "visual graph custom event '" + function.customEventName + "' is missing required argument '" + output.name + "'");
            continue;
        }
        if (argument->value.Type() != output.type) {
            AddRuntimeError(result, function.eventNodeId, output.name,
                "visual graph custom event '" + function.customEventName + "' argument '" + output.name + "' is " + ToString(argument->value.Type()) + " but graph expects " + ToString(output.type));
            continue;
        }
        context.Store(function.eventNodeId, output.name, argument->value);
    }
    return result;
}

VisualGraphRuntimeExecutor::InstructionMap VisualGraphRuntimeExecutor::BuildInstructionMap(const VisualGraphIrFunction& function) {
    InstructionMap instructions;
    instructions.reserve(function.instructions.size());
    for (const VisualGraphIrInstruction& instruction : function.instructions) {
        instructions.emplace(instruction.sourceNodeId, &instruction);
    }
    return instructions;
}

} // namespace kb::visual
