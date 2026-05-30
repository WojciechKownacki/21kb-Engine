#pragma once

#include "docking/EditorDockModel.hpp"
#include "docking/EditorFloatingWindowManager.hpp"
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

    void HandlePointerDown(HWND window, int x, int y);
    void HandlePointerMove(HWND window, int x, int y);
    void HandlePointerUp(HWND window);
    void UpdateHoverCursor(HWND window, int x, int y) const;
#endif

private:
#if defined(_WIN32)
    struct PointerDrag {
        DockHitKind kind = DockHitKind::None;
        std::uint32_t panelId = 0;
        int offsetX = 0;
        int offsetY = 0;
        std::uint32_t splitterNodeId = 0;
        std::uint32_t sourceLeafId = 0;
        std::uint32_t sourceTabIndex = 0;
        DockRect sourceStrip{};
        HWND sourceWindow = nullptr;
        bool detached = false;
    };

    [[nodiscard]] bool Ready() const noexcept;
    [[nodiscard]] bool IsMainWindow(HWND window) const noexcept;
    [[nodiscard]] DockLayout BuildMainLayout() const;

    void InvalidateMain() const;
    void StartDockedDrag(HWND window, const DockLayout& layout, const DockHit& hit);
    void StartFloatingDrag(HWND window, std::uint32_t panelId, int x, int y);
    void UpdateDropPreviewAtScreenPoint(POINT screen);

    HWND mainWindow_ = nullptr;
    EditorDockModel* dockModel_ = nullptr;
    EditorFloatingWindowManager* floatingWindows_ = nullptr;
    const EditorMetrics* metrics_ = nullptr;
    std::optional<PointerDrag> drag_;
    std::optional<DockDropPreview> dropPreview_;
#endif
};

} // namespace kb::editor
