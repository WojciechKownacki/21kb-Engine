#pragma once

#include "kb/editor/docking/DockTypes.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <cstdint>
#include <string>
#include <vector>

namespace kb::editor {

class EditorDockModel;
class EditorFloatingWindowManager;
class EditorSceneContext;

enum class EditorDockTabCommand : std::uint8_t {
    None,
    Close,
    CloseOthers,
    ToggleMaximize,
    Float,
    DockBack,
};

struct EditorDockTabMenuItem {
    EditorDockTabCommand command = EditorDockTabCommand::None;
    std::string label;
    bool enabled = true;
};

#if defined(_WIN32)
// What a tab offers besides being clicked and dragged: its right-click menu, and the
// middle click that closes it. The same commands serve the title of a floating panel.
class EditorDockTabMenu final {
public:
    // The rows for a panel as it stands now: docked or floating, alone in its group or not.
    [[nodiscard]] static std::vector<EditorDockTabMenuItem> Items(const EditorDockModel& dockModel, std::uint32_t panelId);

    // Runs one row. False when it did not apply or the user kept an unsaved document open.
    [[nodiscard]] static bool Execute(
        EditorDockTabCommand command, HWND owner, std::uint32_t panelId,
        EditorDockModel& dockModel, EditorFloatingWindowManager& floatingWindows, EditorSceneContext& sceneContext);

    // The panel whose tab (in the main window) or title (in a floating one) is under the point; 0 when none.
    [[nodiscard]] static std::uint32_t PanelAt(
        HWND messageWindow, HWND mainWindow, int x, int y,
        const EditorDockModel& dockModel, const EditorFloatingWindowManager& floatingWindows, const EditorMetrics& metrics);

    // Right click: shows the menu at the pointer and runs the choice. True when a tab took the click.
    [[nodiscard]] static bool HandleRightButtonDown(
        HWND messageWindow, HWND mainWindow, int x, int y,
        EditorDockModel& dockModel, EditorFloatingWindowManager& floatingWindows, EditorSceneContext& sceneContext, const EditorMetrics& metrics);

    // Middle click closes the tab under the pointer. True when a tab took the click.
    [[nodiscard]] static bool HandleMiddleButtonDown(
        HWND messageWindow, HWND mainWindow, int x, int y,
        EditorDockModel& dockModel, EditorFloatingWindowManager& floatingWindows, EditorSceneContext& sceneContext, const EditorMetrics& metrics);
};
#endif

} // namespace kb::editor
