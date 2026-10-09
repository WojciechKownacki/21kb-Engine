#pragma once

#include <string>

namespace kb::editor {

class EditorSceneBgfxViewport;
class EditorSceneContext;

struct EditorHeadlessPopupCheckResult {
    bool succeeded = false;
    std::string detail;
};

// Checks of the editor's popups made through a real editor window: an off-screen
// window whose messages go through EditorWindowMessageRouter exactly as the
// editor's own windows do, painted by the editor's painters and sharing the
// headless run's viewport, so its scene panel has a real child render surface.
// Input is posted as window messages and the thread's queue is pumped through
// TranslateMessage/DispatchMessage, the way the editor's message loop runs it.
class EditorHeadlessPopupChecks final {
public:
#if defined(_WIN32)
    // Opens toolbar menus with clicks on the menu bar and checks that each dropdown
    // is a popup above the window and its scene viewport, paints every row, and
    // hands a click on a row to the toolbar's own menu handling.
    [[nodiscard]] static EditorHeadlessPopupCheckResult VerifyToolbarMenuOverlay(
        EditorSceneContext& sceneContext, EditorSceneBgfxViewport& viewport);
    // Opens the Inspector's Add Component browser for the selected entity, clicks
    // its search box and types into it with key messages, both to the editor window
    // and to the browser popup, and checks that the search filters the components.
    [[nodiscard]] static EditorHeadlessPopupCheckResult VerifyAddComponentSearchInput(
        EditorSceneContext& sceneContext, EditorSceneBgfxViewport& viewport);
#endif
};

} // namespace kb::editor
