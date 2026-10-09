#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/EditorOverlayPopupWindow.hpp"
#include "scene/EditorSceneContext.hpp"

#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class InspectorAddComponentOverlayWindow final
#if defined(_WIN32)
    : private EditorOverlayPopupWindow::Client
#endif
{
public:
#if defined(_WIN32)
    InspectorAddComponentOverlayWindow() noexcept;
    ~InspectorAddComponentOverlayWindow() = default;

    InspectorAddComponentOverlayWindow(const InspectorAddComponentOverlayWindow&) = delete;
    InspectorAddComponentOverlayWindow& operator=(const InspectorAddComponentOverlayWindow&) = delete;

    void Show(HWND owner, const RECT& inspectorContent, const EditorTheme& theme, EditorSceneContext& sceneContext);
    void Hide() noexcept;
    void HideForOwner(HWND owner) noexcept;
#endif

private:
#if defined(_WIN32)
    void PaintOverlay(HDC dc, const RECT& client) override;
    bool HandleOverlayMessage(UINT message, WPARAM wparam, LPARAM lparam) override;
    void HandlePointerDown(int x, int y);
    void HandlePointerMove(int x, int y);
    void HandlePointerUp() noexcept;
    void HandleMouseWheel(int screenX, int screenY, int delta);

    EditorOverlayPopupWindow popup_;
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
