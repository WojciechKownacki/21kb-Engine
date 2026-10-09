#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/EditorOverlayPopupWindow.hpp"
#include "scene/EditorSceneContext.hpp"

#include <cstdint>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class SceneViewportToolbarDropdownOverlayWindow final
#if defined(_WIN32)
    : private EditorOverlayPopupWindow::Client
#endif
{
public:
#if defined(_WIN32)
    SceneViewportToolbarDropdownOverlayWindow() noexcept;
    ~SceneViewportToolbarDropdownOverlayWindow() = default;

    SceneViewportToolbarDropdownOverlayWindow(const SceneViewportToolbarDropdownOverlayWindow&) = delete;
    SceneViewportToolbarDropdownOverlayWindow& operator=(const SceneViewportToolbarDropdownOverlayWindow&) = delete;

    void Show(HWND parent, const RECT& sceneContent, std::uint64_t panelId, const EditorTheme& theme, const EditorSceneContext& sceneContext);
    void Hide() noexcept;
#endif

private:
#if defined(_WIN32)
    [[nodiscard]] RECT ResolveScreenBounds() const noexcept;
    void PaintOverlay(HDC dc, const RECT& client) override;
    bool HandleOverlayMessage(UINT message, WPARAM wparam, LPARAM lparam) override;
    [[nodiscard]] int ItemIndexAt(int clientX, int clientY) const noexcept;

    EditorOverlayPopupWindow popup_;
    RECT sceneContent_{};
    std::uint64_t panelId_ = 0U;
    int hoveredItem_ = -1;
    EditorTheme theme_{};
    const EditorSceneContext* sceneContext_ = nullptr;
#endif
};

} // namespace kb::editor
