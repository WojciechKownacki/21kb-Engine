#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/AgentBarRenderer.hpp"

#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class EditorSceneContext;

// The agent bubble's window over the Scene View. The Scene View is a native child surface, so the
// bubble is an owned popup; it is a layered window drawn with per-pixel alpha, so its rounded edges
// and shadow blend into the scene. It never takes activation: the keys typed into it reach the
// editor window, which hands them to the agent's request box.
class EditorAgentBarOverlay final {
public:
#if defined(_WIN32)
    EditorAgentBarOverlay() noexcept = default;
    ~EditorAgentBarOverlay();

    EditorAgentBarOverlay(const EditorAgentBarOverlay&) = delete;
    EditorAgentBarOverlay& operator=(const EditorAgentBarOverlay&) = delete;

    void Show(HWND owner, const RECT& sceneContent, const EditorTheme& theme, EditorSceneContext& sceneContext);
    void Hide() noexcept;
#endif

private:
#if defined(_WIN32)
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    [[nodiscard]] bool Ensure(HWND owner);
    // Draws the bubble for the current state and puts it on screen at `screenBounds`.
    void Render();
    void HandlePointerDown(int x, int y);

    HWND window_ = nullptr;
    HWND owner_ = nullptr;
    EditorSceneContext* sceneContext_ = nullptr;
    RECT sceneContent_{};
    bool shown_ = false;
    HDC memoryDc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previousBitmap_ = nullptr;
    void* pixels_ = nullptr;
    SIZE bitmapSize_{};
    POINT hover_{ -1, -1 };
    // The layout last drawn, which clicks and the pointer are tested against, and the row or
    // button under the pointer.
    AgentBarLayout layout_{};
    AgentBarHit hoverHit_{};
    bool tracking_ = false;
    // What the bubble showed when it was last drawn.
    std::string renderedKey_;
#endif
};

} // namespace kb::editor
