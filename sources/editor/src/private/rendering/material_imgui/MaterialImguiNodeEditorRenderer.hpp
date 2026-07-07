#pragma once

#include "rendering/material_imgui/MaterialImguiNodeEditorModel.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ax::NodeEditor {
struct EditorContext;
}

namespace kb::editor {

struct MaterialImguiNodeEditorNewLink {
    ax::NodeEditor::PinId startPin{};
    ax::NodeEditor::PinId endPin{};
};

struct MaterialImguiNodeEditorDeletedLink {
    ax::NodeEditor::LinkId link{};
    ax::NodeEditor::PinId startPin{};
    ax::NodeEditor::PinId endPin{};
};

struct MaterialImguiNodeEditorGraphLinkRequest {
    std::uint32_t fromNodeId = 0U;
    std::string fromPin;
    std::uint32_t toNodeId = 0U;
    std::string toPin;
};

struct MaterialImguiNodeEditorFrameResult {
    std::optional<MaterialImguiNodeEditorNewLink> acceptedNewLink;
    std::vector<MaterialImguiNodeEditorDeletedLink> acceptedDeletedLinks;
};

[[nodiscard]] std::optional<MaterialImguiNodeEditorGraphLinkRequest> ResolveMaterialImguiNewLink(
    const MaterialImguiNodeEditorModel& model,
    const MaterialImguiNodeEditorNewLink& link) noexcept;

[[nodiscard]] std::optional<MaterialImguiNodeEditorGraphLinkRequest> ResolveMaterialImguiDeletedLink(
    const MaterialImguiNodeEditorModel& model,
    const MaterialImguiNodeEditorDeletedLink& link) noexcept;

class MaterialImguiNodeEditorRenderer {
public:
    MaterialImguiNodeEditorRenderer();
    ~MaterialImguiNodeEditorRenderer();

    MaterialImguiNodeEditorRenderer(const MaterialImguiNodeEditorRenderer&) = delete;
    MaterialImguiNodeEditorRenderer& operator=(const MaterialImguiNodeEditorRenderer&) = delete;
    MaterialImguiNodeEditorRenderer(MaterialImguiNodeEditorRenderer&&) noexcept;
    MaterialImguiNodeEditorRenderer& operator=(MaterialImguiNodeEditorRenderer&&) noexcept;

    [[nodiscard]] MaterialImguiNodeEditorFrameResult Render(
        const MaterialImguiNodeEditorModel& model,
        const ImVec2& size = ImVec2(0.0F, 0.0F));

private:
    struct EditorContextDeleter {
        void operator()(ax::NodeEditor::EditorContext* context) const noexcept;
    };

    std::unique_ptr<ax::NodeEditor::EditorContext, EditorContextDeleter> context_;
};

} // namespace kb::editor
