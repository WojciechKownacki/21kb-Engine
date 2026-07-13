#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace kb::render {
namespace {

void AddGraphDiagnostic(
    std::vector<RenderMaterialGraphDiagnostic>& diagnostics,
    RenderMaterialGraphDiagnosticSeverity severity,
    RenderMaterialGraphDiagnosticKind kind,
    std::uint32_t nodeId,
    std::uint32_t linkId,
    std::string_view pin,
    std::string message) {
    diagnostics.push_back(RenderMaterialGraphDiagnostic{
        .severity = severity,
.kind = kind,
.nodeId = nodeId,
.linkId = linkId,
.pin = std::string{ pin },
.message = std::move(message),
    });
}

struct GraphEndpoint {
    std::uint32_t nodeId = 0U;
    std::string pin;
};

struct FunctionEndpointSignature {
    std::string name;
    RenderMaterialGraphPinType type = RenderMaterialGraphPinType::Float4;
    std::uint32_t nodeId = 0U;
};

struct FunctionSignature {
    std::vector<FunctionEndpointSignature> inputs;
    std::vector<FunctionEndpointSignature> outputs;
};

[[nodiscard]] RenderMaterialGraphPinType FunctionEndpointPinType(const RenderMaterialGraphNode& node) noexcept {
    if (const std::optional<RenderMaterialGraphPinType> parsed = ParseRenderMaterialGraphPinType(node.parameter.defaultValueHint)) {
        if (*parsed != RenderMaterialGraphPinType::Unknown) {
            return *parsed;
        }
    }
    return RenderMaterialGraphPinType::Float4;
}

[[nodiscard]] bool HasGraphDiagnosticError(std::span<const RenderMaterialGraphDiagnostic> diagnostics) noexcept {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const RenderMaterialGraphDiagnostic& diagnostic) {
        return diagnostic.severity == RenderMaterialGraphDiagnosticSeverity::Error;
    });
}

[[nodiscard]] bool GraphContainsNodeKind(
    const RenderMaterialGraphDocument& graph,
    RenderMaterialGraphNodeKind kind) noexcept {
    return std::any_of(graph.nodes.begin(), graph.nodes.end(), [kind](const RenderMaterialGraphNode& node) {
        return node.kind == kind;
    });
}

[[nodiscard]] bool ParseUInt64Token(std::string_view text, std::uint64_t& output) noexcept {
    if (text.empty() || text == "_") {
        return false;
    }
    std::uint64_t value = 0U;
    for (const char ch : text) {
        if (ch < '0' || ch > '9') {
            return false;
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(ch - '0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10ULL) {
            return false;
        }
        value = value * 10ULL + digit;
    }
    output = value;
    return true;
}

[[nodiscard]] std::uint64_t MaterialFunctionCallAssetId(const RenderMaterialGraphNode& node) noexcept {
    std::uint64_t assetId = 0U;
    if (ParseUInt64Token(node.parameter.stableId, assetId) && assetId != 0U) {
        return assetId;
    }
    if (ParseUInt64Token(node.parameter.defaultValueHint, assetId) && assetId != 0U) {
        return assetId;
    }
    return 0U;
}

[[nodiscard]] std::string FunctionEndpointName(
    const RenderMaterialGraphNode& node,
    std::string_view prefix,
    std::size_t index) {
    if (!node.parameter.stableId.empty()) {
        return node.parameter.stableId;
    }
    if (!node.parameter.displayName.empty()) {
        return node.parameter.displayName;
    }
    return std::string{ prefix } + std::to_string(index + 1U);
}

[[nodiscard]] FunctionSignature BuildMaterialFunctionSignature(const RenderMaterialGraphDocument& graph) {
    FunctionSignature signature;
    for (const RenderMaterialGraphNode& node : graph.nodes) {
        if (node.kind == RenderMaterialGraphNodeKind::FunctionInput) {
            signature.inputs.push_back(FunctionEndpointSignature{
                .type = FunctionEndpointPinType(node),
                .nodeId = node.id,
            });
        } else if (node.kind == RenderMaterialGraphNodeKind::FunctionOutput) {
            signature.outputs.push_back(FunctionEndpointSignature{
                .type = FunctionEndpointPinType(node),
                .nodeId = node.id,
            });
        }
    }
    const auto order = [](const FunctionEndpointSignature& lhs, const FunctionEndpointSignature& rhs) {
        if (lhs.nodeId != rhs.nodeId) {
            return lhs.nodeId < rhs.nodeId;
        }
        return lhs.name < rhs.name;
    };
    std::sort(signature.inputs.begin(), signature.inputs.end(), order);
    std::sort(signature.outputs.begin(), signature.outputs.end(), order);
    for (std::size_t index = 0U; index < signature.inputs.size(); ++index) {
        if (const RenderMaterialGraphNode* node = FindRenderMaterialGraphNode(graph, signature.inputs[index].nodeId)) {
            signature.inputs[index].name = FunctionEndpointName(*node, "input", index);
        }
    }
    for (std::size_t index = 0U; index < signature.outputs.size(); ++index) {
        if (const RenderMaterialGraphNode* node = FindRenderMaterialGraphNode(graph, signature.outputs[index].nodeId)) {
            signature.outputs[index].name = FunctionEndpointName(*node, "output", index);
        }
    }
    return signature;
}

} // namespace

