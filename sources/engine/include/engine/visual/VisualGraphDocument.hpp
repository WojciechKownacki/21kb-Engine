#pragma once

#include "engine/visual/VisualGraphNodeCatalog.hpp"
#include "engine/visual/VisualGraphNodeDefinitionRegistry.hpp"
#include "engine/visual/VisualGraphTypes.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace kb::visual {

struct VisualGraphAddNodeDesc {
    VisualGraphNodeKind kind = VisualGraphNodeKind::Comment;
    VisualGraphLifecycleEvent lifecycle = VisualGraphLifecycleEvent::Tick;
    std::string symbol;
};

class VisualGraphDocument final {
public:
    VisualGraphDocument();
    explicit VisualGraphDocument(VisualGraphAsset graph);

    [[nodiscard]] const VisualGraphAsset& Graph() const noexcept;
    [[nodiscard]] VisualGraphAsset& MutableGraph() noexcept;

    [[nodiscard]] std::uint32_t AddNode(const VisualGraphAddNodeDesc& desc, const VisualGraphNodeDefinitionRegistry& definitions);
    [[nodiscard]] std::uint32_t AddNode(const VisualGraphNodeCatalogEntry& entry);
    [[nodiscard]] bool AddVariable(VisualGraphVariable variable);
    [[nodiscard]] bool ConnectExecution(std::uint32_t fromNode, std::string fromPin, std::uint32_t toNode, std::string toPin);
    [[nodiscard]] bool ConnectData(std::uint32_t fromNode, std::string fromPin, std::uint32_t toNode, std::string toPin);
    [[nodiscard]] bool RemoveNode(std::uint32_t nodeId);

private:
    [[nodiscard]] bool CanConnect(std::uint32_t fromNode, std::string_view fromPin, std::uint32_t toNode, std::string_view toPin, VisualGraphEdgeKind kind) const noexcept;
    [[nodiscard]] bool HasExecutionOutputConnection(std::uint32_t fromNode, std::string_view fromPin) const noexcept;
    [[nodiscard]] bool HasEdge(std::uint32_t fromNode, std::string_view fromPin, std::uint32_t toNode, std::string_view toPin, VisualGraphEdgeKind kind) const noexcept;
    [[nodiscard]] std::uint32_t AllocateNodeId() const noexcept;
    void AddPinsForNode(const VisualGraphNode& node, const VisualGraphNodeDefinitionRegistry& definitions);
    void AddPinsForNode(std::uint32_t nodeId, const std::vector<VisualGraphPinTemplate>& pinTemplates);

    VisualGraphAsset graph_;
};

} // namespace kb::visual
