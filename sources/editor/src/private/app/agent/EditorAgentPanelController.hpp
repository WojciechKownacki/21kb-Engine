#pragma once

#include "agent/EditorAgentPanelState.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorSceneContext;

#if defined(_WIN32)

// The Agent panel's input: clicks on its controls, typing into the request box, Enter to send.
class EditorAgentPanelController final {
public:
    explicit EditorAgentPanelController(EditorSceneContext& sceneContext) noexcept : sceneContext_(sceneContext) {}

    // `owner` is the window the agent menu opens over.
    bool HandlePointerDown(HWND owner, const RECT& content, int x, int y);
    // Typing while the request box has the keyboard; false when it does not.
    [[nodiscard]] bool HandleChar(WPARAM character);
    [[nodiscard]] bool HandleKeyDown(HWND owner, WPARAM key);
    // Sends the typed request, or stops the one in progress.
    bool SendOrStop();

    // The project folder and the kb_cli beside the editor.
    [[nodiscard]] static EditorAgentWorkspace Workspace();

private:
    void ShowAgentMenu(HWND owner, const RECT& anchor);

    EditorSceneContext& sceneContext_;
};

#endif

} // namespace kb::editor
