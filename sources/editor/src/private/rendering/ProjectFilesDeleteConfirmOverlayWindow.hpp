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

class ProjectFilesDeleteConfirmOverlayWindow final
#if defined(_WIN32)
    : private EditorOverlayPopupWindow::Client
#endif
{
public:
#if defined(_WIN32)
    ProjectFilesDeleteConfirmOverlayWindow() noexcept;
    ~ProjectFilesDeleteConfirmOverlayWindow() = default;

    ProjectFilesDeleteConfirmOverlayWindow(const ProjectFilesDeleteConfirmOverlayWindow&) = delete;
    ProjectFilesDeleteConfirmOverlayWindow& operator=(const ProjectFilesDeleteConfirmOverlayWindow&) = delete;

    void Show(HWND parent, const EditorTheme& theme, const EditorSceneContext& sceneContext);
    void Hide() noexcept;
#endif

private:
#if defined(_WIN32)
    struct StateSnapshot {
        bool open = false;
        int offsetX = 0;
        int offsetY = 0;
        int listScroll = 0;
    };

    [[nodiscard]] RECT ResolveScreenBounds() const noexcept;
    bool MoveToCurrentBounds() noexcept;
    void PaintOverlay(HDC dc, const RECT& client) override;
    bool HandleOverlayMessage(UINT message, WPARAM wparam, LPARAM lparam) override;
    void ForwardMouseWheel(WPARAM wparam, LPARAM lparam) const;
    [[nodiscard]] StateSnapshot SnapshotState() const noexcept;
    [[nodiscard]] static bool SameSnapshot(const StateSnapshot& left, const StateSnapshot& right) noexcept;

    EditorOverlayPopupWindow popup_;
    EditorTheme theme_{};
    const EditorSceneContext* sceneContext_ = nullptr;
#endif
};

} // namespace kb::editor
