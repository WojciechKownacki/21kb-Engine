#pragma once

#include "docking/FloatingWindowRegistry.hpp"
#include "docking/FloatingWindowResizeEventResolver.hpp"
#include "kb/editor/docking/DockTypes.hpp"
#include "kb/editor/theme/EditorTheme.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <cstdint>
#include <optional>

namespace kb::editor {

class EditorDockModel;

class EditorFloatingWindowQueries {
public:
#if defined(_WIN32)
    EditorFloatingWindowQueries(const FloatingWindowRegistry& registry, const EditorMetrics* metrics, const EditorDockModel* dockModel) noexcept;

    [[nodiscard]] bool IsFloatingWindow(HWND window) const noexcept;
    // The tab group a window shows, and the tab of it that is in front. A window shows the
    // active panel of its group, so a tab switch needs nothing from the window.
    [[nodiscard]] std::uint32_t GroupId(HWND window) const noexcept;
    [[nodiscard]] std::uint32_t PanelId(HWND window) const noexcept;
    // The window of the group the panel is a tab of: any tab, not only the one in front.
    [[nodiscard]] HWND WindowForPanel(std::uint32_t panelId) const noexcept;
    [[nodiscard]] std::vector<HWND> Windows() const;
    [[nodiscard]] LRESULT HitTest(HWND window, LPARAM lparam) const;
    [[nodiscard]] std::optional<FloatingWindowResizeEvent> ResizeEvent(HWND window, int width, int height) const noexcept;
    [[nodiscard]] std::optional<DockRect> RectForPanel(std::uint32_t panelId) const;
#endif

private:
#if defined(_WIN32)
    const FloatingWindowRegistry& registry_;
    const EditorMetrics* metrics_ = nullptr;
    const EditorDockModel* dockModel_ = nullptr;
#endif
};

} // namespace kb::editor
