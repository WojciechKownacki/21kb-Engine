#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class ProjectFilesFilterMenuOverlayWindow {
public:
#if defined(_WIN32)
    ProjectFilesFilterMenuOverlayWindow() = default;
    ~ProjectFilesFilterMenuOverlayWindow();

    ProjectFilesFilterMenuOverlayWindow(const ProjectFilesFilterMenuOverlayWindow&) = delete;
    ProjectFilesFilterMenuOverlayWindow& operator=(const ProjectFilesFilterMenuOverlayWindow&) = delete;

    void Show(HWND parent, const RECT& assetContent, const EditorTheme& theme, const EditorSceneContext& sceneContext);
    void Hide() noexcept;
#endif

private:
#if defined(_WIN32)
    [[nodiscard]] bool EnsureWindow(HWND parent);
    void Paint(HDC dc) const;
    void ForwardMouseMessage(UINT message, WPARAM wparam, LPARAM lparam) const;
    void HandleMouseMove(int x, int y);

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

    HWND window_ = nullptr;
    HWND parent_ = nullptr;
    RECT screenBounds_{};
    bool shown_ = false;
    bool lastShowFolders_ = true;
    bool lastShowTemplates_ = true;
    int hoveredIndex_ = -1;
    EditorTheme theme_{};
    const EditorSceneContext* sceneContext_ = nullptr;
#endif
};

} // namespace kb::editor
