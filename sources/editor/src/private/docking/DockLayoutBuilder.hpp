#pragma once

#include "docking/DockNode.hpp"
#include "kb/editor/docking/DockTypes.hpp"

namespace kb::editor {

class DockLayoutBuilder {
public:
    [[nodiscard]] DockLayout Build(const DockNode* root, int clientWidth, int clientHeight, int menuHeight, int toolbarHeight, int tabStripHeight, int tabMinWidth, int tabWidth, int splitterSize, int panelPadding) const;

private:
    void BuildNodeLayout(const DockNode& node, const DockRect& rect, DockLayout& layout, int tabStripHeight, int tabMinWidth, int tabWidth, int splitterSize, int panelPadding) const;
};

} // namespace kb::editor
