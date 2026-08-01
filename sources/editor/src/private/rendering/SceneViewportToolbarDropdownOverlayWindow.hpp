#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "scene/EditorSceneContext.hpp"

#include <cstdint>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class SceneViewportToolbarDropdownOverlayWindow {
public:
#if defined(_WIN32)
    SceneViewportToolbarDropdownOverlayWindow() = default;
    ~SceneViewportToolbarDropdownOverlayWindow();

    SceneViewportToolbarDropdownOverlayWindow(const SceneViewportToolbarDropdownOverlayWindow&) = delete;
    SceneViewportToolbarDropdownOverlayWindow& operator=(const SceneViewportToolbarDropdownOverlayWindow&) = delete;

    void Show(HWND parent, const RECT& sceneContent, std::uint64_t panelId, const EditorTheme& theme, const EditorSceneContext& sceneContext);
    void ShowTerrainToolbar(HWND parent, const RECT& sceneContent, std::uint64_t panelId, const EditorTheme& theme, const EditorSceneContext& sceneContext);
    void Hide() noexcept;
#endif

private:
#if defined(_WIN32)
    [[nodiscard]] bool EnsureWindow(HWND parent);
    [[nodiscard]] RECT ResolveScreenBounds() const noexcept;
    [[nodiscard]] bool MoveToCurrentBounds(bool showWindow) noexcept;
    void Paint(HDC dc) const;
    [[nodiscard]] int ItemIndexAt(int clientX, int clientY) const noexcept;
    void ForwardMouseMessage(UINT message, WPARAM wparam, LPARAM lparam) const;

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

    HWND window_ = nullptr;
    HWND parent_ = nullptr;
    RECT sceneContent_{};
    RECT screenBounds_{};
    std::uint64_t panelId_ = 0U;
    bool shown_ = false;
    bool terrainToolbarOnly_ = false;
    int hoveredItem_ = -1;
    EditorTheme theme_{};
    const EditorSceneContext* sceneContext_ = nullptr;
#endif
};

} // namespace kb::editor
