#pragma once

#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <imgui.h>
#include <imgui_node_editor.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kb::editor {

enum class MaterialImguiPinDirection : std::uint8_t {
    Input,
    Output,
};

struct MaterialImguiPin {
    ax::NodeEditor::PinId editorId{};
    std::uint32_t nodeId = 0U;
    std::uint32_t stablePinId = 0U;
    std::string name;
    std::string label;
    render::RenderMaterialGraphPinType type = render::RenderMaterialGraphPinType::Unknown;
    MaterialImguiPinDirection direction = MaterialImguiPinDirection::Input;
    ImVec4 color{ 0.69F, 0.69F, 0.69F, 1.0F };
};

struct MaterialImguiNode {
    ax::NodeEditor::NodeId editorId{};
    std::uint32_t nodeId = 0U;
    render::RenderMaterialGraphNodeKind kind = render::RenderMaterialGraphNodeKind::MaterialOutput;
    std::string title;
    int positionX = 0;
    int positionY = 0;
    ImVec4 headerColor{ 0.12F, 0.15F, 0.19F, 1.0F };
    std::vector<MaterialImguiPin> inputs;
    std::vector<MaterialImguiPin> outputs;
};

struct MaterialImguiLink {
    ax::NodeEditor::LinkId editorId{};
    ax::NodeEditor::PinId startPin{};
    ax::NodeEditor::PinId endPin{};
    std::uint32_t linkId = 0U;
    std::uint32_t fromNodeId = 0U;
    std::uint32_t toNodeId = 0U;
    std::string fromPin;
    std::string toPin;
    ImVec4 color{ 0.78F, 0.62F, 0.24F, 1.0F };
};

struct MaterialImguiNodeEditorModel {
    std::vector<MaterialImguiNode> nodes;
    std::vector<MaterialImguiLink> links;

    [[nodiscard]] const MaterialImguiNode* FindNode(std::uint32_t nodeId) const noexcept;
    [[nodiscard]] const MaterialImguiPin* FindPin(ax::NodeEditor::PinId pinId) const noexcept;
    [[nodiscard]] const MaterialImguiLink* FindLink(ax::NodeEditor::LinkId linkId) const noexcept;
};

[[nodiscard]] ax::NodeEditor::NodeId MaterialImguiNodeId(std::uint32_t nodeId) noexcept;
[[nodiscard]] ax::NodeEditor::PinId MaterialImguiPinId(
    std::uint32_t nodeId,
    render::RenderMaterialGraphNodeKind kind,
    std::string_view pin,
    MaterialImguiPinDirection direction) noexcept;
[[nodiscard]] ax::NodeEditor::LinkId MaterialImguiLinkId(const render::RenderMaterialGraphLink& link) noexcept;

[[nodiscard]] ImVec4 MaterialImguiPinColor(render::RenderMaterialGraphPinType type) noexcept;
[[nodiscard]] ImVec4 MaterialImguiNodeHeaderColor(render::RenderMaterialGraphNodeKind kind) noexcept;
[[nodiscard]] std::string MaterialImguiNodeTitle(const render::RenderMaterialGraphNode& node);
[[nodiscard]] MaterialImguiNodeEditorModel BuildMaterialImguiNodeEditorModel(const render::RenderMaterialGraphDocument& graph);

} // namespace kb::editor
