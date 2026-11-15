#pragma once

#include "kb/editor/docking/DockTypes.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <cstdint>

namespace kb::editor {

class EditorDockModel;
class EditorFloatingWindowManager;
class EditorSceneContext;

#if defined(_WIN32)
// The one way a panel closes, whether from its tab, its floating window or a menu: the
// document it edits is settled first, then the panel leaves the workspace.
class EditorPanelCloser final {
public:
    // Asks what to do with the unsaved document a panel of this kind edits. False when the
    // user cancels, so the panel (or the whole editor, with `closingEditor`) stays open.
    [[nodiscard]] static bool ResolveUnsavedDocument(HWND owner, DockPanelKind kind, EditorSceneContext& sceneContext, bool closingEditor, bool floating);

    // Closes a docked or floating panel. False when it was not open or the user kept it.
    [[nodiscard]] static bool Close(HWND owner, std::uint32_t panelId, EditorDockModel& dockModel, EditorFloatingWindowManager& floatingWindows, EditorSceneContext& sceneContext);

    // Closes every other tab of the group the panel is docked in, stopping at the first one the user keeps.
    static void CloseOthersInGroup(HWND owner, std::uint32_t panelId, EditorDockModel& dockModel, EditorFloatingWindowManager& floatingWindows, EditorSceneContext& sceneContext);
};
#endif

} // namespace kb::editor
