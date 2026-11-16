#pragma once

#include "docking/DockNode.hpp"
#include "docking/DockPanelCollection.hpp"
#include "kb/editor/docking/DockTypes.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace kb::editor {

class EditorDockModelCommands {
public:
    EditorDockModelCommands(DockPanelCollection& panels, std::unique_ptr<DockNode>& root, std::vector<DockNode>& floating, std::uint32_t& nextNodeId, std::uint32_t& maximizedLeafId, bool& drawerCollapsed) noexcept;
    // Collapses the bottom drawer to its tab strip, or opens it again. Returns whether it changed.
    [[nodiscard]] bool SetDrawerCollapsed(bool collapsed) noexcept;

    // Puts the workspace back to the arrangement a new project starts with.
    void ResetWorkspace();
    [[nodiscard]] bool RegisterToolPanel(DockPanel panel);
    [[nodiscard]] bool UnregisterToolPanel(std::uint32_t panelId);
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
    // Puts the panel in a floating window of its own, taking it out of the docked tree or of
    // the window it was a tab of.
    void UndockPanel(std::uint32_t panelId, DockRect floatingRect);
    // Docks the panel where the target says. A target leaf may be the tab group of a floating
    // window: the panel then becomes one of its tabs, at the insertion index of a strip target.
    void DockPanelTo(std::uint32_t panelId, const DockDropPreview& target);
    [[nodiscard]] bool ActivatePanelKind(DockPanelKind kind, DockArea fallbackArea);
    [[nodiscard]] bool SetPanelTitle(DockPanelKind kind, std::string title);
    // A floating window moves and resizes with all of its tabs.
    void MoveFloatingPanel(std::uint32_t panelId, int x, int y);
    void ResizeFloatingPanel(std::uint32_t panelId, int width, int height);
    [[nodiscard]] bool ToggleMaximizedLeaf(std::uint32_t leafId) noexcept;
    [[nodiscard]] bool RestoreMaximizedLeaf() noexcept;

private:
    [[nodiscard]] std::uint32_t NextNodeId() noexcept;
    [[nodiscard]] static std::uint32_t NextNodeIdCallback(void* context) noexcept;

    DockPanelCollection& panels_;
    std::unique_ptr<DockNode>& root_;
    std::vector<DockNode>& floating_;
    std::uint32_t& nextNodeId_;
    std::uint32_t& maximizedLeafId_;
    bool& drawerCollapsed_;
};

} // namespace kb::editor
