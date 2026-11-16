#include "docking/DockDockedTabDragHandler.hpp"

#if defined(_WIN32)

#include "docking/DockTabIndexResolver.hpp"
#include "docking/DockWindowLayoutResolver.hpp"

namespace kb::editor {

bool DockDockedTabDragHandler::Reorder(DockPointerDrag& drag, int x, int y, HWND mainWindow, EditorDockModel& dockModel, const EditorFloatingWindowManager& floatingWindows, const EditorMetrics& metrics) {
    if (drag.detached || !drag.sourceStrip.Contains(x, y)) {
        return false;
    }

    const DockLayout layout = DockWindowLayoutResolver::Resolve(drag.sourceWindow, mainWindow, dockModel, floatingWindows, metrics);
    const std::uint32_t newIndex = DockTabIndexResolver{}.Resolve(layout, drag.sourceLeafId, x);
    if (newIndex != drag.sourceTabIndex) {
        dockModel.Commands().ReorderPanelInLeaf(drag.panelId, drag.sourceLeafId, newIndex);
        drag.sourceTabIndex = newIndex;
        InvalidateRect(drag.sourceWindow, nullptr, FALSE);
    }
    return true;
}

} // namespace kb::editor

#endif
