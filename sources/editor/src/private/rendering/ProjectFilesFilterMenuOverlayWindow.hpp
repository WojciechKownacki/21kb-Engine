#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "rendering/EditorOverlayPopupWindow.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class ProjectFilesFilterMenuOverlayWindow final
#if defined(_WIN32)
    : private EditorOverlayPopupWindow::Client
#endif
{
public:
#if defined(_WIN32)
    ProjectFilesFilterMenuOverlayWindow() noexcept;
    ~ProjectFilesFilterMenuOverlayWindow() = default;

    ProjectFilesFilterMenuOverlayWindow(const ProjectFilesFilterMenuOverlayWindow&) = delete;
    ProjectFilesFilterMenuOverlayWindow& operator=(const ProjectFilesFilterMenuOverlayWindow&) = delete;

    void Show(HWND parent, const RECT& assetContent, const EditorTheme& theme, const EditorSceneContext& sceneContext);
    void Hide() noexcept;
#endif

private:
#if defined(_WIN32)
    void PaintOverlay(HDC dc, const RECT& client) override;
    bool HandleOverlayMessage(UINT message, WPARAM wparam, LPARAM lparam) override;
    void HandleMouseMove(int x, int y);

    EditorOverlayPopupWindow popup_;
    bool lastShowFolders_ = true;
    bool lastShowTemplates_ = true;
    int hoveredIndex_ = -1;
    EditorTheme theme_{};
    const EditorSceneContext* sceneContext_ = nullptr;
#endif
};

} // namespace kb::editor
