#pragma once

#include "docking/DockPointerDrag.hpp"
#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class DockFloatingDragOperation {
public:
    DockFloatingDragOperation() = delete;

#if defined(_WIN32)
    static void EnsureDetached(DockPointerDrag& drag, POINT screen, EditorDockModel& dockModel, EditorFloatingWindowManager& floatingWindows);
    static void MoveWindow(DockPointerDrag& drag, POINT screen, EditorDockModel& dockModel, EditorFloatingWindowManager& floatingWindows);
#endif

private:
    [[nodiscard]] static DockDropPreview DefaultFloatingReturnTarget() noexcept;
};

} // namespace kb::editor
