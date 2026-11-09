#include "docking/EditorFloatingWindowSync.hpp"
#include "windowing/EditorDisplayRecovery.hpp"

#if defined(_WIN32)

namespace kb::editor {

void EditorFloatingWindowSync::Reconcile(
    const EditorDockModel& dockModel, EditorFloatingWindowManager& floatingWindows) {
    for (const DockPanel& panel : dockModel.Queries().Panels()) {
        if (panel.area == DockArea::Floating && panel.visible) {
            // Reuse the window, but apply the newly selected layout's placement.
            static_cast<void>(floatingWindows.Commands().Create(
                panel.id, panel.title, panel.floatingRect));
            if (const HWND window = floatingWindows.Queries().WindowForPanel(panel.id)) {
                const auto rect = RecoverNativeDisplayRect(panel.floatingRect);
                SetWindowPos(window, nullptr, rect.x, rect.y, rect.width, rect.height, SWP_NOZORDER | SWP_NOACTIVATE);
            }
            continue;
        }
        floatingWindows.Commands().Destroy(panel.id);
    }
}

} // namespace kb::editor

#endif
