#include "docking/EditorFloatingWindowSync.hpp"
#include "windowing/EditorDisplayRecovery.hpp"

#if defined(_WIN32)

namespace kb::editor {

void EditorFloatingWindowSync::Reconcile(
    const EditorDockModel& dockModel, EditorFloatingWindowManager& floatingWindows) {
    floatingWindows.Commands().DestroyOrphaned();
    for (const DockNode& group : dockModel.Queries().FloatingGroups()) {
        const DockPanel* active = dockModel.Queries().FindPanel(group.activePanelId);
        if (active == nullptr) {
            continue;
        }
        // Reuse the window, but apply the newly selected layout's placement.
        static_cast<void>(floatingWindows.Commands().Create(active->id, active->title, active->floatingRect));
        if (const HWND window = floatingWindows.Queries().WindowForPanel(active->id)) {
            const auto rect = RecoverOwnedDisplayRect(GetWindow(window, GW_OWNER), active->floatingRect);
            SetWindowPos(window, nullptr, rect.x, rect.y, rect.width, rect.height, SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
}

} // namespace kb::editor

#endif
