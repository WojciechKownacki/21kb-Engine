#include "engine/visual/VisualGraphCompiler.hpp"

#include "engine/visual/VisualGraphValidator.hpp"

#include <algorithm>
#include <set>
#include <utility>

namespace kb::visual {
namespace {

void AddCompileError(VisualGraphCompileResult& result, std::uint32_t nodeId, std::string message) {
    result.errors.push_back(message);
    result.diagnostics.push_back(VisualGraphDiagnostics::Error(VisualGraphDiagnosticStage::Compile, nodeId, std::move(message)));
}

[[nodiscard]] VisualGraphIrOpcode OpcodeFor(VisualGraphNodeKind kind) noexcept {
    switch (kind) {
    case VisualGraphNodeKind::Sequence:
        return VisualGraphIrOpcode::Sequence;
    case VisualGraphNodeKind::Branch:
        return VisualGraphIrOpcode::Branch;
    case VisualGraphNodeKind::GetComponent:
        return VisualGraphIrOpcode::GetComponent;
    case VisualGraphNodeKind::GetProperty:
        return VisualGraphIrOpcode::GetProperty;
    case VisualGraphNodeKind::SetProperty:
        return VisualGraphIrOpcode::SetProperty;
    case VisualGraphNodeKind::CallNative:
        return VisualGraphIrOpcode::CallNative;
    case VisualGraphNodeKind::EmitEvent:
        return VisualGraphIrOpcode::EmitEvent;
    case VisualGraphNodeKind::Wait:
        return VisualGraphIrOpcode::Wait;
    case VisualGraphNodeKind::Event:
    case VisualGraphNodeKind::CustomEvent:
    case VisualGraphNodeKind::Comment:
        return VisualGraphIrOpcode::Sequence;
    }
    return VisualGraphIrOpcode::Sequence;
}

// LIB-061: Branch and CallNative are the only node kinds whose execution
// forks into two possible successors instead of a single linear "next".
// Branch forks on "true"/"false" (condition); CallNative forks on
// "then"/"failed" (success/failure of the underlying function call) —
// "then" keeps its pre-LIB-061 name so existing graphs' success-path
// wiring is unaffected, "failed" is new and optional.
[[nodiscard]] bool HasExecutionBranches(VisualGraphNodeKind kind) noexcept {
    return kind == VisualGraphNodeKind::Branch || kind == VisualGraphNodeKind::CallNative;
}

[[nodiscard]] std::string_view SuccessPinFor(VisualGraphNodeKind kind) noexcept {
    return kind == VisualGraphNodeKind::Branch ? "true" : "then";
}

[[nodiscard]] std::string_view FailurePinFor(VisualGraphNodeKind kind) noexcept {
    return kind == VisualGraphNodeKind::Branch ? "false" : "failed";
}

[[nodiscard]] std::uint32_t FirstOutgoingNode(const VisualGraphAsset& graph, std::uint32_t nodeId, std::string_view fromPin = "then") noexcept {
    const auto iter = std::ranges::find_if(graph.edges, [nodeId, fromPin](const VisualGraphEdge& edge) {
        return edge.kind == VisualGraphEdgeKind::Execution && edge.fromNode == nodeId && (edge.fromPin.empty() || edge.fromPin == fromPin);
    });
    return iter == graph.edges.end() ? 0U : iter->toNode;
}

[[nodiscard]] std::vector<VisualGraphIrInput> BuildInstructionInputs(const VisualGraphAsset& graph, std::uint32_t nodeId) {
    std::vector<VisualGraphIrInput> inputs;
    for (const VisualGraphEdge& edge : graph.edges) {
        if (edge.kind != VisualGraphEdgeKind::Data || edge.toNode != nodeId) {
            continue;
        }
        const VisualGraphPin* targetPin = graph.FindPin(edge.toNode, edge.toPin, VisualGraphPinDirection::Input);
        inputs.push_back(VisualGraphIrInput{
            .name = edge.toPin,
            .type = targetPin == nullptr ? VisualGraphValueType::Void : targetPin->type,
            .sourceNodeId = edge.fromNode,
            .sourcePin = edge.fromPin,
        });
    }
    return inputs;
}

[[nodiscard]] std::vector<VisualGraphIrOutput> BuildInstructionOutputs(const VisualGraphAsset& graph, std::uint32_t nodeId) {
    std::vector<VisualGraphIrOutput> outputs;
    for (const VisualGraphPin& pin : graph.pins) {
        if (pin.nodeId != nodeId || pin.direction != VisualGraphPinDirection::Output || pin.type == VisualGraphValueType::Void) {
            continue;
        }
        outputs.push_back(VisualGraphIrOutput{
            .name = pin.name,
            .type = pin.type,
        });
    }
    return outputs;
}

void EmitInstructionWithDataDependencies(
    const VisualGraphAsset& graph,
    const VisualGraphNode& node,
    VisualGraphIrFunction& function,
    std::set<std::uint32_t>& emitted,
    std::set<std::uint32_t>& emitting,
    VisualGraphCompileResult& result) {
    if (emitted.contains(node.id)) {
        return;
    }
    if (!emitting.insert(node.id).second) {
        AddCompileError(result, node.id, "data dependency cycle detected at node " + std::to_string(node.id));
        return;
    }

    for (const VisualGraphEdge& edge : graph.edges) {
        if (edge.kind != VisualGraphEdgeKind::Data || edge.toNode != node.id) {
            continue;
        }
        const VisualGraphNode* sourceNode = graph.FindNode(edge.fromNode);
        if (sourceNode == nullptr) {
            AddCompileError(result, edge.fromNode, "data dependency references missing node " + std::to_string(edge.fromNode));
            continue;
        }
        EmitInstructionWithDataDependencies(graph, *sourceNode, function, emitted, emitting, result);
    }

    if (node.kind != VisualGraphNodeKind::Comment && node.kind != VisualGraphNodeKind::Event && node.kind != VisualGraphNodeKind::CustomEvent) {
        function.instructions.push_back(VisualGraphIrInstruction{
            .opcode = OpcodeFor(node.kind),
            .sourceNodeId = node.id,
            .symbol = node.symbol,
            .inputs = BuildInstructionInputs(graph, node.id),
            .outputs = BuildInstructionOutputs(graph, node.id),
            .nextNodeId = FirstOutgoingNode(graph, node.id),
            .trueNodeId = HasExecutionBranches(node.kind) ? FirstOutgoingNode(graph, node.id, SuccessPinFor(node.kind)) : 0U,
            .falseNodeId = HasExecutionBranches(node.kind) ? FirstOutgoingNode(graph, node.id, FailurePinFor(node.kind)) : 0U,
        });
    }

    emitting.erase(node.id);
    emitted.insert(node.id);
}

[[nodiscard]] bool IsEntryNode(const VisualGraphNode& node) noexcept {
    return node.kind == VisualGraphNodeKind::Event || node.kind == VisualGraphNodeKind::CustomEvent;
}

void CompileEntryFunction(const VisualGraphAsset& graph, const VisualGraphNode& eventNode, VisualGraphCompileResult& result) {
    VisualGraphIrFunction function{};
    function.event = eventNode.lifecycle;
    function.eventNodeId = eventNode.id;
    function.eventOutputs = BuildInstructionOutputs(graph, eventNode.id);
    if (eventNode.kind == VisualGraphNodeKind::CustomEvent) {
        function.customEventName = eventNode.symbol;
    }
    function.entryNodeId = FirstOutgoingNode(graph, eventNode.id);
    std::set<std::uint32_t> visited;
    std::set<std::uint32_t> emitted;
    std::set<std::uint32_t> emitting;
    std::vector<std::uint32_t> pending;

    std::uint32_t currentNodeId = function.entryNodeId;
    while (currentNodeId != 0U || !pending.empty()) {
        if (currentNodeId == 0U) {
            currentNodeId = pending.back();
            pending.pop_back();
        }
        if (!visited.insert(currentNodeId).second) {
            currentNodeId = 0U;
            continue;
        }
        const VisualGraphNode* node = graph.FindNode(currentNodeId);
        if (node == nullptr) {
            AddCompileError(result, currentNodeId, "execution chain references missing node " + std::to_string(currentNodeId));
            break;
        }

        const std::uint32_t nextNodeId = FirstOutgoingNode(graph, node->id);
        const std::uint32_t trueNodeId = HasExecutionBranches(node->kind) ? FirstOutgoingNode(graph, node->id, SuccessPinFor(node->kind)) : 0U;
        const std::uint32_t falseNodeId = HasExecutionBranches(node->kind) ? FirstOutgoingNode(graph, node->id, FailurePinFor(node->kind)) : 0U;
        EmitInstructionWithDataDependencies(graph, *node, function, emitted, emitting, result);

        if (falseNodeId != 0U) {
            pending.push_back(falseNodeId);
        }
        if (trueNodeId != 0U) {
            pending.push_back(trueNodeId);
        }
        currentNodeId = nextNodeId;
    }

    result.module.functions.push_back(std::move(function));
}

} // namespace

VisualGraphCompileResult VisualGraphCompiler::Compile(const VisualGraphAsset& graph) {
    VisualGraphCompileResult result{};
    result.module.graphName = graph.name;
    result.module.variables = graph.variables;

    VisualGraphValidationResult validation = VisualGraphValidator::Validate(graph);
    if (!validation.Succeeded()) {
        result.errors = std::move(validation.errors);
        result.diagnostics = std::move(validation.diagnostics);
        return result;
    }

    bool hasEvent = false;
    for (const VisualGraphNode& node : graph.nodes) {
        if (IsEntryNode(node)) {
            hasEvent = true;
            CompileEntryFunction(graph, node, result);
        }
    }
    if (!hasEvent) {
        result.errors.push_back("visual graph has no event entry nodes");
        result.diagnostics.push_back(VisualGraphDiagnostics::Error(VisualGraphDiagnosticStage::Compile, result.errors.back()));
    }
    return result;
}

const char* ToString(VisualGraphIrOpcode opcode) noexcept {
    switch (opcode) {
    case VisualGraphIrOpcode::Sequence:
        return "Sequence";
    case VisualGraphIrOpcode::Branch:
        return "Branch";
    case VisualGraphIrOpcode::GetComponent:
        return "GetComponent";
    case VisualGraphIrOpcode::GetProperty:
        return "GetProperty";
    case VisualGraphIrOpcode::SetProperty:
        return "SetProperty";
    case VisualGraphIrOpcode::CallNative:
        return "CallNative";
    case VisualGraphIrOpcode::EmitEvent:
        return "EmitEvent";
    case VisualGraphIrOpcode::Wait:
        return "Wait";
    }
    return "Sequence";
}

} // namespace kb::visual
