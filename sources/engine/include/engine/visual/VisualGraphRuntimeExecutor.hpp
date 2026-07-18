#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/visual/VisualGraphRuntimeBindingRegistry.hpp"
#include "engine/visual/VisualGraphRuntimeRegistry.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kb::visual {

using VisualGraphCustomEventArgument = VisualGraphEventArgument;

struct VisualGraphRuntimeExecutionResult {
    bool executed = false;
    std::vector<std::string> errors;
    std::vector<VisualGraphDiagnostic> diagnostics;

    [[nodiscard]] bool Succeeded() const noexcept {
        return errors.empty() && !VisualGraphDiagnostics::HasErrors(diagnostics);
    }
};

class VisualGraphRuntimeExecutor final {
public:
    explicit VisualGraphRuntimeExecutor(const VisualGraphRuntimeBindingRegistry& bindings) noexcept;

    [[nodiscard]] VisualGraphRuntimeExecutionResult Execute(
        const VisualGraphRuntimeArtifact& artifact,
        VisualGraphLifecycleEvent event,
        VisualGraphRuntimeExecutionContext& context) const;
    [[nodiscard]] VisualGraphRuntimeExecutionResult ExecuteCustomEvent(
        const VisualGraphRuntimeArtifact& artifact,
        std::string_view eventName,
        VisualGraphRuntimeExecutionContext& context) const;
    [[nodiscard]] VisualGraphRuntimeExecutionResult ExecuteCustomEvent(
        const VisualGraphRuntimeArtifact& artifact,
        std::string_view eventName,
        std::span<const VisualGraphCustomEventArgument> arguments,
        VisualGraphRuntimeExecutionContext& context) const;

private:
    using InstructionMap = std::unordered_map<std::uint32_t, const VisualGraphIrInstruction*>;
    using NodeSet = std::unordered_set<std::uint32_t>;

    [[nodiscard]] VisualGraphRuntimeExecutionResult ExecuteFunction(const VisualGraphIrFunction* function, VisualGraphRuntimeExecutionContext& context) const;
    // LIB-101: `stepBudget` is decremented once per ExecuteNode call
    // (regardless of whether it's a forward-flow or input-dependency
    // recursion) and shared by reference across the whole recursive walk —
    // the existing `executing` NodeSet only catches a node revisiting
    // itself while still ON THE CALL STACK (a data-dependency cycle); it
    // does NOT catch a CONTROL-FLOW loop (e.g. a Branch/Sequence wired back
    // to an already-executed ancestor), since `executing.erase(nodeId)`
    // runs before every forward-flow recursive call. stepBudget is the
    // watchdog for that gap — see ExecuteNode's own doc comment in the .cpp
    // for the full reasoning.
    [[nodiscard]] VisualGraphRuntimeExecutionResult ExecuteNode(
        const InstructionMap& instructions,
        std::uint32_t nodeId,
        VisualGraphRuntimeExecutionContext& context,
        NodeSet& executing,
        NodeSet& evaluatedNodes,
        bool followExecution,
        std::size_t& stepBudget,
        // LIB-101: C++ recursion depth of the DATA-INPUT resolution chain
        // (incremented only on the input-dependency recursion, NOT on the
        // iterative forward-flow walk). Bounds a pathologically deep data DAG
        // to a real stack budget so it fails with a diagnostic instead of a
        // STATUS_STACK_OVERFLOW — the gap stepBudget (a plain step COUNT)
        // cannot close, since 4096 nested frames overflow the stack long
        // before the count runs out.
        std::size_t depth) const;

    [[nodiscard]] const VisualGraphRuntimeBinding* FindBinding(const VisualGraphIrInstruction& instruction) const noexcept;
    [[nodiscard]] static bool TryExecuteBuiltInInstruction(const VisualGraphIrInstruction& instruction, VisualGraphRuntimeExecutionContext& context);
    [[nodiscard]] static VisualGraphRuntimeExecutionResult ValidateBindingSignature(const VisualGraphIrInstruction& instruction, const VisualGraphRuntimeBinding& binding);
    [[nodiscard]] static VisualGraphRuntimeExecutionResult ValidateProducedOutputs(
        const VisualGraphIrInstruction& instruction,
        const VisualGraphRuntimeBinding& binding,
        const VisualGraphRuntimeExecutionContext& context);
    [[nodiscard]] static const VisualGraphIrFunction* FindLifecycleFunction(const VisualGraphRuntimeArtifact& artifact, VisualGraphLifecycleEvent event) noexcept;
    [[nodiscard]] static const VisualGraphIrFunction* FindCustomEventFunction(const VisualGraphRuntimeArtifact& artifact, std::string_view eventName) noexcept;
    [[nodiscard]] static VisualGraphRuntimeExecutionResult StoreCustomEventArguments(
        const VisualGraphIrFunction& function,
        std::span<const VisualGraphCustomEventArgument> arguments,
        VisualGraphRuntimeExecutionContext& context);
    [[nodiscard]] static std::vector<VisualGraphEventArgument> CollectEventArguments(
        const VisualGraphIrInstruction& instruction,
        const VisualGraphRuntimeExecutionContext& context);
    [[nodiscard]] static kb::scene::SceneEntity CollectEventTarget(
        const VisualGraphIrInstruction& instruction,
        const VisualGraphRuntimeExecutionContext& context);
    [[nodiscard]] static InstructionMap BuildInstructionMap(const VisualGraphIrFunction& function);

    const VisualGraphRuntimeBindingRegistry& bindings_;
};

} // namespace kb::visual
