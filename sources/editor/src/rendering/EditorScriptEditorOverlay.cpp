#include "rendering/EditorScriptEditorOverlay.hpp"

#if defined(_WIN32)
#include "rendering/ScriptEditorPanelRenderer.hpp"
#include "rendering/script_editor/ScriptEditorWindow.hpp"
#include "scene/EditorSceneContext.hpp"

#include <unordered_map>

namespace kb::editor {
namespace {

// One editor child window per host (the main window or a floating window).
[[nodiscard]] std::unordered_map<HWND, HWND>& Windows() {
    static std::unordered_map<HWND, HWND> windows;
    return windows;
}

} // namespace

void EditorScriptEditorOverlay::Sync(HWND parent, const RECT& content, const EditorSceneContext& sceneContext) {
    if (parent == nullptr) {
        return;
    }
    const EditorScriptEditorState& script = sceneContext.ScriptEditor();
    if (!script.IsOpen()) {
        Hide(parent);
        return;
    }

    HWND& window = Windows()[parent];
    if (window == nullptr || IsWindow(window) == 0) {
        window = ScriptEditorWindow::Ensure(parent);
        if (window == nullptr) {
            return;
        }
    }

    ScriptEditorWindow::Sync(window, ScriptEditorPanelRenderer::BodyRect(content), script.FilePath(), script.Generation());
}

void EditorScriptEditorOverlay::Hide(HWND parent) noexcept {
    const auto it = Windows().find(parent);
    if (it != Windows().end()) {
        ScriptEditorWindow::Hide(it->second);
    }
}

bool EditorScriptEditorOverlay::IsDirty(HWND parent) {
    const auto it = Windows().find(parent);
    return it != Windows().end() && ScriptEditorWindow::IsModified(it->second);
}

} // namespace kb::editor

#endif
