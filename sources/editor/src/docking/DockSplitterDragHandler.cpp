#include "docking/DockSplitterDragHandler.hpp"

#if defined(_WIN32)

#include "docking/DockMainLayoutResolver.hpp"

namespace kb::editor {

void DockSplitterDragHandler::Move(DockPointerDrag& drag, int x, int y, HWND mainWindow, EditorDockModel& dockModel, const EditorMetrics& metrics) {
    const DockLayout layout = DockMainLayoutResolver::Resolve(mainWindow, dockModel, metrics);
    dockModel.Commands().ResizeSplitter(drag.splitterNodeId, x, y, layout);
    InvalidateRect(mainWindow, nullptr, FALSE);
}

} // namespace kb::editor

#endif
