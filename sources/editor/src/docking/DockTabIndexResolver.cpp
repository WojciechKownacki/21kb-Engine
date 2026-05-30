#include "docking/DockTabIndexResolver.hpp"

namespace kb::editor {

std::uint32_t DockTabIndexResolver::Resolve(const DockLayout& layout, std::uint32_t leafId, int x) const noexcept {
    std::uint32_t index = 0;
    bool sawLeafPanel = false;

    for (const DockPanelLayout& panel : layout.panels) {
        if (panel.leafId != leafId) {
            continue;
        }

        sawLeafPanel = true;
        const int midpoint = panel.tab.x + (panel.tab.width / 2);
        if (x < midpoint) {
            return index;
        }
        ++index;
    }

    if (!sawLeafPanel) {
        return 0;
    }
    return index == 0 ? 0 : index - 1;
}

} // namespace kb::editor
