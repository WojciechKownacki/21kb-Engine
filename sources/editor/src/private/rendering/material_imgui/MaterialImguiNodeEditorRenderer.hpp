#pragma once

#include "rendering/material_imgui/MaterialImguiNodeEditorModel.hpp"

#include <memory>
#include <optional>
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

struct MaterialImguiNodeEditorFrameResult {
    std::optional<MaterialImguiNodeEditorNewLink> acceptedNewLink;
    std::vector<MaterialImguiNodeEditorDeletedLink> acceptedDeletedLinks;
};

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
