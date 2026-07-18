#include "engine/visual/VisualGraphRuntimeExecutor.hpp"

#include <ranges>

namespace kb::visual {
namespace {

// LIB-101: bounds a single top-level Execute*/ExecuteCustomEvent call's
// TOTAL ExecuteNode invocation count — deliberately generous (legitimate
// graphs authoring a bounded "for"/"while" pattern via Branch/Sequence can
// need many steps), but finite, catching the one gap the pre-existing
// `executing` NodeSet cycle guard does not: a CONTROL-FLOW loop (Branch or
// plain fall-through wired back to an already-executed ancestor), which
// `executing.erase(nodeId)` running before every forward-flow recursive
// call lets slip straight through as unbounded C++ recursion today. Same
// numeric choice as LIB-058's kDefaultLibraryInputLimits.maxCollectionSize
// (4096) — a familiar "clearly gone wrong" bound already used elsewhere in
// this codebase, not independently derived.
constexpr std::size_t kMaxVisualGraphExecutionSteps = 4096U;

// LIB-101: the maximum C++ recursion DEPTH of the data-input resolution
// chain (see ExecuteNode's `depth` parameter). Deliberately FAR below
// kMaxVisualGraphExecutionSteps: the step budget is a plain invocation
// COUNT, but each level of data-input recursion is a real C++ stack frame
// (a VisualGraphRuntimeExecutionResult plus locals), and a few thousand of
// them overflow a default 1 MB stack — the exact STATUS_STACK_OVERFLOW the
// step-count watchdog alone could not prevent. 256 is a generous ceiling
// for any legitimately-authored data DAG (the acyclic-edge validator keeps
// real graphs shallow) while sitting safely under the frame count that
// would actually blow the stack.
constexpr std::size_t kMaxVisualGraphInputDepth = 256U;

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

void AppendContextErrors(
    VisualGraphRuntimeExecutionResult& result,
    std::uint32_t nodeId,
    const VisualGraphRuntimeExecutionContext& context) {
    for (const std::string& error : context.RuntimeErrors()) {
        AddRuntimeError(result, nodeId, error);
    }
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
    context.BeginExecutionPass();
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
        context.BeginExecutionPass();
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
    std::size_t stepBudget = kMaxVisualGraphExecutionSteps;
    VisualGraphRuntimeExecutionResult executed = ExecuteNode(instructions, function->entryNodeId, context, executing, evaluatedNodes, true, stepBudget, 0U);
    executed.executed = true;
    return executed;
}

VisualGraphRuntimeExecutionResult VisualGraphRuntimeExecutor::ExecuteNode(
    const InstructionMap& instructions,
    std::uint32_t nodeId,
    VisualGraphRuntimeExecutionContext& context,
    NodeSet& executing,
    NodeSet& evaluatedNodes,
    bool followExecution,
    std::size_t& stepBudget,
    std::size_t depth) const {
    VisualGraphRuntimeExecutionResult result{};

    // LIB-101: a data-input chain deeper than kMaxVisualGraphInputDepth would
    // recurse deep enough into C++ to overflow the stack — fail with a
    // diagnostic here, BEFORE the recursive call below adds another frame,
    // rather than crashing the whole process with STATUS_STACK_OVERFLOW. The
    // iterative forward-flow walk never increases depth, so this only ever
    // trips on a genuinely deep data DAG, never on a long control-flow chain.
    if (depth > kMaxVisualGraphInputDepth) {
        AddRuntimeError(result, nodeId, "visual graph runtime exceeded its maximum data-input recursion depth (kMaxVisualGraphInputDepth=" + std::to_string(kMaxVisualGraphInputDepth) + ") — a pathologically deep chain of data-dependency nodes, refused before it can overflow the stack");
        return result;
    }

    // LIB-101: forward execution flow (Branch/CallNative/plain fall-through,
    // all gated on followExecution==true below) is walked ITERATIVELY here,
    // not via a per-step recursive call, specifically so a long OR cyclic
    // chain costs O(1) C++ stack regardless of stepBudget's size — an
    // earlier version of this fix recursed once per forward step and
    // crashed with a genuine stack overflow (STATUS_STACK_OVERFLOW) well
    // before a 4096-step stepBudget was ever exhausted, since each
    // recursive VisualGraphRuntimeExecutionResult stack frame costs
    // meaningfully more than zero bytes. A followExecution==false call
    // (resolving one node's OWN data inputs, recursed into below) never
    // reaches any of the `continue` sites — every one of them is gated on
    // followExecution==true — so this loop runs exactly once for that case,
    // identical to the function's pre-LIB-101 behavior. Input-dependency
    // recursion itself is NOT converted to iteration (it is tree/DAG-shaped,
    // not chain-shaped, and the asset-level validator, VisualGraphValidator::
    // ValidateAcyclicEdges, already keeps it acyclic and shallow for any
    // graph that went through the normal authoring pipeline) — it still
    // shares this same stepBudget, so a pathologically deep hand-authored
    // data chain is still bounded, just not guaranteed stack-safe the way
    // forward flow now is; not fabricated as fully solved here.
    while (true) {
        if (nodeId == 0U) {
            return result;
        }
        // LIB-101: catches the control-flow-loop gap the `executing` cycle
        // guard just below cannot — see this function's doc comment above
        // and in the header for the full reasoning.
        if (stepBudget == 0U) {
            AddRuntimeError(result, nodeId, "visual graph runtime exceeded its maximum step budget (kMaxVisualGraphExecutionSteps=" + std::to_string(kMaxVisualGraphExecutionSteps) + ") — likely an infinite control-flow loop (a Branch/Sequence wired back to an already-executed ancestor)");
            return result;
        }
        --stepBudget;
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
            // LIB-101: `nodeResult` is deliberately a FRESH, per-iteration
            // value — the original recursive version got this for free
            // (each ExecuteNode call had its own local `result`); the
            // iterative rewrite must recreate that isolation explicitly, or
            // an EARLIER node's already-merged-in, already-handled failure
            // would incorrectly poison THIS node's own "did my own dispatch
            // just fail" checks below (isHandledCallNativeFailure in
            // particular) — confirmed by a real regression this rewrite
            // introduced and caught by the existing RunVisualGraph*
            // CallNativeFailureBranchTest before this comment was written.
            VisualGraphRuntimeExecutionResult nodeResult{};
            for (const VisualGraphIrInput& input : instruction->inputs) {
                if (evaluatedNodes.contains(input.sourceNodeId)) {
                    continue;
                }
                if (!instructions.contains(input.sourceNodeId)) {
                    if (context.TryRead(input.sourceNodeId, input.sourcePin) != nullptr) {
                        continue;
                    }
                }
                AppendErrors(nodeResult, ExecuteNode(instructions, input.sourceNodeId, context, executing, evaluatedNodes, false, stepBudget, depth + 1U));
                if (!nodeResult.Succeeded()) {
                    executing.erase(nodeId);
                    AppendErrors(result, std::move(nodeResult));
                    return result;
                }
            }

            if (instruction->opcode == VisualGraphIrOpcode::EmitEvent) {
                context.EmitEvent(instruction->symbol, CollectEventArguments(*instruction, context), CollectEventTarget(*instruction, context));
            } else if (instruction->opcode == VisualGraphIrOpcode::CallNative) {
                const VisualGraphRuntimeBinding* binding = FindBinding(*instruction);
                if (binding == nullptr && TryExecuteBuiltInInstruction(*instruction, context)) {
                } else if (binding == nullptr) {
                    AddRuntimeError(nodeResult, instruction->sourceNodeId, "visual graph runtime missing binding for node " + std::to_string(instruction->sourceNodeId) + " " + instruction->symbol);
                } else {
                    AppendErrors(nodeResult, ValidateBindingSignature(*instruction, *binding));
                    if (nodeResult.Succeeded()) {
                        // LIB-061: a call is judged to have failed when it added
                        // AT LEAST ONE new runtime error (context.RuntimeErrors()
                        // is a persistent, ever-growing log — only the slice
                        // added during THIS callback counts, never the whole
                        // list, since a later node reached after a HANDLED
                        // failure must not re-report an earlier node's already-
                        // recorded errors as its own). The verdict is stashed as
                        // a "failed" pin value so a later revisit of this node
                        // (e.g. reached via two different execution paths) can
                        // re-derive the same branch decision without
                        // re-invoking the callback, mirroring how Branch re-reads
                        // its "condition" data pin on every visit.
                        //
                        // The failure is ALWAYS recorded as a runtime error —
                        // whether or not a "failed" handler is wired — matching
                        // this codebase's existing diagnostic model, where a
                        // real failure is never silently downgraded (ScriptDiagnostic
                        // carries no severity; any diagnostic means the overall
                        // Tick reports Succeeded() == false). What a wired
                        // "failed" handler changes is NOT whether the failure is
                        // reported, but whether the graph gets a chance to react
                        // to it (log more context, apply a fallback, notify
                        // another system) before that report happens, instead of
                        // execution simply halting at the point of failure.
                        const std::size_t errorCountBefore = context.RuntimeErrors().size();
                        binding->callback(context, *instruction);
                        const bool callFailed = context.RuntimeErrors().size() > errorCountBefore;
                        context.Store(instruction->sourceNodeId, "failed", VisualGraphRuntimeValue{ callFailed });
                        if (callFailed) {
                            for (std::size_t i = errorCountBefore; i < context.RuntimeErrors().size(); ++i) {
                                AddRuntimeError(nodeResult, instruction->sourceNodeId, context.RuntimeErrors()[i]);
                            }
                        } else {
                            AppendErrors(nodeResult, ValidateProducedOutputs(*instruction, *binding, context));
                        }
                    }
                }
            } else if (instruction->opcode != VisualGraphIrOpcode::Branch && instruction->opcode != VisualGraphIrOpcode::Sequence) {
                const VisualGraphRuntimeBinding* binding = FindBinding(*instruction);
                if (binding == nullptr && TryExecuteBuiltInInstruction(*instruction, context)) {
                } else if (binding == nullptr) {
                    AddRuntimeError(nodeResult, instruction->sourceNodeId, "visual graph runtime missing binding for node " + std::to_string(instruction->sourceNodeId) + " " + instruction->symbol);
                } else {
                    AppendErrors(nodeResult, ValidateBindingSignature(*instruction, *binding));
                    if (nodeResult.Succeeded()) {
                        binding->callback(context, *instruction);
                        AppendContextErrors(nodeResult, instruction->sourceNodeId, context);
                        if (nodeResult.Succeeded()) {
                            AppendErrors(nodeResult, ValidateProducedOutputs(*instruction, *binding, context));
                        }
                    }
                }
            }

            if (!nodeResult.Succeeded()) {
                // LIB-061: a CallNative call that failed but has a wired
                // "failed" exec output is still allowed to continue — the
                // failure was already recorded above (Succeeded() stays false
                // for the overall Tick either way), but the graph gets to run
                // its handler instead of execution halting right here. Every
                // other failure reason (missing binding, signature mismatch, an
                // UNWIRED CallNative failure, any other opcode's error) keeps
                // the historical hard-abort.
                const bool isHandledCallNativeFailure = instruction->opcode == VisualGraphIrOpcode::CallNative && instruction->falseNodeId != 0U &&
                    context.ReadBool(instruction->sourceNodeId, "failed");
                if (!isHandledCallNativeFailure) {
                    executing.erase(nodeId);
                    AppendErrors(result, std::move(nodeResult));
                    return result;
                }
            }
            evaluatedNodes.insert(nodeId);
            AppendErrors(result, std::move(nodeResult));
        }

        if (instruction->opcode == VisualGraphIrOpcode::Branch && followExecution) {
            const auto condition = std::ranges::find_if(instruction->inputs, [](const VisualGraphIrInput& input) {
                return input.name == "condition";
            });
            const bool conditionValue = condition == instruction->inputs.end() ? false : context.ReadBool(condition->sourceNodeId, condition->sourcePin);
            executing.erase(nodeId);
            // LIB-101: iterative — see the loop's own doc comment above for
            // why this used to be `return ExecuteNode(...)`.
            nodeId = conditionValue ? instruction->trueNodeId : instruction->falseNodeId;
            continue;
        }

        // LIB-061: only reached when the call above either succeeded, or failed
        // but had a wired "failed" handler (an unwired failure already returned
        // early above, matching the pre-LIB-061 default). Unlike Branch just
        // above, `result` can already be non-empty here (a handled failure's
        // recorded error) — the next iteration's own errors naturally
        // ACCUMULATE into the same `result` rather than needing an explicit
        // merge (LIB-101: this is what used to be an explicit AppendErrors of
        // a separate recursive call's result).
        if (instruction->opcode == VisualGraphIrOpcode::CallNative && followExecution) {
            const bool callFailed = context.ReadBool(instruction->sourceNodeId, "failed");
            executing.erase(nodeId);
            nodeId = callFailed ? instruction->falseNodeId : instruction->trueNodeId;
            continue;
        }

        const std::uint32_t nextNodeId = instruction->nextNodeId;
        executing.erase(nodeId);
        if (!result.Succeeded()) {
            return result;
        }
        if (!followExecution || nextNodeId == 0U) {
            return result;
        }
        nodeId = nextNodeId;
    }
}

const VisualGraphRuntimeBinding* VisualGraphRuntimeExecutor::FindBinding(const VisualGraphIrInstruction& instruction) const noexcept {
    return bindings_.Find(instruction.opcode, instruction.symbol);
}

bool VisualGraphRuntimeExecutor::TryExecuteBuiltInInstruction(const VisualGraphIrInstruction& instruction, VisualGraphRuntimeExecutionContext& context) {
    if (instruction.opcode == VisualGraphIrOpcode::GetProperty && instruction.symbol == "DeltaSeconds") {
        const float deltaSeconds = context.ReadFloat(0U, "deltaSeconds");
        for (const VisualGraphIrOutput& output : instruction.outputs) {
            if (output.name == "value" && output.type == VisualGraphValueType::Float) {
                context.Store(instruction.sourceNodeId, output.name, VisualGraphRuntimeValue{deltaSeconds});
            }
        }
        return true;
    }
    return false;
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

std::vector<VisualGraphEventArgument> VisualGraphRuntimeExecutor::CollectEventArguments(
    const VisualGraphIrInstruction& instruction,
    const VisualGraphRuntimeExecutionContext& context) {
    std::vector<VisualGraphEventArgument> arguments;
    for (const VisualGraphIrInput& input : instruction.inputs) {
        if (input.type == VisualGraphValueType::Void || input.name == "exec") {
            continue;
        }
        if (input.name == "target" && input.type == VisualGraphValueType::Entity) {
            continue;
        }
        const VisualGraphRuntimeValue* value = context.TryRead(input.sourceNodeId, input.sourcePin);
        if (value == nullptr) {
            continue;
        }
        arguments.push_back(VisualGraphEventArgument{
            .name = input.name,
            .value = *value,
        });
    }
    return arguments;
}

kb::scene::SceneEntity VisualGraphRuntimeExecutor::CollectEventTarget(
    const VisualGraphIrInstruction& instruction,
    const VisualGraphRuntimeExecutionContext& context) {
    for (const VisualGraphIrInput& input : instruction.inputs) {
        if (input.name == "target" && input.type == VisualGraphValueType::Entity) {
            return kb::scene::SceneEntity{ context.ReadUInt64(input.sourceNodeId, input.sourcePin) };
        }
    }
    return {};
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
