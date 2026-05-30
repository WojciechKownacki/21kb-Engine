#pragma once

#include "docking/DockPanelCollection.hpp"
#include "docking/DockNode.hpp"
#include "kb/editor/docking/DockTypes.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace kb::editor {

class EditorDockModel {
public:
    EditorDockModel();
    ~EditorDockModel();

    EditorDockModel(const EditorDockModel&) = delete;
    EditorDockModel& operator=(const EditorDockModel&) = delete;
    EditorDockModel(EditorDockModel&&) = delete;
    EditorDockModel& operator=(EditorDockModel&&) = delete;

    [[nodiscard]] const std::vector<DockPanel>& Panels() const noexcept;
    [[nodiscard]] std::vector<DockPanel> PanelsInArea(DockArea area) const;
    [[nodiscard]] const DockPanel* FindPanel(std::uint32_t panelId) const noexcept;
    [[nodiscard]] DockPanel* FindPanel(std::uint32_t panelId) noexcept;
    [[nodiscard]] DockLayout BuildLayout(int clientWidth, int clientHeight, int menuHeight, int toolbarHeight, int tabStripHeight, int tabMinWidth, int tabWidth, int splitterSize, int panelPadding) const;
    [[nodiscard]] DockHit HitTest(const DockLayout& layout, int x, int y) const;
    [[nodiscard]] std::optional<DockDropPreview> ResolveDropPreview(const DockLayout& layout, int x, int y) const;

    void ActivatePanel(std::uint32_t panelId);
    void ResizeSplitter(std::uint32_t nodeId, int mouseX, int mouseY, const DockLayout& layout);
    void ReorderPanelInLeaf(std::uint32_t panelId, std::uint32_t leafId, std::uint32_t newIndex);
    [[nodiscard]] std::uint32_t PanelCountInLeaf(std::uint32_t leafId) const noexcept;
    void UndockPanel(std::uint32_t panelId, DockRect floatingRect);
    void DockPanelTo(std::uint32_t panelId, const DockDropPreview& target);
    void MoveFloatingPanel(std::uint32_t panelId, int x, int y);
    void ResizeFloatingPanel(std::uint32_t panelId, int width, int height);

private:
    [[nodiscard]] std::uint32_t NextNodeId() noexcept;
    [[nodiscard]] static std::uint32_t NextNodeIdCallback(void* context) noexcept;

    DockPanelCollection panels_;
    std::unique_ptr<DockNode> root_;
    std::uint32_t nextNodeId_ = 1;
};

} // namespace kb::editor
