#include "engine/visual/VisualGraphDocument.hpp"

#include <algorithm>
#include <ranges>
#include <utility>

namespace kb::visual {

VisualGraphDocument::VisualGraphDocument() {
    graph_.name = "VisualGraph";
}

VisualGraphDocument::VisualGraphDocument(VisualGraphAsset graph)
    : graph_(std::move(graph)) {}

const VisualGraphAsset& VisualGraphDocument::Graph() const noexcept {
    return graph_;
}

VisualGraphAsset& VisualGraphDocument::MutableGraph() noexcept {
    return graph_;
}

std::uint32_t VisualGraphDocument::AddNode(const VisualGraphAddNodeDesc& desc, const VisualGraphNodeDefinitionRegistry& definitions) {
    VisualGraphNode node{
        .id = AllocateNodeId(),
        .kind = desc.kind,
        .lifecycle = desc.lifecycle,
        .symbol = desc.symbol,
    };
    const std::uint32_t nodeId = node.id;
    graph_.nodes.push_back(node);
    AddPinsForNode(node, definitions);
    return nodeId;
}

std::uint32_t VisualGraphDocument::AddNode(const VisualGraphNodeCatalogEntry& entry) {
    VisualGraphNode node{
        .id = AllocateNodeId(),
        .kind = entry.kind,
        .lifecycle = entry.lifecycle,
        .symbol = entry.symbol,
    };
    const std::uint32_t nodeId = node.id;
    graph_.nodes.push_back(node);
    AddPinsForNode(nodeId, entry.pins);
    return nodeId;
}

bool VisualGraphDocument::AddVariable(VisualGraphVariable variable) {
    if (variable.name.empty() || variable.type == VisualGraphValueType::Void) {
        return false;
    }
    const auto sameName = [&variable](const VisualGraphVariable& existing) {
        return existing.name == variable.name;
    };
    if (std::ranges::find_if(graph_.variables, sameName) != graph_.variables.end()) {
        return false;
    }
    graph_.variables.push_back(std::move(variable));
    return true;
}

bool VisualGraphDocument::ConnectExecution(std::uint32_t fromNode, std::string fromPin, std::uint32_t toNode, std::string toPin) {
    if (!CanConnect(fromNode, fromPin, toNode, toPin, VisualGraphEdgeKind::Execution)) {
        return false;
    }
    graph_.edges.push_back(VisualGraphEdge{
        .fromNode = fromNode,
        .fromPin = std::move(fromPin),
        .toNode = toNode,
        .toPin = std::move(toPin),
        .kind = VisualGraphEdgeKind::Execution,
    });
    return true;
}

bool VisualGraphDocument::ConnectData(std::uint32_t fromNode, std::string fromPin, std::uint32_t toNode, std::string toPin) {
    if (!CanConnect(fromNode, fromPin, toNode, toPin, VisualGraphEdgeKind::Data)) {
        return false;
    }
    graph_.edges.push_back(VisualGraphEdge{
        .fromNode = fromNode,
        .fromPin = std::move(fromPin),
        .toNode = toNode,
        .toPin = std::move(toPin),
        .kind = VisualGraphEdgeKind::Data,
    });
    return true;
}

bool VisualGraphDocument::CanConnect(std::uint32_t fromNode, std::string_view fromPin, std::uint32_t toNode, std::string_view toPin, VisualGraphEdgeKind kind) const noexcept {
    if (graph_.FindNode(fromNode) == nullptr || graph_.FindNode(toNode) == nullptr || HasEdge(fromNode, fromPin, toNode, toPin, kind)) {
        return false;
    }

    const VisualGraphPin* outputPin = graph_.FindPin(fromNode, fromPin, VisualGraphPinDirection::Output);
    const VisualGraphPin* inputPin = graph_.FindPin(toNode, toPin, VisualGraphPinDirection::Input);
    if (outputPin == nullptr || inputPin == nullptr) {
        return false;
    }

    if (kind == VisualGraphEdgeKind::Execution) {
        return outputPin->type == VisualGraphValueType::Void && inputPin->type == VisualGraphValueType::Void && !HasExecutionOutputConnection(fromNode, fromPin);
    }

    return outputPin->type != VisualGraphValueType::Void && inputPin->type != VisualGraphValueType::Void && outputPin->type == inputPin->type;
}

bool VisualGraphDocument::HasExecutionOutputConnection(std::uint32_t fromNode, std::string_view fromPin) const noexcept {
    return std::ranges::find_if(graph_.edges, [fromNode, fromPin](const VisualGraphEdge& edge) {
        return edge.kind == VisualGraphEdgeKind::Execution && edge.fromNode == fromNode && edge.fromPin == fromPin;
    }) != graph_.edges.end();
}

bool VisualGraphDocument::HasEdge(std::uint32_t fromNode, std::string_view fromPin, std::uint32_t toNode, std::string_view toPin, VisualGraphEdgeKind kind) const noexcept {
    return std::ranges::find_if(graph_.edges, [fromNode, fromPin, toNode, toPin, kind](const VisualGraphEdge& edge) {
        return edge.kind == kind && edge.fromNode == fromNode && edge.fromPin == fromPin && edge.toNode == toNode && edge.toPin == toPin;
    }) != graph_.edges.end();
}

bool VisualGraphDocument::RemoveNode(std::uint32_t nodeId) {
    const auto nodeIter = std::ranges::find_if(graph_.nodes, [nodeId](const VisualGraphNode& node) {
        return node.id == nodeId;
    });
    if (nodeIter == graph_.nodes.end()) {
        return false;
    }

    graph_.nodes.erase(nodeIter);
    std::erase_if(graph_.pins, [nodeId](const VisualGraphPin& pin) {
        return pin.nodeId == nodeId;
    });
    std::erase_if(graph_.edges, [nodeId](const VisualGraphEdge& edge) {
        return edge.fromNode == nodeId || edge.toNode == nodeId;
    });
    return true;
}

std::uint32_t VisualGraphDocument::AllocateNodeId() const noexcept {
    std::uint32_t nextId = 1U;
    for (const VisualGraphNode& node : graph_.nodes) {
        nextId = std::max(nextId, node.id + 1U);
    }
    return nextId;
}

void VisualGraphDocument::AddPinsForNode(const VisualGraphNode& node, const VisualGraphNodeDefinitionRegistry& definitions) {
    std::vector<VisualGraphPin> pins = definitions.CreatePinsForNode(node);
    graph_.pins.insert(graph_.pins.end(), pins.begin(), pins.end());
}

void VisualGraphDocument::AddPinsForNode(std::uint32_t nodeId, const std::vector<VisualGraphPinTemplate>& pinTemplates) {
    graph_.pins.reserve(graph_.pins.size() + pinTemplates.size());
    for (const VisualGraphPinTemplate& pinTemplate : pinTemplates) {
        graph_.pins.push_back(VisualGraphPin{
            .nodeId = nodeId,
            .direction = pinTemplate.direction,
            .name = pinTemplate.name,
            .type = pinTemplate.type,
        });
    }
}

} // namespace kb::visual
