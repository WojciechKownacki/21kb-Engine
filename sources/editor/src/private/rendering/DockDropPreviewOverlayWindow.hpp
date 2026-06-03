#pragma once

#include "kb/editor/docking/DockTypes.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

class DockDropPreviewOverlayWindow {
public:
#if defined(_WIN32)
    DockDropPreviewOverlayWindow() = default;
    ~DockDropPreviewOverlayWindow();

    DockDropPreviewOverlayWindow(const DockDropPreviewOverlayWindow&) = delete;
    DockDropPreviewOverlayWindow& operator=(const DockDropPreviewOverlayWindow&) = delete;

    void Show(HWND parent, const DockDropPreview& preview, const EditorTheme& theme);
    void Hide() noexcept;
#endif

private:
#if defined(_WIN32)
    [[nodiscard]] bool EnsureWindow(HWND parent);
    void Paint(HDC dc) const;

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

    HWND window_ = nullptr;
    DockDropPreview preview_{};
    EditorTheme theme_{};
#endif
};

} // namespace kb::editor
