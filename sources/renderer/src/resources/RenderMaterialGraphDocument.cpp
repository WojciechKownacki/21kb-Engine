#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <cstddef>

namespace kb::render {
namespace {

[[nodiscard]] bool EqualsIgnoreCase(std::string_view lhs, std::string_view rhs) noexcept {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < lhs.size(); ++index) {
        char left = lhs[index];
        char right = rhs[index];
        if (left >= 'A' && left <= 'Z') {
            left = static_cast<char>(left - 'A' + 'a');
        }
        if (right >= 'A' && right <= 'Z') {
            right = static_cast<char>(right - 'A' + 'a');
        }
        if (left != right) {
            return false;
        }
    }
    return true;
}

} // namespace

std::string_view RenderMaterialGraphNodeKindName(RenderMaterialGraphNodeKind kind) noexcept {
    switch (kind) {
    case RenderMaterialGraphNodeKind::MaterialOutput:
        return "MaterialOutput";
    case RenderMaterialGraphNodeKind::ConstantScalar:
        return "ConstantScalar";
    case RenderMaterialGraphNodeKind::ConstantColor:
        return "ConstantColor";
    case RenderMaterialGraphNodeKind::TextureSample:
        return "TextureSample";
    case RenderMaterialGraphNodeKind::ParameterScalar:
        return "ParameterScalar";
    case RenderMaterialGraphNodeKind::ParameterColor:
        return "ParameterColor";
    case RenderMaterialGraphNodeKind::ParameterTexture:
        return "ParameterTexture";
    }
    return "MaterialOutput";
}

std::optional<RenderMaterialGraphNodeKind> ParseRenderMaterialGraphNodeKind(std::string_view text) noexcept {
    if (EqualsIgnoreCase(text, "MaterialOutput")) {
        return RenderMaterialGraphNodeKind::MaterialOutput;
    }
    if (EqualsIgnoreCase(text, "ConstantScalar")) {
        return RenderMaterialGraphNodeKind::ConstantScalar;
    }
    if (EqualsIgnoreCase(text, "ConstantColor")) {
        return RenderMaterialGraphNodeKind::ConstantColor;
    }
    if (EqualsIgnoreCase(text, "TextureSample")) {
        return RenderMaterialGraphNodeKind::TextureSample;
    }
    if (EqualsIgnoreCase(text, "ParameterScalar")) {
        return RenderMaterialGraphNodeKind::ParameterScalar;
    }
    if (EqualsIgnoreCase(text, "ParameterColor")) {
        return RenderMaterialGraphNodeKind::ParameterColor;
    }
    if (EqualsIgnoreCase(text, "ParameterTexture")) {
        return RenderMaterialGraphNodeKind::ParameterTexture;
    }
    return std::nullopt;
}

RenderMaterialGraphDocument MakeDefaultRenderMaterialGraphDocument() {
    RenderMaterialGraphDocument graph{};
    graph.documentVersion = kRenderMaterialGraphDocumentVersion;
    graph.hasExplicitDocumentVersion = true;
    graph.nodes.push_back(RenderMaterialGraphNode{
        .id = 1U,
        .kind = RenderMaterialGraphNodeKind::MaterialOutput,
        .positionX = 640,
        .positionY = 240,
    });
    return graph;
}

const RenderMaterialGraphNode* FindRenderMaterialGraphNode(const RenderMaterialGraphDocument& graph, std::uint32_t nodeId) noexcept {
    for (const RenderMaterialGraphNode& node : graph.nodes) {
        if (node.id == nodeId) {
            return &node;
        }
    }
    return nullptr;
}

bool IsRenderMaterialGraphInputPin(RenderMaterialGraphNodeKind kind, std::string_view pin) noexcept {
    switch (kind) {
    case RenderMaterialGraphNodeKind::MaterialOutput:
        return pin == "baseColor" ||
            pin == "metallic" ||
            pin == "roughness" ||
            pin == "normal" ||
            pin == "emissive" ||
            pin == "occlusion" ||
            pin == "alpha";
    case RenderMaterialGraphNodeKind::TextureSample:
        return pin == "uv";
    case RenderMaterialGraphNodeKind::ConstantScalar:
    case RenderMaterialGraphNodeKind::ConstantColor:
    case RenderMaterialGraphNodeKind::ParameterScalar:
    case RenderMaterialGraphNodeKind::ParameterColor:
    case RenderMaterialGraphNodeKind::ParameterTexture:
        return false;
    }
    return false;
}

bool IsRenderMaterialGraphOutputPin(RenderMaterialGraphNodeKind kind, std::string_view pin) noexcept {
    switch (kind) {
    case RenderMaterialGraphNodeKind::ConstantScalar:
    case RenderMaterialGraphNodeKind::ParameterScalar:
        return pin == "value";
    case RenderMaterialGraphNodeKind::ConstantColor:
    case RenderMaterialGraphNodeKind::ParameterColor:
        return pin == "rgba";
    case RenderMaterialGraphNodeKind::TextureSample:
        return pin == "color" || pin == "r" || pin == "g" || pin == "b" || pin == "a";
    case RenderMaterialGraphNodeKind::ParameterTexture:
        return pin == "texture";
    case RenderMaterialGraphNodeKind::MaterialOutput:
        return false;
    }
    return false;
}

} // namespace kb::render
