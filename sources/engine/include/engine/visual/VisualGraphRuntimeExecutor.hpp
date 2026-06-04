#pragma once

#include "engine/visual/VisualGraphRuntimeBindingRegistry.hpp"
#include "engine/visual/VisualGraphRuntimeRegistry.hpp"

#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kb::visual {

struct VisualGraphCustomEventArgument {
    std::string name;
    VisualGraphRuntimeValue value;
};

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
    [[nodiscard]] VisualGraphRuntimeExecutionResult ExecuteNode(
        const InstructionMap& instructions,
        std::uint32_t nodeId,
        VisualGraphRuntimeExecutionContext& context,
        NodeSet& executing,
        NodeSet& evaluatedNodes,
        bool followExecution) const;

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
    [[nodiscard]] static InstructionMap BuildInstructionMap(const VisualGraphIrFunction& function);

    const VisualGraphRuntimeBindingRegistry& bindings_;
};

} // namespace kb::visual
