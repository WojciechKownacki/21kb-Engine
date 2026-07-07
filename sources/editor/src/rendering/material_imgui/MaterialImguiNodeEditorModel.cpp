#include "rendering/material_imgui/MaterialImguiNodeEditorModel.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <utility>

namespace kb::editor {
namespace {

constexpr std::uintptr_t kNodeIdTag = 0x1000000000000000ULL;
constexpr std::uintptr_t kInputPinIdTag = 0x2000000000000000ULL;
constexpr std::uintptr_t kOutputPinIdTag = 0x3000000000000000ULL;
constexpr std::uintptr_t kLinkIdTag = 0x4000000000000000ULL;

[[nodiscard]] std::uintptr_t PackedNodePinId(
    std::uint32_t nodeId,
    render::RenderMaterialGraphNodeKind kind,
    std::string_view pin,
    MaterialImguiPinDirection direction) noexcept {
    const std::uint32_t stablePinId = render::RenderMaterialGraphStablePinId(
        kind,
        pin,
        direction == MaterialImguiPinDirection::Output);
    const std::uintptr_t tag = direction == MaterialImguiPinDirection::Output ? kOutputPinIdTag : kInputPinIdTag;
    return tag | (static_cast<std::uintptr_t>(nodeId) << 28U) | static_cast<std::uintptr_t>(stablePinId & 0x0FFFFFFFU);
}

[[nodiscard]] std::uintptr_t PackedLinkId(const render::RenderMaterialGraphLink& link) noexcept {
    if (link.id != 0U) {
        return kLinkIdTag | static_cast<std::uintptr_t>(link.id);
    }
    const std::size_t hash = std::hash<std::string>{}(
        std::to_string(link.fromNodeId) + ":" + link.fromPin + ">" + std::to_string(link.toNodeId) + ":" + link.toPin);
    return kLinkIdTag | (static_cast<std::uintptr_t>(hash) & 0x0FFFFFFFFFFFFFFFULL);
}

[[nodiscard]] std::string PinLabel(std::string_view pin) {
    if (pin == "rgba" || pin == "RGBA") {
        return "RGBA";
    }
    if (pin == "uv") {
        return "UV";
    }
    if (pin == "baseColor") {
        return "Base Color";
    }
    if (pin == "roughness") {
        return "Roughness";
    }
    if (pin == "metallic") {
        return "Metallic";
    }
    if (pin == "normal") {
        return "Normal";
    }
    if (pin.empty()) {
        return {};
    }
    std::string label{ pin };
    label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
    return label;
}

[[nodiscard]] MaterialImguiPin BuildPin(
    const render::RenderMaterialGraphNode& node,
    std::string_view pin,
    MaterialImguiPinDirection direction) {
    const bool output = direction == MaterialImguiPinDirection::Output;
    const render::RenderMaterialGraphPinType type = render::RenderMaterialGraphPinDataType(node, pin, output);
    return MaterialImguiPin{
        .editorId = MaterialImguiPinId(node.id, node.kind, pin, direction),
        .nodeId = node.id,
        .stablePinId = render::RenderMaterialGraphStablePinId(node, pin, output),
        .name = std::string{ pin },
        .label = PinLabel(pin),
        .type = type,
        .direction = direction,
        .color = MaterialImguiPinColor(type),
    };
}

} // namespace

const MaterialImguiNode* MaterialImguiNodeEditorModel::FindNode(std::uint32_t nodeId) const noexcept {
    const auto it = std::ranges::find_if(nodes, [nodeId](const MaterialImguiNode& node) {
        return node.nodeId == nodeId;
    });
    return it == nodes.end() ? nullptr : &*it;
}

const MaterialImguiPin* MaterialImguiNodeEditorModel::FindPin(ax::NodeEditor::PinId pinId) const noexcept {
    for (const MaterialImguiNode& node : nodes) {
        const auto input = std::ranges::find_if(node.inputs, [pinId](const MaterialImguiPin& pin) {
            return pin.editorId == pinId;
        });
        if (input != node.inputs.end()) {
            return &*input;
        }
        const auto output = std::ranges::find_if(node.outputs, [pinId](const MaterialImguiPin& pin) {
            return pin.editorId == pinId;
        });
        if (output != node.outputs.end()) {
            return &*output;
        }
    }
    return nullptr;
}

const MaterialImguiLink* MaterialImguiNodeEditorModel::FindLink(ax::NodeEditor::LinkId linkId) const noexcept {
    const auto it = std::ranges::find_if(links, [linkId](const MaterialImguiLink& link) {
        return link.editorId == linkId;
    });
    return it == links.end() ? nullptr : &*it;
}

ax::NodeEditor::NodeId MaterialImguiNodeId(std::uint32_t nodeId) noexcept {
    return ax::NodeEditor::NodeId{ kNodeIdTag | static_cast<std::uintptr_t>(nodeId) };
}

ax::NodeEditor::PinId MaterialImguiPinId(
    std::uint32_t nodeId,
    render::RenderMaterialGraphNodeKind kind,
    std::string_view pin,
    MaterialImguiPinDirection direction) noexcept {
    return ax::NodeEditor::PinId{ PackedNodePinId(nodeId, kind, pin, direction) };
}

ax::NodeEditor::LinkId MaterialImguiLinkId(const render::RenderMaterialGraphLink& link) noexcept {
    return ax::NodeEditor::LinkId{ PackedLinkId(link) };
}

ImVec4 MaterialImguiPinColor(render::RenderMaterialGraphPinType type) noexcept {
    switch (type) {
    case render::RenderMaterialGraphPinType::Float: return ImVec4{ 0.70F, 0.70F, 0.70F, 1.0F };
    case render::RenderMaterialGraphPinType::Float2: return ImVec4{ 0.36F, 0.62F, 0.85F, 1.0F };
    case render::RenderMaterialGraphPinType::Float3: return ImVec4{ 0.32F, 0.71F, 0.62F, 1.0F };
    case render::RenderMaterialGraphPinType::Float4: return ImVec4{ 0.85F, 0.59F, 0.30F, 1.0F };
    case render::RenderMaterialGraphPinType::Color: return ImVec4{ 0.86F, 0.67F, 0.19F, 1.0F };
    case render::RenderMaterialGraphPinType::Texture2D:
    case render::RenderMaterialGraphPinType::TextureCube:
    case render::RenderMaterialGraphPinType::Texture3D:
    case render::RenderMaterialGraphPinType::Texture2DArray: return ImVec4{ 0.72F, 0.56F, 0.84F, 1.0F };
    case render::RenderMaterialGraphPinType::Sampler: return ImVec4{ 0.59F, 0.52F, 0.86F, 1.0F };
    case render::RenderMaterialGraphPinType::Normal: return ImVec4{ 0.37F, 0.65F, 0.87F, 1.0F };
    case render::RenderMaterialGraphPinType::Bool: return ImVec4{ 0.46F, 0.73F, 0.40F, 1.0F };
    case render::RenderMaterialGraphPinType::MaterialAttributes: return ImVec4{ 0.85F, 0.44F, 0.69F, 1.0F };
    case render::RenderMaterialGraphPinType::Unknown: return ImVec4{ 0.69F, 0.69F, 0.69F, 1.0F };
    }
    return ImVec4{ 0.69F, 0.69F, 0.69F, 1.0F };
}

ImVec4 MaterialImguiNodeHeaderColor(render::RenderMaterialGraphNodeKind kind) noexcept {
    switch (kind) {
    case render::RenderMaterialGraphNodeKind::MaterialOutput: return ImVec4{ 0.54F, 0.18F, 0.36F, 1.0F };
    case render::RenderMaterialGraphNodeKind::TextureSample:
    case render::RenderMaterialGraphNodeKind::TextureSampleCube:
    case render::RenderMaterialGraphNodeKind::TextureSampleVolume:
    case render::RenderMaterialGraphNodeKind::TextureSample2DArray:
    case render::RenderMaterialGraphNodeKind::ParameterTexture:
    case render::RenderMaterialGraphNodeKind::TextureObject:
    case render::RenderMaterialGraphNodeKind::TextureObjectCube:
    case render::RenderMaterialGraphNodeKind::TextureObjectVolume:
    case render::RenderMaterialGraphNodeKind::TextureObject2DArray: return ImVec4{ 0.04F, 0.42F, 0.39F, 1.0F };
    case render::RenderMaterialGraphNodeKind::NormalUnpack: return ImVec4{ 0.10F, 0.30F, 0.52F, 1.0F };
    case render::RenderMaterialGraphNodeKind::ConstantColor:
    case render::RenderMaterialGraphNodeKind::ParameterColor:
    case render::RenderMaterialGraphNodeKind::ColorRamp: return ImVec4{ 0.50F, 0.34F, 0.08F, 1.0F };
    default: return ImVec4{ 0.13F, 0.18F, 0.24F, 1.0F };
    }
}

[[nodiscard]] bool IsTextureFamilyNode(render::RenderMaterialGraphNodeKind kind) noexcept {
    switch (kind) {
    case render::RenderMaterialGraphNodeKind::TextureSample:
    case render::RenderMaterialGraphNodeKind::TextureSampleCube:
    case render::RenderMaterialGraphNodeKind::TextureSampleVolume:
    case render::RenderMaterialGraphNodeKind::TextureSample2DArray:
    case render::RenderMaterialGraphNodeKind::ParameterTexture:
    case render::RenderMaterialGraphNodeKind::TextureObject:
    case render::RenderMaterialGraphNodeKind::TextureObjectCube:
    case render::RenderMaterialGraphNodeKind::TextureObjectVolume:
    case render::RenderMaterialGraphNodeKind::TextureObject2DArray:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool IsTextureSamplePreviewNode(render::RenderMaterialGraphNodeKind kind) noexcept {
    switch (kind) {
    case render::RenderMaterialGraphNodeKind::TextureSample:
    case render::RenderMaterialGraphNodeKind::TextureSampleCube:
    case render::RenderMaterialGraphNodeKind::TextureSampleVolume:
    case render::RenderMaterialGraphNodeKind::TextureSample2DArray:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool IsTextureObjectPreviewNode(render::RenderMaterialGraphNodeKind kind) noexcept {
    switch (kind) {
    case render::RenderMaterialGraphNodeKind::ParameterTexture:
    case render::RenderMaterialGraphNodeKind::TextureObject:
    case render::RenderMaterialGraphNodeKind::TextureObjectCube:
    case render::RenderMaterialGraphNodeKind::TextureObjectVolume:
    case render::RenderMaterialGraphNodeKind::TextureObject2DArray:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] MaterialImguiNodeBodyKind NodeBodyKind(render::RenderMaterialGraphNodeKind kind) noexcept {
    if (IsTextureSamplePreviewNode(kind)) {
        return MaterialImguiNodeBodyKind::TextureSamplePreview;
    }
    if (IsTextureObjectPreviewNode(kind)) {
        return MaterialImguiNodeBodyKind::TextureObjectPreview;
    }
    return MaterialImguiNodeBodyKind::Pins;
}

[[nodiscard]] std::string MaterialImguiNodeKindTitle(render::RenderMaterialGraphNodeKind kind) {
    switch (kind) {
    case render::RenderMaterialGraphNodeKind::TextureSample: return "Image Texture";
    case render::RenderMaterialGraphNodeKind::TextureSampleCube: return "Cube Texture";
    case render::RenderMaterialGraphNodeKind::TextureSampleVolume: return "Volume Texture";
    case render::RenderMaterialGraphNodeKind::TextureSample2DArray: return "Texture Array";
    case render::RenderMaterialGraphNodeKind::ParameterTexture: return "Image Parameter";
    case render::RenderMaterialGraphNodeKind::TextureObject: return "Texture Object";
    case render::RenderMaterialGraphNodeKind::TextureObjectCube: return "Texture Cube Object";
    case render::RenderMaterialGraphNodeKind::TextureObjectVolume: return "Texture Volume Object";
    case render::RenderMaterialGraphNodeKind::TextureObject2DArray: return "Texture Array Object";
    default: return std::string{ render::RenderMaterialGraphNodeKindName(kind) };
    }
}

std::string MaterialImguiNodeTitle(const render::RenderMaterialGraphNode& node) {
    if (IsTextureFamilyNode(node.kind)) {
        return MaterialImguiNodeKindTitle(node.kind);
    }
    if (!node.parameter.displayName.empty()) {
        return node.parameter.displayName;
    }
    return MaterialImguiNodeKindTitle(node.kind);
}

MaterialImguiNodeEditorModel BuildMaterialImguiNodeEditorModel(const render::RenderMaterialGraphDocument& graph) {
    MaterialImguiNodeEditorModel model{};
    model.nodes.reserve(graph.nodes.size());
    for (const render::RenderMaterialGraphNode& graphNode : graph.nodes) {
        MaterialImguiNode node{
            .editorId = MaterialImguiNodeId(graphNode.id),
            .nodeId = graphNode.id,
            .kind = graphNode.kind,
            .title = MaterialImguiNodeTitle(graphNode),
            .positionX = graphNode.positionX,
            .positionY = graphNode.positionY,
            .bodyKind = NodeBodyKind(graphNode.kind),
            .headerColor = MaterialImguiNodeHeaderColor(graphNode.kind),
        };
        for (const std::string& input : render::RenderMaterialGraphNodeInputPinNames(graphNode)) {
            node.inputs.push_back(BuildPin(graphNode, input, MaterialImguiPinDirection::Input));
        }
        for (const std::string& output : render::RenderMaterialGraphNodeOutputPinNames(graphNode)) {
            node.outputs.push_back(BuildPin(graphNode, output, MaterialImguiPinDirection::Output));
        }
        model.nodes.push_back(std::move(node));
    }

    model.links.reserve(graph.links.size());
    for (const render::RenderMaterialGraphLink& graphLink : graph.links) {
        const render::RenderMaterialGraphNode* fromNode = render::FindRenderMaterialGraphNode(graph, graphLink.fromNodeId);
        const render::RenderMaterialGraphNode* toNode = render::FindRenderMaterialGraphNode(graph, graphLink.toNodeId);
        if (fromNode == nullptr || toNode == nullptr) {
            continue;
        }
        const render::RenderMaterialGraphPinType type =
            render::RenderMaterialGraphPinDataType(*fromNode, graphLink.fromPin, true);
        model.links.push_back(MaterialImguiLink{
            .editorId = MaterialImguiLinkId(graphLink),
            .startPin = MaterialImguiPinId(fromNode->id, fromNode->kind, graphLink.fromPin, MaterialImguiPinDirection::Output),
            .endPin = MaterialImguiPinId(toNode->id, toNode->kind, graphLink.toPin, MaterialImguiPinDirection::Input),
            .linkId = graphLink.id,
            .fromNodeId = graphLink.fromNodeId,
            .toNodeId = graphLink.toNodeId,
            .fromPin = graphLink.fromPin,
            .toPin = graphLink.toPin,
            .color = MaterialImguiPinColor(type),
        });
    }
    return model;
}

} // namespace kb::editor
