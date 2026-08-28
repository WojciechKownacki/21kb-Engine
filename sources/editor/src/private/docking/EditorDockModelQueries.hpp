#pragma once

#include "docking/DockNode.hpp"
#include "docking/DockPanelCollection.hpp"
#include "kb/editor/docking/DockTypes.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace kb::editor {

class EditorDockModelQueries {
public:
    EditorDockModelQueries(const DockPanelCollection& panels, const DockNode* root, std::uint32_t maximizedLeafId) noexcept;

    [[nodiscard]] const std::vector<DockPanel>& Panels() const noexcept;
    [[nodiscard]] std::vector<DockPanel> PanelsInArea(DockArea area) const;
    [[nodiscard]] const DockPanel* FindPanel(std::uint32_t panelId) const noexcept;
    [[nodiscard]] DockLayout BuildLayout(int clientWidth, int clientHeight, int menuHeight, int toolbarHeight, int tabStripHeight, int tabMinWidth, int tabWidth, int splitterSize) const;
    [[nodiscard]] DockHit HitTest(const DockLayout& layout, int x, int y) const;
    [[nodiscard]] std::optional<DockDropPreview> ResolveDropPreview(const DockLayout& layout, int x, int y) const;
    [[nodiscard]] std::uint32_t PanelCountInLeaf(std::uint32_t leafId) const noexcept;

private:
    const DockPanelCollection& panels_;
    const DockNode* root_ = nullptr;
    std::uint32_t maximizedLeafId_ = 0;
};

} // namespace kb::editor
