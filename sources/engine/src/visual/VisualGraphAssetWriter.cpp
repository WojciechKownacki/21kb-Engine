#include "engine/visual/VisualGraphAssetWriter.hpp"

#include <ostream>
#include <sstream>

namespace kb::visual {

std::string VisualGraphAssetWriter::WriteToString(const VisualGraphAsset& graph) {
    std::ostringstream output;
    Write(output, graph);
    return output.str();
}

void VisualGraphAssetWriter::Write(std::ostream& output, const VisualGraphAsset& graph) {
    output << "kbgraph " << graph.version << "\n";
    output << "name " << graph.name << "\n";
    for (const VisualGraphVariable& variable : graph.variables) {
        output << "variable " << variable.name << " " << ToString(variable.type);
        if (!variable.defaultValue.empty()) {
            output << " " << variable.defaultValue;
        }
        output << "\n";
    }
    for (const VisualGraphNode& node : graph.nodes) {
        output << "node " << node.id << " " << ToString(node.kind);
        if (node.kind == VisualGraphNodeKind::Event) {
            output << " " << ToString(node.lifecycle);
        } else if (node.kind == VisualGraphNodeKind::CustomEvent) {
            output << " " << node.symbol;
        } else if (!node.symbol.empty()) {
            output << " " << node.symbol;
        }
        output << "\n";
    }
    for (const VisualGraphPin& pin : graph.pins) {
        output << "pin " << pin.nodeId << " " << ToString(pin.direction) << " " << pin.name << " " << ToString(pin.type) << "\n";
    }
    for (const VisualGraphEdge& edge : graph.edges) {
        if (edge.kind == VisualGraphEdgeKind::Execution && edge.fromPin.empty() && edge.toPin.empty()) {
            output << "edge " << edge.fromNode << " " << edge.toNode << "\n";
        } else {
            output << "edge " << ToString(edge.kind) << " " << edge.fromNode << " " << edge.fromPin << " " << edge.toNode << " " << edge.toPin << "\n";
        }
    }
}

} // namespace kb::visual
