#pragma once

#include "docking/DockNode.hpp"
#include "docking/DockPanelCollection.hpp"
#include "kb/editor/docking/DockTypes.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace kb::editor {

class EditorDockModelCommands {
public:
    EditorDockModelCommands(DockPanelCollection& panels, std::unique_ptr<DockNode>& root, std::uint32_t& nextNodeId, std::uint32_t& maximizedLeafId) noexcept;

    // Puts the workspace back to the arrangement a new project starts with.
    void ResetWorkspace();
    // Rebuilds the dock tree from a previously serialized workspace. Refuses a text
    // that does not account for exactly the panels this build has, leaving the
    // current arrangement untouched.
    [[nodiscard]] bool RestoreWorkspace(std::string_view tree);
    // The current dock tree as text, for storing alongside the project.
    [[nodiscard]] std::string SerializeWorkspace() const;
    void ActivatePanel(std::uint32_t panelId);
    [[nodiscard]] bool ClosePanel(std::uint32_t panelId);
    void ResizeSplitter(std::uint32_t nodeId, int mouseX, int mouseY, const DockLayout& layout);
    void ReorderPanelInLeaf(std::uint32_t panelId, std::uint32_t leafId, std::uint32_t newIndex);
    void UndockPanel(std::uint32_t panelId, DockRect floatingRect);
    void DockPanelTo(std::uint32_t panelId, const DockDropPreview& target);
    [[nodiscard]] bool ActivatePanelKind(DockPanelKind kind, DockArea fallbackArea);
    [[nodiscard]] bool SetPanelTitle(DockPanelKind kind, std::string title);
    void MoveFloatingPanel(std::uint32_t panelId, int x, int y);
    void ResizeFloatingPanel(std::uint32_t panelId, int width, int height);
    [[nodiscard]] bool ToggleMaximizedLeaf(std::uint32_t leafId) noexcept;
    [[nodiscard]] bool RestoreMaximizedLeaf() noexcept;

private:
    [[nodiscard]] std::uint32_t NextNodeId() noexcept;
    [[nodiscard]] static std::uint32_t NextNodeIdCallback(void* context) noexcept;

    DockPanelCollection& panels_;
    std::unique_ptr<DockNode>& root_;
    std::uint32_t& nextNodeId_;
    std::uint32_t& maximizedLeafId_;
};

} // namespace kb::editor
