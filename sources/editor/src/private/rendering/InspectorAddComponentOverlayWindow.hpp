#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "scene/EditorSceneContext.hpp"

#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class InspectorAddComponentOverlayWindow {
public:
#if defined(_WIN32)
    InspectorAddComponentOverlayWindow() = default;
    ~InspectorAddComponentOverlayWindow();

    InspectorAddComponentOverlayWindow(const InspectorAddComponentOverlayWindow&) = delete;
    InspectorAddComponentOverlayWindow& operator=(const InspectorAddComponentOverlayWindow&) = delete;

    void Show(HWND owner, const RECT& inspectorContent, const EditorTheme& theme, EditorSceneContext& sceneContext);
    void Hide() noexcept;
    void HideForOwner(HWND owner) noexcept;
#endif

private:
#if defined(_WIN32)
    [[nodiscard]] bool EnsureWindow(HWND owner);
    void Paint(HDC dc) const;
    void HandlePointerDown(int x, int y);
    void HandlePointerMove(int x, int y);
    void HandlePointerUp() noexcept;
    void HandleMouseWheel(int screenX, int screenY, int delta);

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

    HWND window_ = nullptr;
    HWND owner_ = nullptr;
    RECT screenBounds_{};
    bool shown_ = false;
    EditorTheme theme_{};
    EditorSceneContext* sceneContext_ = nullptr;
    std::string renderedSearch_;
    std::string renderedCategory_;
    int renderedScroll_ = 0;
    float renderedSlide_ = 1.0F;
    bool renderedScrollbarDragging_ = false;
#endif
};

} // namespace kb::editor
