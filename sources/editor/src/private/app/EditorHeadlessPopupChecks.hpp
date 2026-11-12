#pragma once

#include <filesystem>
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
    [[nodiscard]] static EditorHeadlessPopupCheckResult VerifyParticleControls(
        EditorSceneContext& sceneContext, EditorSceneBgfxViewport& viewport);
    [[nodiscard]] static EditorHeadlessPopupCheckResult VerifyDebuggerInteraction(
        EditorSceneContext& sceneContext, EditorSceneBgfxViewport& viewport);
    [[nodiscard]] static EditorHeadlessPopupCheckResult VerifyWorkspaceControls(
        EditorSceneContext& sceneContext, EditorSceneBgfxViewport& viewport);
    // Drives the dock workspace with pointer and key messages: tab click, reorder, tear-off,
    // grouping and ungrouping, Escape, maximize, the tab menu, closing, the Layout menu's panel
    // rows and a crowded strip. Writes what the window painted along the way into `screenshots`.
    [[nodiscard]] static EditorHeadlessPopupCheckResult VerifyDockingInteractions(
        EditorSceneContext& sceneContext, EditorSceneBgfxViewport& viewport, const std::filesystem::path& screenshots);
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
    // Frames the Scene View of a real editor window on a sphere of focusRadius at the scene origin, looking
    // along +Z, and clicks it the way the mouse does - a move, then the button down and up - where the world
    // point (x, y, z) lands on screen, moved by offsetX pixels. The selection is left as the click made it.
    [[nodiscard]] static EditorHeadlessPopupCheckResult ClickSceneView(
        EditorSceneContext& sceneContext, EditorSceneBgfxViewport& viewport,
        float x, float y, float z, float offsetX, float focusRadius);
#endif
};

} // namespace kb::editor
