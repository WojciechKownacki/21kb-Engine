#pragma once

#include "kb/editor/theme/EditorTheme.hpp"
#include "scene/EditorSceneContext.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class ProjectFilesDeleteConfirmOverlayWindow {
public:
#if defined(_WIN32)
    ProjectFilesDeleteConfirmOverlayWindow() = default;
    ~ProjectFilesDeleteConfirmOverlayWindow();

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

    [[nodiscard]] bool EnsureWindow(HWND parent);
    [[nodiscard]] RECT ResolveScreenBounds() const noexcept;
    [[nodiscard]] bool MoveToCurrentBounds(bool showWindow) noexcept;
    void Paint(HDC dc) const;
    void ForwardMouseMessage(UINT message, WPARAM wparam, LPARAM lparam) const;
    void ForwardMouseWheel(WPARAM wparam, LPARAM lparam) const;
    [[nodiscard]] StateSnapshot SnapshotState() const noexcept;
    [[nodiscard]] static bool SameSnapshot(const StateSnapshot& left, const StateSnapshot& right) noexcept;

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

    HWND window_ = nullptr;
    HWND parent_ = nullptr;
    RECT screenBounds_{};
    bool shown_ = false;
    EditorTheme theme_{};
    const EditorSceneContext* sceneContext_ = nullptr;
#endif
};

} // namespace kb::editor
