#pragma once

#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
#include "docking/DockPointerDrag.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <optional>

namespace kb::editor {

class EditorDockController {
public:
#if defined(_WIN32)
    void Configure(HWND mainWindow, EditorDockModel& dockModel, EditorFloatingWindowManager& floatingWindows, const EditorMetrics& metrics) noexcept;

    [[nodiscard]] const DockDropPreview* DropPreview() const noexcept;

    [[nodiscard]] bool HandlePointerDown(HWND window, int x, int y);
    [[nodiscard]] bool HandlePointerMove(HWND window, int x, int y, bool leftButtonDown);
    [[nodiscard]] bool HandlePointerUp(HWND window);
    void CancelDrag() noexcept;
    void HandleCaptureChanged(HWND newCapture) noexcept;
    void UpdateHoverCursor(HWND window, int x, int y) const;
#endif

private:
#if defined(_WIN32)
    [[nodiscard]] bool Ready() const noexcept;
    [[nodiscard]] bool LeftButtonPressed() const noexcept;
    void CaptureSourceWindow() const noexcept;

    HWND mainWindow_ = nullptr;
    EditorDockModel* dockModel_ = nullptr;
    EditorFloatingWindowManager* floatingWindows_ = nullptr;
    const EditorMetrics* metrics_ = nullptr;
    std::optional<DockPointerDrag> drag_;
    std::optional<DockDropPreview> dropPreview_;
#endif
};

} // namespace kb::editor
