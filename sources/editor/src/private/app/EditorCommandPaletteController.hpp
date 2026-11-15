#pragma once

#include "app/EditorWindowMessageContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

// Opens the command palette and carries out what is chosen in it. The palette lists the enabled rows of every
// menu, which choosing runs through the menus' own route, and the assets the editor can open, which choosing
// opens the way a double-click in the asset browser does. The keys and the pointer reach it from the editor
// window's message routing while it is open.
class EditorCommandPaletteController {
public:
    EditorCommandPaletteController() = delete;

#if defined(_WIN32)
    // The key the palette opens with (Ctrl+Shift+P).
    [[nodiscard]] static bool IsOpenShortcut(WPARAM key) noexcept;

    static void Open(HWND mainWindow, EditorDockModel& dockModel, EditorSceneContext& sceneContext, EditorShellInteractionState& shellInteraction);
    static void Close(EditorWindowMessageContext& context);

    // The opening shortcut, and while the palette is open the keys that move, choose and close. False for a key
    // that edits the text, which the caller hands to the text box.
    [[nodiscard]] static bool HandleKeyDown(EditorWindowMessageContext& context, WPARAM key);
    // While the palette is open every mouse message of an editor window belongs to it; true when it took one.
    [[nodiscard]] static bool HandlePointer(EditorWindowMessageContext& context, HWND messageWindow, UINT message, WPARAM wparam, LPARAM lparam);
    // Runs the chosen row and closes the palette.
    static void ChooseSelected(EditorWindowMessageContext& context);
#endif
};

} // namespace kb::editor
