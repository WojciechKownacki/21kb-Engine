#pragma once

#include "agent/EditorAgentPanelState.hpp"

#include <optional>

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
    // A button of the bubble that waits for a decision (the agent got stuck, a plan), and the suggestion over the request box: the
    // panel and the bar both press them here. What they start sees the project as the editor has it.
    bool PressCardButton(int index);
    // A button of the bubble over the agent bar: the card's own buttons, the suggestion's, and for a plan the one that opens its list.
    bool PressNotice(int index);
    // One menu for the agent bar: every agent with its models, then the reasoning level. It opens
    // upwards from `screenAnchor`, its bottom-left corner.
    void ShowModelMenu(HWND owner, POINT screenAnchor);

    // The project folder and the kb_cli beside the editor.
    [[nodiscard]] static EditorAgentWorkspace Workspace();

private:
    // Saves what is open and gathers what the agent is told about the editor, for a request the agent works on.
    [[nodiscard]] std::optional<EditorAgentWorkspace> PrepareRun();
    void ShowAgentMenu(HWND owner, const RECT& anchor);
    void ShowModeMenu(HWND owner, const RECT& anchor);
    void AttachFile(HWND owner);
    // Ctrl+V: a screenshot or copied image files become pictures of the request, text is typed.
    void Paste(HWND owner);
    void ShowConversations(HWND owner, const RECT& anchor);
    void SearchConversations(HWND owner, const RECT& anchor);
    // The model (`models`) or reasoning level picker.
    void ShowChoiceMenu(HWND owner, const RECT& anchor, bool models);

    EditorSceneContext& sceneContext_;
};

#endif

} // namespace kb::editor
