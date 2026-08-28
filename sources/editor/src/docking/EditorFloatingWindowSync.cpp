#include "docking/EditorFloatingWindowSync.hpp"

#if defined(_WIN32)

namespace kb::editor {

void EditorFloatingWindowSync::Reconcile(
    const EditorDockModel& dockModel, EditorFloatingWindowManager& floatingWindows) {
    for (const DockPanel& panel : dockModel.Queries().Panels()) {
        if (panel.area == DockArea::Floating && panel.visible) {
            // Creating a window a panel already has is a no-op, so a panel whose
            // window survived the change keeps it, title and position included.
            static_cast<void>(floatingWindows.Commands().Create(
                panel.id, panel.title, panel.floatingRect));
            continue;
        }
        floatingWindows.Commands().Destroy(panel.id);
    }
}

} // namespace kb::editor

#endif
