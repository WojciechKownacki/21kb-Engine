#include "docking/EditorFloatingWindowQueries.hpp"

#if defined(_WIN32)
#include "docking/FloatingWindowRectReader.hpp"
#include "windowing/FloatingWindowHitTestResolver.hpp"

namespace kb::editor {

EditorFloatingWindowQueries::EditorFloatingWindowQueries(const FloatingWindowRegistry& registry, const EditorMetrics* metrics) noexcept
    : registry_(registry)
    , metrics_(metrics) {}

bool EditorFloatingWindowQueries::IsFloatingWindow(HWND window) const noexcept {
    return registry_.ContainsWindow(window);
}

std::uint32_t EditorFloatingWindowQueries::PanelId(HWND window) const noexcept {
    return registry_.PanelId(window);
}

HWND EditorFloatingWindowQueries::WindowForPanel(std::uint32_t panelId) const noexcept {
    return registry_.WindowForPanel(panelId);
}

std::vector<HWND> EditorFloatingWindowQueries::Windows() const {
    return registry_.Windows();
}

LRESULT EditorFloatingWindowQueries::HitTest(HWND window, LPARAM lparam) const {
    if (metrics_ == nullptr) {
        return HTCLIENT;
    }

    return FloatingWindowHitTestResolver::Resolve(window, lparam, *metrics_);
}

std::optional<FloatingWindowResizeEvent> EditorFloatingWindowQueries::ResizeEvent(HWND window, int width, int height) const noexcept {
    return FloatingWindowResizeEventResolver::Resolve(registry_, window, width, height);
}

std::optional<DockRect> EditorFloatingWindowQueries::RectForPanel(std::uint32_t panelId) const {
    return FloatingWindowRectReader::Read(WindowForPanel(panelId));
}

} // namespace kb::editor

#endif
