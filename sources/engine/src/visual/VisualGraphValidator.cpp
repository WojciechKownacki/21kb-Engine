#include "engine/visual/VisualGraphValidator.hpp"

#include <set>
#include <string>
#include <utility>

namespace kb::visual {
namespace {

void AddValidationError(VisualGraphValidationResult& result, std::string message) {
    result.errors.push_back(message);
    result.diagnostics.push_back(VisualGraphDiagnostics::Error(VisualGraphDiagnosticStage::Validation, std::move(message)));
}

void AddValidationError(VisualGraphValidationResult& result, std::uint32_t nodeId, std::string message) {
    result.errors.push_back(message);
    result.diagnostics.push_back(VisualGraphDiagnostics::Error(VisualGraphDiagnosticStage::Validation, nodeId, std::move(message)));
}

void AddValidationError(VisualGraphValidationResult& result, std::uint32_t nodeId, std::string_view pinName, std::string message) {
    result.errors.push_back(message);
    result.diagnostics.push_back(VisualGraphDiagnostics::Error(VisualGraphDiagnosticStage::Validation, nodeId, std::string{pinName}, std::move(message)));
}

void ValidateVariables(const VisualGraphAsset& graph, VisualGraphValidationResult& result) {
    std::set<std::string> variableNames;
    for (const VisualGraphVariable& variable : graph.variables) {
        if (variable.name.empty()) {
            AddValidationError(result, "variable name is empty");
        } else if (!variableNames.insert(variable.name).second) {
            AddValidationError(result, "duplicate variable '" + variable.name + "'");
        }
        if (variable.type == VisualGraphValueType::Void) {
            AddValidationError(result, "variable '" + variable.name + "' cannot use Void type");
        }
    }
}

void ValidateNodes(const VisualGraphAsset& graph, VisualGraphValidationResult& result, std::set<std::uint32_t>& nodeIds) {
    std::set<VisualGraphLifecycleEvent> lifecycleEvents;
    std::set<std::string> customEvents;
    for (const VisualGraphNode& node : graph.nodes) {
        if (node.id == 0U) {
            AddValidationError(result, node.id, "node id 0 is reserved");
        }
        if (!nodeIds.insert(node.id).second) {
            AddValidationError(result, node.id, "duplicate node id " + std::to_string(node.id));
        }
        if (node.kind == VisualGraphNodeKind::Event) {
            if (!lifecycleEvents.insert(node.lifecycle).second) {
                AddValidationError(result, node.id, "duplicate lifecycle event node " + std::string{ToString(node.lifecycle)});
            }
        } else if (node.kind == VisualGraphNodeKind::CustomEvent) {
            if (node.symbol.empty()) {
                AddValidationError(result, node.id, "custom event node requires a symbol");
            } else if (!customEvents.insert(node.symbol).second) {
                AddValidationError(result, node.id, "duplicate custom event node " + node.symbol);
            }
        } else if (node.kind != VisualGraphNodeKind::Sequence && node.kind != VisualGraphNodeKind::Branch && node.kind != VisualGraphNodeKind::Wait && node.kind != VisualGraphNodeKind::Comment &&
                   node.symbol.empty()) {
            AddValidationError(result, node.id, "node " + std::to_string(node.id) + " requires a symbol");
        }
    }
}

void ValidatePins(const VisualGraphAsset& graph, VisualGraphValidationResult& result) {
    std::set<std::string> pinKeys;
    for (const VisualGraphPin& pin : graph.pins) {
        if (graph.FindNode(pin.nodeId) == nullptr) {
            AddValidationError(result, pin.nodeId, pin.name, "pin '" + pin.name + "' references missing node " + std::to_string(pin.nodeId));
        }
        if (pin.name.empty()) {
            AddValidationError(result, pin.nodeId, pin.name, "pin name is empty on node " + std::to_string(pin.nodeId));
        }
        const std::string key = std::to_string(pin.nodeId) + ":" + ToString(pin.direction) + ":" + pin.name;
        if (!pinKeys.insert(key).second) {
            AddValidationError(result, pin.nodeId, pin.name, "duplicate pin '" + pin.name + "' on node " + std::to_string(pin.nodeId));
        }
    }
}

void ValidateEdgePins(const VisualGraphAsset& graph, const VisualGraphEdge& edge, VisualGraphValidationResult& result) {
    const VisualGraphPin* outputPin = edge.fromPin.empty() ? nullptr : graph.FindPin(edge.fromNode, edge.fromPin, VisualGraphPinDirection::Output);
    const VisualGraphPin* inputPin = edge.toPin.empty() ? nullptr : graph.FindPin(edge.toNode, edge.toPin, VisualGraphPinDirection::Input);
    if (!edge.fromPin.empty() && outputPin == nullptr) {
        AddValidationError(result, edge.fromNode, edge.fromPin, "edge references missing output pin '" + edge.fromPin + "' on node " + std::to_string(edge.fromNode));
    }
    if (!edge.toPin.empty() && inputPin == nullptr) {
        AddValidationError(result, edge.toNode, edge.toPin, "edge references missing input pin '" + edge.toPin + "' on node " + std::to_string(edge.toNode));
    }
    if (edge.kind == VisualGraphEdgeKind::Data && (edge.fromPin.empty() || edge.toPin.empty())) {
        AddValidationError(result, edge.toNode, edge.toPin, "data edge from node " + std::to_string(edge.fromNode) + " to node " + std::to_string(edge.toNode) + " requires pins");
    }
    if (edge.kind == VisualGraphEdgeKind::Data && outputPin != nullptr && inputPin != nullptr) {
        if (outputPin->type == VisualGraphValueType::Void || inputPin->type == VisualGraphValueType::Void) {
            AddValidationError(result, edge.toNode, edge.toPin, "data edge from node " + std::to_string(edge.fromNode) + " to node " + std::to_string(edge.toNode) + " cannot use Void pins");
        } else if (!IsImplicitVisualGraphValueConversion(outputPin->type, inputPin->type)) {
            const VisualGraphValueConversion conversion = ClassifyVisualGraphValueConversion(outputPin->type, inputPin->type);
            const char* reason = conversion == VisualGraphValueConversion::Lossy ? "requires an explicit lossy conversion node" : "uses incompatible types";
            AddValidationError(result, edge.toNode, edge.toPin,
                "data edge from " + std::string{ToString(outputPin->type)} + " to " + ToString(inputPin->type) + " " + reason);
        }
    }
    if (edge.kind == VisualGraphEdgeKind::Execution) {
        if (outputPin != nullptr && outputPin->type != VisualGraphValueType::Void) {
            AddValidationError(result, edge.fromNode, edge.fromPin, "execution edge output pin '" + edge.fromPin + "' must be Void");
        }
        if (inputPin != nullptr && inputPin->type != VisualGraphValueType::Void) {
            AddValidationError(result, edge.toNode, edge.toPin, "execution edge input pin '" + edge.toPin + "' must be Void");
        }
    }
}

void ValidateEdges(const VisualGraphAsset& graph, VisualGraphValidationResult& result) {
    std::set<std::string> executionOutputs;
    for (const VisualGraphEdge& edge : graph.edges) {
        if (graph.FindNode(edge.fromNode) == nullptr) {
            AddValidationError(result, edge.fromNode, "edge references missing source node " + std::to_string(edge.fromNode));
        }
        if (graph.FindNode(edge.toNode) == nullptr) {
            AddValidationError(result, edge.toNode, "edge references missing target node " + std::to_string(edge.toNode));
        }
        ValidateEdgePins(graph, edge, result);

        if (edge.kind == VisualGraphEdgeKind::Execution) {
            const std::string outputPin = edge.fromPin.empty() ? "then" : edge.fromPin;
            const std::string key = std::to_string(edge.fromNode) + ":" + outputPin;
            if (!executionOutputs.insert(key).second) {
                AddValidationError(result, edge.fromNode, outputPin, "execution output '" + outputPin + "' on node " + std::to_string(edge.fromNode) + " has multiple targets");
            }
        }
    }
}

void VisitAcyclicEdgeGraph(
    const VisualGraphAsset& graph,
    VisualGraphEdgeKind edgeKind,
    std::string_view edgeLabel,
    std::uint32_t nodeId,
    std::set<std::uint32_t>& visiting,
    std::set<std::uint32_t>& visited,
    VisualGraphValidationResult& result) {
    if (visited.contains(nodeId)) {
        return;
    }
    if (!visiting.insert(nodeId).second) {
        AddValidationError(result, nodeId, std::string{edgeLabel} + " cycle detected at node " + std::to_string(nodeId));
        return;
    }

    for (const VisualGraphEdge& edge : graph.edges) {
        if (edge.kind != edgeKind || edge.fromNode != nodeId || graph.FindNode(edge.toNode) == nullptr) {
            continue;
        }
        VisitAcyclicEdgeGraph(graph, edgeKind, edgeLabel, edge.toNode, visiting, visited, result);
    }

    visiting.erase(nodeId);
    visited.insert(nodeId);
}

void ValidateAcyclicEdges(const VisualGraphAsset& graph, VisualGraphEdgeKind edgeKind, std::string_view edgeLabel, VisualGraphValidationResult& result) {
    std::set<std::uint32_t> visited;
    for (const VisualGraphNode& node : graph.nodes) {
        std::set<std::uint32_t> visiting;
        VisitAcyclicEdgeGraph(graph, edgeKind, edgeLabel, node.id, visiting, visited, result);
    }
}

} // namespace

VisualGraphValidationResult VisualGraphValidator::Validate(const VisualGraphAsset& graph) {
    VisualGraphValidationResult result{};
    if (graph.version != VisualGraphAsset::kCurrentVersion) {
        AddValidationError(result, "unsupported kbgraph version " + std::to_string(graph.version));
    }

    ValidateVariables(graph, result);

    std::set<std::uint32_t> nodeIds;
    ValidateNodes(graph, result, nodeIds);
    ValidatePins(graph, result);
    ValidateEdges(graph, result);
    ValidateAcyclicEdges(graph, VisualGraphEdgeKind::Execution, "execution", result);
    ValidateAcyclicEdges(graph, VisualGraphEdgeKind::Data, "data dependency", result);

    if (graph.nodes.empty()) {
        AddValidationError(result, "visual graph has no nodes");
    }
    return result;
}

} // namespace kb::visual
