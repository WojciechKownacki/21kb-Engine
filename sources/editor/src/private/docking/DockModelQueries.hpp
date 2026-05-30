#pragma once

#include "docking/DockNode.hpp"
#include "kb/editor/docking/DockTypes.hpp"

#include <optional>

namespace kb::editor {

class DockModelQueries {
public:
    DockModelQueries() = delete;

    [[nodiscard]] static DockLayout BuildLayout(const DockNode* root, int clientWidth, int clientHeight, int menuHeight, int toolbarHeight, int tabStripHeight, int tabMinWidth, int tabWidth, int splitterSize, int panelPadding);
    [[nodiscard]] static DockHit HitTest(const DockLayout& layout, int x, int y);
    [[nodiscard]] static std::optional<DockDropPreview> ResolveDropPreview(const DockLayout& layout, int x, int y);
};

} // namespace kb::editor