RenderMaterialGraphCustomCode BuildRenderMaterialFunctionCallCustomCode(const RenderMaterialGraphDocument& functionGraph) {
    const FunctionSignature signature = BuildMaterialFunctionSignature(functionGraph);
    RenderMaterialGraphCustomCode customCode{};
    customCode.body.clear();
    customCode.inputs.clear();
    customCode.outputs.clear();
    customCode.outputType = signature.outputs.empty() ? RenderMaterialGraphPinType::Float4 : signature.outputs.front().type;
    customCode.inputs.reserve(signature.inputs.size());
    for (const FunctionEndpointSignature& input : signature.inputs) {
        customCode.inputs.push_back(RenderMaterialGraphCustomPin{
            .name = input.name,
            .type = input.type,
        });
    }
    customCode.outputs.reserve(signature.outputs.size());
    for (const FunctionEndpointSignature& output : signature.outputs) {
        customCode.outputs.push_back(RenderMaterialGraphCustomPin{
            .name = output.name,
            .type = output.type,
        });
    }
    return customCode;
}

namespace {

[[nodiscard]] bool PinListsMatchFunctionSignature(
    const std::vector<RenderMaterialGraphCustomPin>& callPins,
    std::span<const FunctionEndpointSignature> functionPins) noexcept {
    if (callPins.size() != functionPins.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < callPins.size(); ++index) {
        if (callPins[index].name != functionPins[index].name ||
            callPins[index].type != functionPins[index].type) {
            return false;
        }
    }
    return true;
}

void AddMaterialFunctionDiagnostic(
    std::vector<RenderMaterialGraphDiagnostic>& diagnostics,
    RenderMaterialGraphDiagnosticKind kind,
    const RenderMaterialGraphNode& callNode,
    std::string pin,
    std::string message) {
    AddGraphDiagnostic(
        diagnostics,
        RenderMaterialGraphDiagnosticSeverity::Error,
        kind,
        callNode.id,
        0U,
        std::move(pin),
        std::move(message));
}

void AddLayerStackDiagnostic(
    std::vector<RenderMaterialGraphDiagnostic>& diagnostics,
    const RenderMaterialGraphNode& layerStackNode,
    std::string message) {
    AddGraphDiagnostic(
        diagnostics,
        RenderMaterialGraphDiagnosticSeverity::Error,
        RenderMaterialGraphDiagnosticKind::MissingMaterialFunction,
        layerStackNode.id,
        0U,
        "attributes",
        std::move(message));
}

[[nodiscard]] bool IsLayerStackParameterTypeSupported(RenderMaterialGraphPinType type) noexcept {
    switch (type) {
    case RenderMaterialGraphPinType::Float:
    case RenderMaterialGraphPinType::Float2:
    case RenderMaterialGraphPinType::Float3:
    case RenderMaterialGraphPinType::Float4:
    case RenderMaterialGraphPinType::Color:
    case RenderMaterialGraphPinType::Normal:
        return true;
    case RenderMaterialGraphPinType::Unknown:
    case RenderMaterialGraphPinType::Texture2D:
    case RenderMaterialGraphPinType::TextureCube:
    case RenderMaterialGraphPinType::Texture3D:
    case RenderMaterialGraphPinType::Texture2DArray:
    case RenderMaterialGraphPinType::Sampler:
    case RenderMaterialGraphPinType::MaterialAttributes:
        return false;
    }
    return false;
}

[[nodiscard]] std::string DefaultLayerStackParameterHint(RenderMaterialGraphPinType type) {
    switch (type) {
    case RenderMaterialGraphPinType::Float:
        return "0";
    case RenderMaterialGraphPinType::Float2:
        return "0 0";
    case RenderMaterialGraphPinType::Float3:
    case RenderMaterialGraphPinType::Normal:
        return "0 0 0";
    case RenderMaterialGraphPinType::Float4:
        return "0 0 0 0";
    case RenderMaterialGraphPinType::Color:
        return "1 1 1 1";
    case RenderMaterialGraphPinType::Unknown:
    case RenderMaterialGraphPinType::Texture2D:
    case RenderMaterialGraphPinType::TextureCube:
    case RenderMaterialGraphPinType::Texture3D:
    case RenderMaterialGraphPinType::Texture2DArray:
    case RenderMaterialGraphPinType::Sampler:
    case RenderMaterialGraphPinType::MaterialAttributes:
        return {};
    }
    return {};
}

[[nodiscard]] RenderMaterialGraphNodeKind LayerStackParameterNodeKind(RenderMaterialGraphPinType type) noexcept {
    switch (type) {
    case RenderMaterialGraphPinType::Float:
        return RenderMaterialGraphNodeKind::ConstantScalar;
    case RenderMaterialGraphPinType::Float2:
        return RenderMaterialGraphNodeKind::ConstantVector2;
    case RenderMaterialGraphPinType::Float3:
    case RenderMaterialGraphPinType::Normal:
        return RenderMaterialGraphNodeKind::ConstantVector;
    case RenderMaterialGraphPinType::Float4:
    case RenderMaterialGraphPinType::Color:
        return RenderMaterialGraphNodeKind::ConstantColor;
    case RenderMaterialGraphPinType::Unknown:
    case RenderMaterialGraphPinType::Texture2D:
    case RenderMaterialGraphPinType::TextureCube:
    case RenderMaterialGraphPinType::Texture3D:
    case RenderMaterialGraphPinType::Texture2DArray:
    case RenderMaterialGraphPinType::Sampler:
    case RenderMaterialGraphPinType::MaterialAttributes:
        return RenderMaterialGraphNodeKind::ConstantScalar;
    }
    return RenderMaterialGraphNodeKind::ConstantScalar;
}

[[nodiscard]] std::string_view LayerStackParameterOutputPin(RenderMaterialGraphPinType type) noexcept {
    switch (type) {
    case RenderMaterialGraphPinType::Float:
        return "value";
    case RenderMaterialGraphPinType::Float2:
        return "xy";
    case RenderMaterialGraphPinType::Float3:
    case RenderMaterialGraphPinType::Normal:
        return "xyz";
    case RenderMaterialGraphPinType::Float4:
    case RenderMaterialGraphPinType::Color:
        return "rgba";
    case RenderMaterialGraphPinType::Unknown:
    case RenderMaterialGraphPinType::Texture2D:
    case RenderMaterialGraphPinType::TextureCube:
    case RenderMaterialGraphPinType::Texture3D:
    case RenderMaterialGraphPinType::Texture2DArray:
    case RenderMaterialGraphPinType::Sampler:
    case RenderMaterialGraphPinType::MaterialAttributes:
        return "value";
    }
    return "value";
}

[[nodiscard]] bool ValidateLayerStackParameters(
    const RenderMaterialGraphNode& layerStackNode,
    std::span<const RenderMaterialGraphLayerStackParameter> parameters,
    std::string_view ownerName,
    std::vector<RenderMaterialGraphDiagnostic>& diagnostics) {
    for (const RenderMaterialGraphLayerStackParameter& parameter : parameters) {
        if (parameter.pinName.empty()) {
            AddLayerStackDiagnostic(
                diagnostics,
                layerStackNode,
                "LayerStack " + std::string{ ownerName } + " parameter has an empty pin name.");
            return false;
        }
        if (!IsLayerStackParameterTypeSupported(parameter.type)) {
            AddLayerStackDiagnostic(
                diagnostics,
                layerStackNode,
                "LayerStack " + std::string{ ownerName } + " parameter '" + parameter.pinName +
                    "' uses unsupported type '" + std::string{ RenderMaterialGraphPinTypeName(parameter.type) } + "'.");
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::vector<RenderMaterialGraphCustomPin> LayerStackParameterPins(
    std::span<const RenderMaterialGraphLayerStackParameter> parameters) {
    std::vector<RenderMaterialGraphCustomPin> pins;
    pins.reserve(parameters.size());
    for (const RenderMaterialGraphLayerStackParameter& parameter : parameters) {
        pins.push_back(RenderMaterialGraphCustomPin{
            .name = parameter.pinName,
            .type = parameter.type,
        });
    }
    return pins;
}

[[nodiscard]] bool ValidateMaterialFunctionCallSignature(
    const RenderMaterialGraphNode& callNode,
    std::uint64_t assetId,
    const FunctionSignature& signature,
    std::vector<RenderMaterialGraphDiagnostic>& diagnostics) {
    const bool inputsMatch = PinListsMatchFunctionSignature(callNode.customCode.inputs, signature.inputs);
    const bool outputsMatch = PinListsMatchFunctionSignature(callNode.customCode.outputs, signature.outputs);
    if (inputsMatch && outputsMatch) {
        return true;
    }

    AddMaterialFunctionDiagnostic(
        diagnostics,
        RenderMaterialGraphDiagnosticKind::MaterialFunctionSignatureMismatch,
        callNode,
        {},
        "MaterialFunctionCall for function asset " + std::to_string(assetId) +
            " has stale dynamic pins; expected " + std::to_string(signature.inputs.size()) +
            " inputs and " + std::to_string(signature.outputs.size()) + " outputs.");
    return false;
}

[[nodiscard]] std::uint32_t NextGraphNodeId(const RenderMaterialGraphDocument& graph) noexcept {
    std::uint32_t next = 1U;
    for (const RenderMaterialGraphNode& node : graph.nodes) {
        next = std::max(next, node.id + 1U);
    }
    return next;
}

[[nodiscard]] const FunctionEndpointSignature* FindFunctionEndpoint(
    std::span<const FunctionEndpointSignature> endpoints,
    std::uint32_t nodeId) noexcept {
    for (const FunctionEndpointSignature& endpoint : endpoints) {
        if (endpoint.nodeId == nodeId) {
            return &endpoint;
        }
    }
    return nullptr;
}

[[nodiscard]] const FunctionEndpointSignature* FindFunctionEndpointByName(
    std::span<const FunctionEndpointSignature> endpoints,
    std::string_view name) noexcept {
    for (const FunctionEndpointSignature& endpoint : endpoints) {
        if (endpoint.name == name) {
            return &endpoint;
        }
    }
    return nullptr;
}

[[nodiscard]] std::optional<GraphEndpoint> MappedFunctionEndpoint(
    const RenderMaterialGraphDocument& functionGraph,
    const RenderMaterialGraphLink& link,
    const std::unordered_map<std::uint32_t, std::uint32_t>& nodeMap,
    const std::unordered_map<std::string, GraphEndpoint>& outerInputs,
    std::span<const FunctionEndpointSignature> functionInputs) {
    const RenderMaterialGraphNode* fromNode = FindRenderMaterialGraphNode(functionGraph, link.fromNodeId);
    if (fromNode == nullptr) {
        return std::nullopt;
    }
    if (fromNode->kind == RenderMaterialGraphNodeKind::FunctionInput) {
        const FunctionEndpointSignature* input = FindFunctionEndpoint(functionInputs, fromNode->id);
        if (input == nullptr) {
            return std::nullopt;
        }
        const auto it = outerInputs.find(input->name);
        if (it == outerInputs.end()) {
            return std::nullopt;
        }
        return it->second;
    }
    const auto mapped = nodeMap.find(fromNode->id);
    if (mapped == nodeMap.end()) {
        return std::nullopt;
    }
    return GraphEndpoint{ .nodeId = mapped->second, .pin = link.fromPin };
}

[[nodiscard]] RenderMaterialGraphLink MakeInlinedGraphLink(
    const RenderMaterialGraphDocument& graph,
    GraphEndpoint from,
    GraphEndpoint to) {
    RenderMaterialGraphLink link{
        .fromNodeId = from.nodeId,
        .fromPin = std::move(from.pin),
        .toNodeId = to.nodeId,
        .toPin = std::move(to.pin),
    };
    if (const RenderMaterialGraphNode* fromNode = FindRenderMaterialGraphNode(graph, link.fromNodeId)) {
        link.fromPinId = RenderMaterialGraphStablePinId(*fromNode, link.fromPin, true);
    }
    if (const RenderMaterialGraphNode* toNode = FindRenderMaterialGraphNode(graph, link.toNodeId)) {
        link.toPinId = RenderMaterialGraphStablePinId(*toNode, link.toPin, false);
    }
    link.id = MakeRenderMaterialGraphLinkId(link);
    return link;
}

void AddLayerStackParameterConstants(
    RenderMaterialGraphDocument& graph,
    std::uint32_t& nextNodeId,
    const RenderMaterialGraphNode& callNode,
    std::span<const RenderMaterialGraphLayerStackParameter> parameters,
    std::int32_t x,
    std::int32_t y) {
    for (std::size_t index = 0U; index < parameters.size(); ++index) {
        const RenderMaterialGraphLayerStackParameter& parameter = parameters[index];
        const RenderMaterialGraphNodeKind kind = LayerStackParameterNodeKind(parameter.type);
        RenderMaterialGraphNode valueNode{
            .id = nextNodeId++,
            .kind = kind,
            .positionX = x,
            .positionY = y + static_cast<std::int32_t>(index) * 64,
            .parameter = RenderMaterialGraphParameterMetadata{
                .displayName = parameter.pinName,
                .defaultValueHint = parameter.valueHint.empty()
                    ? DefaultLayerStackParameterHint(parameter.type)
                    : parameter.valueHint,
                .overrideSupported = false,
            },
        };
        const std::uint32_t valueNodeId = valueNode.id;
        graph.nodes.push_back(std::move(valueNode));
        graph.links.push_back(MakeInlinedGraphLink(
            graph,
            GraphEndpoint{ .nodeId = valueNodeId, .pin = std::string{ LayerStackParameterOutputPin(parameter.type) } },
            GraphEndpoint{ .nodeId = callNode.id, .pin = parameter.pinName }));
    }
}

[[nodiscard]] bool InlineOneMaterialFunctionCall(
    RenderMaterialGraphDocument& graph,
    const RenderMaterialGraphNode& callNode,
    const RenderMaterialGraphDocument& functionGraph,
    const FunctionSignature& signature) {
    RenderMaterialGraphDocument out = graph;
    out.nodes.erase(
        std::remove_if(out.nodes.begin(), out.nodes.end(), [&callNode](const RenderMaterialGraphNode& node) {
            return node.id == callNode.id;
        }),
        out.nodes.end());
    for (RenderMaterialGraphCompositeSubgraph& composite : out.composites) {
        composite.nodeIds.erase(
            std::remove(composite.nodeIds.begin(), composite.nodeIds.end(), callNode.id),
            composite.nodeIds.end());
    }

    std::unordered_map<std::string, GraphEndpoint> outerInputs;
    std::vector<RenderMaterialGraphLink> outgoingLinks;
    out.links.clear();
    for (const RenderMaterialGraphLink& link : graph.links) {
        if (link.toNodeId == callNode.id) {
            outerInputs[link.toPin] = GraphEndpoint{ .nodeId = link.fromNodeId, .pin = link.fromPin };
        } else if (link.fromNodeId == callNode.id) {
            outgoingLinks.push_back(link);
        } else {
            out.links.push_back(link);
        }
    }

    std::unordered_map<std::uint32_t, std::uint32_t> nodeMap;
    std::uint32_t nextNodeId = NextGraphNodeId(out);
    for (const RenderMaterialGraphNode& node : functionGraph.nodes) {
        if (node.kind == RenderMaterialGraphNodeKind::FunctionInput ||
            node.kind == RenderMaterialGraphNodeKind::FunctionOutput ||
            node.kind == RenderMaterialGraphNodeKind::MaterialOutput) {
            continue;
        }
        RenderMaterialGraphNode cloned = node;
        cloned.id = nextNodeId++;
        cloned.positionX += callNode.positionX;
        cloned.positionY += callNode.positionY;
        nodeMap[node.id] = cloned.id;
        out.nodes.push_back(std::move(cloned));
    }

    std::unordered_map<std::string, GraphEndpoint> resolvedOutputs;
    for (const RenderMaterialGraphLink& link : functionGraph.links) {
        const RenderMaterialGraphNode* toNode = FindRenderMaterialGraphNode(functionGraph, link.toNodeId);
        if (toNode == nullptr || toNode->kind == RenderMaterialGraphNodeKind::FunctionInput) {
            continue;
        }

        const std::optional<GraphEndpoint> from = MappedFunctionEndpoint(
            functionGraph,
            link,
            nodeMap,
            outerInputs,
            signature.inputs);
        if (!from.has_value()) {
            continue;
        }

        if (toNode->kind == RenderMaterialGraphNodeKind::FunctionOutput) {
            if (const FunctionEndpointSignature* output = FindFunctionEndpoint(signature.outputs, toNode->id)) {
                resolvedOutputs[output->name] = *from;
            }
            continue;
        }

        const auto mappedTo = nodeMap.find(toNode->id);
        if (mappedTo == nodeMap.end()) {
            continue;
        }
        out.links.push_back(MakeInlinedGraphLink(
            out,
            *from,
            GraphEndpoint{ .nodeId = mappedTo->second, .pin = link.toPin }));
    }

    for (const RenderMaterialGraphLink& link : outgoingLinks) {
        const auto resolved = resolvedOutputs.find(link.fromPin);
        if (resolved == resolvedOutputs.end()) {
            continue;
        }
        out.links.push_back(MakeInlinedGraphLink(
            out,
            resolved->second,
            GraphEndpoint{ .nodeId = link.toNodeId, .pin = link.toPin }));
    }

    graph = std::move(out);
    return true;
}

[[nodiscard]] RenderMaterialGraphNode MakeLayerStackFunctionCallNode(
    std::uint32_t nodeId,
    std::int32_t x,
    std::int32_t y,
    std::uint64_t functionAssetId,
    std::string displayName,
    std::vector<RenderMaterialGraphCustomPin> inputs) {
    return RenderMaterialGraphNode{
        .id = nodeId,
        .kind = RenderMaterialGraphNodeKind::MaterialFunctionCall,
        .positionX = x,
        .positionY = y,
        .parameter = RenderMaterialGraphParameterMetadata{
            .stableId = std::to_string(functionAssetId),
            .displayName = std::move(displayName),
            .description = "Expanded from a Material Layer Stack.",
        },
        .customCode = RenderMaterialGraphCustomCode{
            .body = {},
            .outputType = RenderMaterialGraphPinType::MaterialAttributes,
            .inputs = std::move(inputs),
            .outputs = {
                RenderMaterialGraphCustomPin{ .name = "Attributes", .type = RenderMaterialGraphPinType::MaterialAttributes },
            },
        },
    };
}

[[nodiscard]] bool ExpandOneMaterialLayerStack(
    RenderMaterialGraphDocument& graph,
    const RenderMaterialGraphNode& layerStackNode,
    std::vector<RenderMaterialGraphDiagnostic>& diagnostics) {
    std::vector<RenderMaterialGraphLayerStackEntry> activeEntries;
    for (const RenderMaterialGraphLayerStackEntry& entry : layerStackNode.layerStack) {
        if (entry.enabled) {
            activeEntries.push_back(entry);
        }
    }
    if (activeEntries.empty()) {
        AddLayerStackDiagnostic(diagnostics, layerStackNode, "LayerStack requires at least one enabled layer function.");
        return false;
    }
    for (std::size_t index = 0U; index < activeEntries.size(); ++index) {
        if (activeEntries[index].layerFunctionAssetId == 0U) {
            AddLayerStackDiagnostic(diagnostics, layerStackNode, "LayerStack layer " + std::to_string(index) + " is missing a layer function asset id.");
            return false;
        }
        if (!ValidateLayerStackParameters(layerStackNode, activeEntries[index].layerParameters, "layer " + std::to_string(index), diagnostics)) {
            return false;
        }
        if (index > 0U && activeEntries[index].blendFunctionAssetId == 0U) {
            AddLayerStackDiagnostic(diagnostics, layerStackNode, "LayerStack layer " + std::to_string(index) + " is missing a blend function asset id.");
            return false;
        }
        if (index > 0U) {
            for (const RenderMaterialGraphLayerStackParameter& parameter : activeEntries[index].blendParameters) {
                if (parameter.pinName == "A" || parameter.pinName == "B") {
                    AddLayerStackDiagnostic(
                        diagnostics,
                        layerStackNode,
                        "LayerStack blend " + std::to_string(index) + " parameter '" + parameter.pinName + "' is reserved for layer compositing.");
                    return false;
                }
            }
            if (!ValidateLayerStackParameters(layerStackNode, activeEntries[index].blendParameters, "blend " + std::to_string(index), diagnostics)) {
                return false;
            }
        }
    }

    RenderMaterialGraphDocument out = graph;
    out.nodes.erase(
        std::remove_if(out.nodes.begin(), out.nodes.end(), [&layerStackNode](const RenderMaterialGraphNode& node) {
            return node.id == layerStackNode.id;
        }),
        out.nodes.end());
    for (RenderMaterialGraphCompositeSubgraph& composite : out.composites) {
        composite.nodeIds.erase(
            std::remove(composite.nodeIds.begin(), composite.nodeIds.end(), layerStackNode.id),
            composite.nodeIds.end());
    }

    std::vector<RenderMaterialGraphLink> outgoingLinks;
    out.links.clear();
    for (const RenderMaterialGraphLink& link : graph.links) {
        if (link.fromNodeId == layerStackNode.id) {
            outgoingLinks.push_back(link);
        } else if (link.toNodeId != layerStackNode.id) {
            out.links.push_back(link);
        }
    }

    std::uint32_t nextNodeId = NextGraphNodeId(out);
    std::optional<GraphEndpoint> currentAttributes;
    for (std::size_t index = 0U; index < activeEntries.size(); ++index) {
        const RenderMaterialGraphLayerStackEntry& entry = activeEntries[index];
        const std::string layerName = entry.layerName.empty()
            ? "Layer " + std::to_string(index + 1U)
            : entry.layerName;
        RenderMaterialGraphNode layerCall = MakeLayerStackFunctionCallNode(
            nextNodeId++,
            layerStackNode.positionX - 360 + static_cast<std::int32_t>(index) * 220,
            layerStackNode.positionY + static_cast<std::int32_t>(index) * 80,
            entry.layerFunctionAssetId,
            layerName,
            LayerStackParameterPins(entry.layerParameters));
        const RenderMaterialGraphNode layerCallForLinks = layerCall;
        const std::uint32_t layerCallId = layerCall.id;
        out.nodes.push_back(std::move(layerCall));
        AddLayerStackParameterConstants(
            out,
            nextNodeId,
            layerCallForLinks,
            entry.layerParameters,
            layerStackNode.positionX - 560 + static_cast<std::int32_t>(index) * 220,
            layerStackNode.positionY + static_cast<std::int32_t>(index) * 80);
        GraphEndpoint layerAttributes{ .nodeId = layerCallId, .pin = "Attributes" };
        if (!currentAttributes.has_value()) {
            currentAttributes = layerAttributes;
            continue;
        }

        const std::string blendName = entry.blendName.empty()
            ? "Blend " + std::to_string(index)
            : entry.blendName;
        std::vector<RenderMaterialGraphCustomPin> blendInputs{
            RenderMaterialGraphCustomPin{ .name = "A", .type = RenderMaterialGraphPinType::MaterialAttributes },
            RenderMaterialGraphCustomPin{ .name = "B", .type = RenderMaterialGraphPinType::MaterialAttributes },
        };
        std::vector<RenderMaterialGraphCustomPin> blendParameterPins = LayerStackParameterPins(entry.blendParameters);
        blendInputs.insert(blendInputs.end(), blendParameterPins.begin(), blendParameterPins.end());
        RenderMaterialGraphNode blendCall = MakeLayerStackFunctionCallNode(
            nextNodeId++,
            layerStackNode.positionX - 120 + static_cast<std::int32_t>(index) * 220,
            layerStackNode.positionY + static_cast<std::int32_t>(index) * 80,
            entry.blendFunctionAssetId,
            blendName,
            std::move(blendInputs));
        const RenderMaterialGraphNode blendCallForLinks = blendCall;
        const std::uint32_t blendCallId = blendCall.id;
        out.nodes.push_back(std::move(blendCall));
        out.links.push_back(MakeInlinedGraphLink(
            out,
            *currentAttributes,
            GraphEndpoint{ .nodeId = blendCallId, .pin = "A" }));
        out.links.push_back(MakeInlinedGraphLink(
            out,
            layerAttributes,
            GraphEndpoint{ .nodeId = blendCallId, .pin = "B" }));
        AddLayerStackParameterConstants(
            out,
            nextNodeId,
            blendCallForLinks,
            entry.blendParameters,
            layerStackNode.positionX - 320 + static_cast<std::int32_t>(index) * 220,
            layerStackNode.positionY + 96 + static_cast<std::int32_t>(index) * 80);
        currentAttributes = GraphEndpoint{ .nodeId = blendCallId, .pin = "Attributes" };
    }

    if (currentAttributes.has_value()) {
        for (const RenderMaterialGraphLink& link : outgoingLinks) {
            if (link.fromPin != "attributes") {
                continue;
            }
            out.links.push_back(MakeInlinedGraphLink(
                out,
                *currentAttributes,
                GraphEndpoint{ .nodeId = link.toNodeId, .pin = link.toPin }));
        }
    }

    graph = std::move(out);
    return true;
}

[[nodiscard]] bool ExpandMaterialLayerStacks(
    RenderMaterialGraphDocument& graph,
    std::vector<RenderMaterialGraphDiagnostic>& diagnostics) {
    bool changed = false;
    std::vector<std::uint32_t> layerStackNodeIds;
    for (const RenderMaterialGraphNode& node : graph.nodes) {
        if (node.kind == RenderMaterialGraphNodeKind::LayerStack) {
            layerStackNodeIds.push_back(node.id);
        }
    }
    for (const std::uint32_t nodeId : layerStackNodeIds) {
        const RenderMaterialGraphNode* current = FindRenderMaterialGraphNode(graph, nodeId);
        if (current == nullptr || current->kind != RenderMaterialGraphNodeKind::LayerStack) {
            continue;
        }
        const RenderMaterialGraphNode layerStackNode = *current;
        if (ExpandOneMaterialLayerStack(graph, layerStackNode, diagnostics)) {
            changed = true;
        }
        if (HasGraphDiagnosticError(diagnostics)) {
            break;
        }
    }
    return changed;
}

[[nodiscard]] bool InlineMaterialGraphFunctionsRecursive(
    RenderMaterialGraphDocument& graph,
    RenderMaterialGraphBuildContext context,
    std::vector<std::uint64_t>& functionStack,
    std::vector<RenderMaterialGraphDiagnostic>& diagnostics) {
    bool changed = false;
    std::vector<std::uint32_t> callNodeIds;
    for (const RenderMaterialGraphNode& node : graph.nodes) {
        if (node.kind == RenderMaterialGraphNodeKind::MaterialFunctionCall) {
            callNodeIds.push_back(node.id);
        }
    }

    for (const std::uint32_t callNodeId : callNodeIds) {
        const RenderMaterialGraphNode* currentCallNode = FindRenderMaterialGraphNode(graph, callNodeId);
        if (currentCallNode == nullptr || currentCallNode->kind != RenderMaterialGraphNodeKind::MaterialFunctionCall) {
            continue;
        }
        const RenderMaterialGraphNode callNode = *currentCallNode;
        const std::uint64_t functionAssetId = MaterialFunctionCallAssetId(callNode);
        if (functionAssetId == 0U) {
            AddMaterialFunctionDiagnostic(
                diagnostics,
                RenderMaterialGraphDiagnosticKind::MissingMaterialFunction,
                callNode,
                {},
                "MaterialFunctionCall requires a numeric function asset id in its stable id.");
            continue;
        }
        if (std::find(functionStack.begin(), functionStack.end(), functionAssetId) != functionStack.end()) {
            AddMaterialFunctionDiagnostic(
                diagnostics,
                RenderMaterialGraphDiagnosticKind::MaterialFunctionCycle,
                callNode,
                {},
                "Material function asset " + std::to_string(functionAssetId) + " recursively calls itself.");
            continue;
        }
        if (context.functionLibrary == nullptr) {
            AddMaterialFunctionDiagnostic(
                diagnostics,
                RenderMaterialGraphDiagnosticKind::MissingMaterialFunction,
                callNode,
                {},
                "MaterialFunctionCall asset " + std::to_string(functionAssetId) + " cannot be resolved without a function library.");
            continue;
        }
        const RenderMaterialGraphFunctionLibraryEntry* entry = context.functionLibrary->Find(functionAssetId);
        if (entry == nullptr) {
            AddMaterialFunctionDiagnostic(
                diagnostics,
                RenderMaterialGraphDiagnosticKind::MissingMaterialFunction,
                callNode,
                {},
                "MaterialFunctionCall references missing function asset " + std::to_string(functionAssetId) + ".");
            continue;
        }

        RenderMaterialGraphDocument functionGraph = entry->graph;
        functionStack.push_back(functionAssetId);
        const bool nestedChanged = InlineMaterialGraphFunctionsRecursive(functionGraph, context, functionStack, diagnostics);
        static_cast<void>(nestedChanged);
        functionStack.pop_back();
        if (HasGraphDiagnosticError(diagnostics)) {
            continue;
        }

        const FunctionSignature signature = BuildMaterialFunctionSignature(functionGraph);
        if (!ValidateMaterialFunctionCallSignature(callNode, functionAssetId, signature, diagnostics)) {
            continue;
        }
        if (InlineOneMaterialFunctionCall(graph, callNode, functionGraph, signature)) {
            changed = true;
        }
    }
    return changed;
}

} // namespace

std::vector<std::uint64_t> DiscoverRenderMaterialGraphFunctionDependencies(const RenderMaterialGraphDocument& graph) {
    std::vector<std::uint64_t> dependencies;
    const auto appendUnique = [&dependencies](std::uint64_t assetId) {
        if (assetId == 0U || std::find(dependencies.begin(), dependencies.end(), assetId) != dependencies.end()) {
            return;
        }
        dependencies.push_back(assetId);
    };
    for (const RenderMaterialGraphNode& node : graph.nodes) {
        if (node.kind == RenderMaterialGraphNodeKind::MaterialFunctionCall) {
            appendUnique(MaterialFunctionCallAssetId(node));
        } else if (node.kind == RenderMaterialGraphNodeKind::LayerStack) {
            for (const RenderMaterialGraphLayerStackEntry& entry : node.layerStack) {
                appendUnique(entry.layerFunctionAssetId);
                appendUnique(entry.blendFunctionAssetId);
            }
        }
    }
    std::sort(dependencies.begin(), dependencies.end());
    return dependencies;
}

RenderMaterialGraphFunctionInlineResult InlineRenderMaterialGraphFunctions(
    const RenderMaterialGraphDocument& graph,
    RenderMaterialGraphBuildContext context) {
    RenderMaterialGraphFunctionInlineResult result{
        .graph = graph,
    };
    static_cast<void>(ExpandMaterialLayerStacks(result.graph, result.diagnostics));
    if (!result.Succeeded()) {
        return result;
    }
    if (!GraphContainsNodeKind(result.graph, RenderMaterialGraphNodeKind::MaterialFunctionCall)) {
        return result;
    }

    std::vector<std::uint64_t> functionStack;
    bool changed = true;
    std::size_t guard = 0U;
    while (changed && guard < 128U && result.Succeeded()) {
        changed = InlineMaterialGraphFunctionsRecursive(result.graph, context, functionStack, result.diagnostics);
        ++guard;
    }
    if (guard == 128U && result.Succeeded()) {
        AddGraphDiagnostic(
            result.diagnostics,
            RenderMaterialGraphDiagnosticSeverity::Error,
            RenderMaterialGraphDiagnosticKind::MaterialFunctionCycle,
            0U,
            0U,
            {},
            "Material function inlining exceeded the expansion guard.");
    }
    return result;
}

} // namespace kb::render
