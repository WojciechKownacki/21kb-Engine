#pragma once

#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"

namespace kb::editor {

#if defined(_WIN32)

// Brings the torn-off windows in line with the dock model after the arrangement is
// replaced wholesale - opening a project, or switching to a saved layout. A panel
// that ended up floating gets its window, and a panel that ended up docked or closed
// loses the one it had, so no window is left behind pointing at nothing.
class EditorFloatingWindowSync final {
public:
    EditorFloatingWindowSync() = delete;

    static void Reconcile(const EditorDockModel& dockModel, EditorFloatingWindowManager& floatingWindows);
};

#endif

} // namespace kb::editor
